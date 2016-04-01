/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

#include <assert.h>
#include <stdio.h> /* sprintf */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <time.h>
#ifdef __KERNEL__
#include <net/tcp.h>
#include <pthread.h>	/* pthread_mutex_t */
#endif /* __KERNEL__ */

#include <gfarm/gfarm.h>

#include "gfutil.h"	/* timeval */
#include "gfevent.h"
#include "hash.h"
#include "timer.h"

#include "context.h"
#include "liberror.h"
#include "conn_hash.h"
#include "host.h" /* gfarm_host_info_address_get() */
#include "hostspec.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "schedule.h"
#include "lookup.h"
#include "gfs_profile.h"
#include "filesystem.h"
#include "gfs_failover.h"

#ifndef __KERNEL__
#define SCHED_MUTEX_DCL
#define SCHED_MUTEX_INIT(s)
#define SCHED_MUTEX_DESTROY(s)
#define SCHED_MUTEX_LOCK(s)
#define SCHED_MUTEX_UNLOCK(s)
#else /* __KERNEL__ */
#define SCHED_MUTEX_DCL	pthread_mutex_t sched_mutex;
#define SCHED_MUTEX_INIT(s)	pthread_mutex_init(&(s)->sched_mutex, NULL);
#define SCHED_MUTEX_DESTROY(s)	pthread_mutex_destroy(&(s)->sched_mutex);
#define SCHED_MUTEX_LOCK(s)	pthread_mutex_lock(&(s)->sched_mutex);
#define SCHED_MUTEX_UNLOCK(s)	pthread_mutex_unlock(&(s)->sched_mutex);
#endif /* __KERNEL__ */
/*
 * The outline of current scheduling algorithm is as follows:
 *
 * boolean is_satisfied():
 *	if (desired_number of hosts, which load average is lower or equal
 *	  to IDLE_LOAD_AVERAGE, are found) {
 *		return True;
 *	} else if (enough_number of hosts, which load average is lower or equal
 *	  to SEMI_IDLE_LOAD_AVERAGE, are found) {
 *		return True;
 *	}
 *	return False;
 *
 * void select_hosts():
 *	if (it's read-mode)
 *		select hosts by load average order.
 *	if (it's write-mode)
 *		if there are enough hosts which have enough free space
 *			select only the hosts which have enough free space
 *		else if there are not enough idle hosts
 *			select hosts by load average order
 *		else
 *			select hosts by disk free space order.
 *
 * void finish():
 *	- add VIRTUAL_LOAD_FOR_SCHEDULED_HOST to loadavg cache of
 *	  each scheduled hosts.
 *	- return the hosts.
 *
 * void search_idle_in_networks(networks):
 *	search hosts in the network from cache
 *	if (is_satisfied())
 *		return;
 *	search hosts in the network. i.e. actually call try_host()
 *	if (is_satisfied())
 *		return;
 *	clear `scheduled` member in the cache, and search the cache again.
 *
 *
 * 1. grouping hosts by its network.
 *
 * 2. at first, search hosts on the local network
 *   (i.e. the same network with this client host).
 *	search_idle_in_networks(the local network)
 *
 * 3. if there is at least one network which RTT isn't known yet,
 *    examine the RTT.
 *	search each network one by one in this phase,
 *	e.g. assume there are 3 RTT-unknown networks, say, netA, netB, NetC,
 *	and if each network has several hosts, say host1@netA, host2@netA,
 *	and so on...
 *	The examination is done in the following order:
 *		host1@netA -> host1@netB -> host1@netC
 *		 -> host2@netA -> host2@netB -> host2@netC
 *		 -> host3@netA -> host3@netB -> host3@netC -> ....
 *	Also, concurrently try 3 hosts at most per network,
 *	because the purpose of this phase is to see RTT of each network.
 *
 * 4. search networks by RTT order.
 *	for each: current network ... a network which RTT <
 			min(current*RTT_THRESH_RATIO, current+RTT_THRESH_DIFF)
 *		search_idle_in_networks(the networks)
 *		proceed current network pointer to next RTT level
 *
 * 5. reaching this phase means that not enough_number of hosts are found.
 *	in this case,
 *	- select hosts by load average order
 *	and
 *	- finish().
 *
 * some notes:
 * - load average isn't only condition to see whether the host can be used
 *   or not.
 *   If it's write-mode, we check whether disk free space is enough or not,
 *   by comparing the space against gfarm_get_minimum_free_disk_space().
 * - invalidation of loadavg cache is a bit complicated.
 *   if the load average is cached in this scheduling process, the cache
 *   won't be invalidated.
 *   Otherwise, if LOADAVG_EXPIRATION seconds aren't passed yet, only
 *   `scheduled' member is invalidated.
 *	-> this is a hack. see search_idle_forget_scheduled()
 *   Otherwise `loadavg' member is invalidated, too.
 */

#define CONCURRENCY		(gfarm_ctxp->schedule_concurrency)
#define PER_NET_CONCURRENCY	(gfarm_ctxp->schedule_concurrency_per_net)
				/* used when examining RTT */
#define ENOUGH_RATE		(gfarm_ctxp->schedule_candidates_ratio)
				/* 4.0 * GFARM_F2LL_SCALE */

#define	ADDR_EXPIRATION		(gfarm_ctxp->schedule_cache_timeout)
#define	LOADAVG_EXPIRATION	(gfarm_ctxp->schedule_cache_timeout)
#define	STATFS_EXPIRATION	(gfarm_ctxp->schedule_cache_timeout)

#define RTT_THRESH_RATIO	(gfarm_ctxp->schedule_rtt_thresh_ratio)
				/* 4.0 * GFARM_F2LL_SCALE */
#define RTT_THRESH_DIFF		(gfarm_ctxp->schedule_rtt_thresh_diff)
				/* range to treat as similar distance */

#define staticp	(gfarm_ctxp->schedule_static)

enum gfarm_schedule_search_mode {
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG,
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH,
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH_AND_DISKAVAIL,
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_DISKAVAIL
};

struct gfarm_schedule_static {
	/*
	 * The following hash is shared among all metadata servers,
	 * but it should be OK, because the key is a (host, port, username)
	 * tuple, unless there is inconsistency in a metadata server.
	 */
	struct gfarm_hash_table *search_idle_hosts_state;

	/*
	 * The followings are working area during scheduling
	 */
	int search_idle_candidate_host_number;
	struct search_idle_host_state *search_idle_candidate_list;
	struct search_idle_host_state **search_idle_candidate_last;

	const char *search_idle_domain_filter;
#if 0 /* not yet in gfarm v2 */
	struct gfarm_hash_table *search_idle_arch_filter;
#endif

	/*
	 * The followings are is shared among all metadata servers,
	 * but it must be OK, since these are a global things.
	 */
	/*
	 * local_host may be included in the local_network, thus
	 * network_list should be FIFO order.
	 */
	struct search_idle_network *search_idle_network_list;
	struct search_idle_network **search_idle_network_list_last;
	struct search_idle_network *search_idle_local_net;
	struct search_idle_network **search_idle_local_host;
	int search_idle_local_host_count;

	/* The followings are working area during scheduling */
	struct timeval search_idle_now;

	/* whether need to see authentication or not? */
	enum gfarm_schedule_search_mode default_search_method;

	SCHED_MUTEX_DCL
};

static void search_idle_network_list_free(void);

gfarm_error_t
gfarm_schedule_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_schedule_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->search_idle_hosts_state = NULL;
	s->search_idle_candidate_host_number = 0;
	s->search_idle_candidate_list = NULL;
	s->search_idle_candidate_last = NULL;
	s->search_idle_domain_filter = NULL;
	s->search_idle_network_list = NULL;
	s->search_idle_network_list_last = &s->search_idle_network_list;
	s->search_idle_local_net = NULL;
	s->search_idle_local_host = NULL;
	s->search_idle_local_host_count = 0;
	memset(&s->search_idle_now, 0, sizeof(s->search_idle_now));
	s->default_search_method = GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH;

	SCHED_MUTEX_INIT(s)

	ctxp->schedule_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_schedule_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_schedule_static *s = ctxp->schedule_static;

	if (s == NULL)
		return;

	SCHED_MUTEX_DESTROY(s)
	if (s->search_idle_hosts_state != NULL)
		gfp_conn_hash_table_dispose(s->search_idle_hosts_state);
	search_idle_network_list_free();
	free(s->search_idle_local_host);
	free(s);
}

#if 0 /* not yet in gfarm v2 */
/*
 * data structure which represents architectures which can run a program
 */

#define ARCH_SET_HASHTAB_SIZE 31		/* prime number */

#define IS_IN_ARCH_SET(arch, arch_set) \
	(gfarm_hash_lookup(arch_set, arch, strlen(arch) + 1) != NULL)

#define free_arch_set(arch_set)	gfarm_hash_table_free(arch_set)

/* Create a set of architectures that the program is registered for */
static gfarm_error_t
program_arch_set(char *program, struct gfarm_hash_table **arch_setp)
{
	gfarm_error_t e;
	char *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info *sections;
	struct gfarm_hash_table *arch_set;
	int i, nsections, created;

	e = gfarm_url_make_path(program, &gfarm_file);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = "such program isn't registered";
		free(gfarm_file);
		return (e);
	}
	if (!GFARM_S_IS_PROGRAM(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("specified command is not an executable");
	}
	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	gfarm_path_info_free(&pi);
	free(gfarm_file);
	if (e != GFARM_ERR_NO_ERROR)
		return ("no binary is registered as the specified command");

	arch_set = gfarm_hash_table_alloc(ARCH_SET_HASHTAB_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	if (arch_set == NULL) {
		gfarm_file_section_info_free_all(nsections, sections);
		return (GFARM_ERR_NO_MEMORY);
	}
	/* register architectures of the program to `arch_set' */
	for (i = 0; i < nsections; i++) {
		if (gfarm_hash_enter(arch_set,
		    sections[i].section, strlen(sections[i].section) + 1,
		    sizeof(int), &created) == NULL) {
			free_arch_set(arch_set);
			gfarm_file_section_info_free_all(nsections, sections);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	gfarm_file_section_info_free_all(nsections, sections);
	*arch_setp = arch_set;
	return (GFARM_ERR_NO_ERROR);
}
#endif /* not yet in gfarm v2 */
/*
 * data structure which represents information about a host
 */

#define HOSTS_HASHTAB_SIZE	3079	/* prime number */

struct search_idle_network;

struct search_idle_host_state {
#if 0 /* not yet in gfarm v2 */
	char *architecture;
#endif
	int port, ncpu;
	struct timeval addr_cache_time;	/* always available */
	struct sockaddr addr;		/* if HOST_STATE_FLAG_ADDR_AVAIL */

	struct search_idle_network *net;

	struct timeval rtt_cache_time;	/* if HOST_STATE_FLAG_RTT_TRIED */
	int rtt_usec;			/* if HOST_STATE_FLAG_RTT_AVAIL */

	struct timeval loadavg_cache_time;

	/* loadavg * GFM_PROTO_LOADAVG_FSCALE */
	long long loadavg;		/* if HOST_STATE_FLAG_RTT_AVAIL
					   or HOST_STATE_FLAG_STATFS_AVAIL */

	/*if HOST_STATE_FLAG_STATFS_AVAIL*/
	struct timeval statfs_cache_time;
#if 0
	gfarm_off_t blocks, bfree, bavail;
	gfarm_off_t files, ffree, favail;
	gfarm_int32_t bsize;
#else
	gfarm_off_t diskused, diskavail;
#endif

	gfarm_uint64_t scheduled_age;
#define	HOST_STATE_SCHEDULED_AGE_NOT_FOUND	0
	int scheduled;

	int flags;
#define HOST_STATE_FLAG_ADDR_AVAIL		0x001
#define HOST_STATE_FLAG_RTT_TRIED		0x002
#define HOST_STATE_FLAG_RTT_AVAIL		0x004
/*#define HOST_STATE_FLAG_AUTH_TRIED		0x008*/ /* not actually used */
#define HOST_STATE_FLAG_AUTH_SUCCEED		0x010
#define HOST_STATE_FLAG_STATFS_AVAIL		0x020
/* The followings are working area during scheduling */
#define HOST_STATE_FLAG_JUST_CACHED		0x040
#define HOST_STATE_FLAG_SCHEDULING		0x080
#define HOST_STATE_FLAG_AVAILABLE		0x100
#define HOST_STATE_FLAG_CACHE_WAS_USED		0x200

	/*
	 * The followings are working area during scheduling
	 */

	/* linked in search_idle_candidate_list */
	struct search_idle_host_state *next;

	/* linked in search_idle_network::candidate_list */
	struct search_idle_host_state *next_in_the_net;

	/* work area */
	char *return_value; /* hostname */
};

struct search_idle_network {
	struct search_idle_network *next;
	struct gfarm_hostspec *network;
	int rtt_usec;			/* if NET_FLAG_RTT_AVAIL */
	int flags;
	int local_network;

#define NET_FLAG_NETMASK_KNOWN	0x01
#define NET_FLAG_RTT_AVAIL	0x02

/* The followings are working area during scheduling */
#define NET_FLAG_SCHEDULING	0x04

	/*
	 * The followings are working area during scheduling
	 */
	struct search_idle_host_state *candidate_list;
	struct search_idle_host_state **candidate_last;
	struct search_idle_host_state *cursor;
	int ongoing;
};

static int
is_expired(struct timeval *cached_timep, int expiration)
{
	struct timeval expired;

	expired = *cached_timep;
	expired.tv_sec += expiration;
	return (gfarm_timeval_cmp(&staticp->search_idle_now, &expired) >= 0);
}

static void
search_idle_network_set_local(struct search_idle_network *net)
{
	net->local_network = 1;
}

static int
search_idle_network_is_local(struct search_idle_network *net)
{
	return (net->local_network);
}

static gfarm_error_t
search_idle_network_list_add0(struct sockaddr *addr, int flags,
	struct search_idle_network **netp)
{
	struct search_idle_network *net;
	gfarm_error_t e;

	GFARM_MALLOC(net);
	if (net == NULL) {
		gflog_debug(GFARM_MSG_1003589,
		    "search_idle_network_list_add0: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	net->rtt_usec = 0; /* local or unknown */
	/* XXX - gfarm_addr_network_get() may assume IPv4 class C network */
	e = gfarm_addr_network_get(addr, &net->network);
	if (e != GFARM_ERR_NO_ERROR) {
		free(net);
		gflog_debug(GFARM_MSG_1003590,
		    "search_idle_network_list_add0: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	net->flags = flags;
	net->local_network = 0;
	net->candidate_list = NULL;
	net->candidate_last = &net->candidate_list;
	net->ongoing = 0;
	net->next = NULL;
	*staticp->search_idle_network_list_last = net;
	staticp->search_idle_network_list_last = &net->next;
	if (netp != NULL)
		*netp = net;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
search_idle_network_list_local_host_init(void)
{
	int count, i, j;
	struct in_addr *self_ip;
	struct search_idle_network *net;
	gfarm_error_t e, save_e = GFARM_ERR_NO_ERROR;
	struct sockaddr_in addr_in;

	e = gfarm_get_ip_addresses(&count, &self_ip);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	GFARM_MALLOC_ARRAY(staticp->search_idle_local_host, count);
	if (staticp->search_idle_local_host == NULL) {
		free(self_ip);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0, j = 0; i < count; ++i) {
		addr_in.sin_family = AF_INET;
		addr_in.sin_addr = self_ip[i];
		e = search_idle_network_list_add0((struct sockaddr *)&addr_in,
			NET_FLAG_NETMASK_KNOWN | NET_FLAG_RTT_AVAIL, &net);
		if (e == GFARM_ERR_NO_ERROR) {
			staticp->search_idle_local_host[j++] = net;
			search_idle_network_set_local(net);
		} else if (save_e == GFARM_ERR_NO_ERROR)
			save_e = e;
	}
	free(self_ip);
	staticp->search_idle_local_host_count = j;
	return (j > 0 ? GFARM_ERR_NO_ERROR : save_e);
}

static gfarm_error_t
search_idle_network_list_init(struct gfm_connection *gfm_server)
{
	gfarm_error_t e;
	char *self_name;
	struct search_idle_network *net;
	struct sockaddr peer_addr;
	int port;

	assert(staticp->search_idle_network_list == NULL);

	e = search_idle_network_list_local_host_init();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_host_get_canonical_self_name(gfm_server,
	    &self_name, &port);
	if (e != GFARM_ERR_NO_ERROR)
		self_name = gfarm_host_get_self_name();
	/*
	 * XXX FIXME
	 * This is a suspicious part.
	 * Maybe the peer_addr is not same among different metadata servers.
	 *
	 * Probably it's better to create the `net' variable by using
	 * the results of gfarm_get_ip_addresses().
	 */
	/* XXX FIXME this port number (0) is dummy */
	e = gfm_host_address_get(gfm_server, self_name, 0,
	    &peer_addr, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000171,
		    "gfarm search_idle_network_list_init: "
		    "self address_get(%s): %s",
		    self_name, gfarm_error_string(e));
		return (e);
	}
	e = search_idle_network_list_add0(&peer_addr,
		NET_FLAG_NETMASK_KNOWN | NET_FLAG_RTT_AVAIL, &net);
	if (e == GFARM_ERR_NO_ERROR) {
		staticp->search_idle_local_net = net;
		search_idle_network_set_local(net);
	}
	return (e);
}

static void
search_idle_network_list_free(void)
{
	struct search_idle_network *net, *next;

	for (net = staticp->search_idle_network_list; net != NULL;
	     net = next) {
		next = net->next;
		free(net);
	}

	staticp->search_idle_network_list = NULL;
}

static gfarm_error_t
search_idle_network_list_add(struct sockaddr *addr,
	struct search_idle_network **netp)
{
	struct search_idle_network *net;

	/* XXX - if there are lots of networks, this is too slow */
	for (net = staticp->search_idle_network_list; net != NULL;
	    net = net->next) {
		if (!gfarm_hostspec_match(net->network, NULL, addr))
			continue;
		*netp = net;
		return (GFARM_ERR_NO_ERROR);
	}
	/* first host in the network */
	return (search_idle_network_list_add0(addr,
		NET_FLAG_NETMASK_KNOWN, netp));
}

static gfarm_error_t
search_idle_host_state_init(struct gfm_connection *gfm_server)
{
	gfarm_error_t e;
	static const char diag[] = "search_idle_host_state_init";

	e = gfp_conn_hash_table_init(&staticp->search_idle_hosts_state,
	    HOSTS_HASHTAB_SIZE);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001427,
		    "%s: hash_table_init: %s", diag, gfarm_error_string(e));
		return (e);
	}

	/* ignore any error here */
	e = search_idle_network_list_init(gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_notice(GFARM_MSG_1004312, "%s: network_list_init: %s",
		    diag, gfarm_error_string(e));

	/* when a connection error happens, make the host unavailable. */
	gfs_client_add_hook_for_connection_error(
		gfarm_schedule_host_cache_purge);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
search_idle_host_state_add_host_sched_info(struct gfm_connection *gfm_server,
	struct gfarm_host_sched_info *info,
	struct search_idle_host_state **hp)
{
	gfarm_error_t e;
	char *hostname = info->host;
	int created;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	if (staticp->search_idle_hosts_state == NULL) {
		e = search_idle_host_state_init(gfm_server);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001428,
			    "search_idle_host_state_add_host_sched_info: "
			    "search_idle_host_state_init: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}

	e = gfp_conn_hash_enter(&staticp->search_idle_hosts_state,
	    HOSTS_HASHTAB_SIZE,
	    sizeof(*h), hostname, info->port, gfm_client_username(gfm_server),
	    &entry, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001429,
		    "search_idle_host_state_add_host_sched_info: "
		    "gfp_conn_hash_enter: %s",
		    gfarm_error_string(e));
		return (e);
	}

	h = gfarm_hash_entry_data(entry);
	if (created || (h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0 ||
	    is_expired(&h->addr_cache_time, ADDR_EXPIRATION)) {
		if (created) {
			memset(h, 0, sizeof(*h));
			h->port = info->port;
			h->ncpu = info->ncpu;
#if 0 /* not yet in gfarm v2 */
			h->architecture = strdup(info->architecture);
			if (h->architecture == NULL) {
				gfp_conn_hash_purge(
				    staticp->search_idle_hosts_state, entry);
				return (GFARM_ERR_NO_MEMORY);
			}
#endif
			h->net = NULL;
			h->scheduled_age =
			    HOST_STATE_SCHEDULED_AGE_NOT_FOUND + 1;
			h->scheduled = 0;
			h->flags = 0;
		} else if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0) {
			if (!is_expired(&h->addr_cache_time,
				ADDR_EXPIRATION)) {
				gflog_debug(GFARM_MSG_1001430,
				    "search_idle_host_state_"
				    "add_host_sched_info: %s: unknown host",
				    hostname);
				return (GFARM_ERR_UNKNOWN_HOST);
			}
		} else { /* cope with address change */
			assert(is_expired(&h->addr_cache_time, ADDR_EXPIRATION)
			    );
			h->flags &= ~HOST_STATE_FLAG_ADDR_AVAIL;
			h->net = NULL;
		}
		e = gfm_host_address_get(gfm_server,
		    hostname, h->port, &h->addr, NULL);
		gettimeofday(&h->addr_cache_time, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001431,
			    "search_idle_host_state_add_host_sched_info: "
			    "gfm_host_address_get(%s): %s",
			    hostname, gfarm_error_string(e));
			return (e);
		}
		h->flags |= HOST_STATE_FLAG_ADDR_AVAIL;
		e = search_idle_network_list_add(&h->addr, &h->net);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001432,
			    "search_idle_host_state_add_host_sched_info: "
			    "search_idle_network_list_add: %s",
			    gfarm_error_string(e));
			h->net = NULL;
			return (e);
		}
	} else if (h->net == NULL) {
		/* search_idle_network_list_add() failed at the last time */
		e = search_idle_network_list_add(&h->addr, &h->net);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001433,
			    "search_idle_host_state_add_host_sched_info: "
			    "retrying search_idle_network_list_add: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}

	*hp = h;
	return (GFARM_ERR_NO_ERROR);
}

/* forget that this user was authenticated by the specified host */
gfarm_error_t
gfarm_schedule_host_cache_purge(struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	if (staticp->search_idle_hosts_state == NULL)
		return (GFARM_ERR_NO_ERROR);

	e = gfp_conn_hash_lookup(&staticp->search_idle_hosts_state,
	    HOSTS_HASHTAB_SIZE,
	    gfs_client_hostname(gfs_server),
	    gfs_client_port(gfs_server),
	    gfs_client_username(gfs_server),
	    &entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001434,
		    "gfarm_schedule_host_cache_purge: "
		    "connection is not cached: %s",
		    gfarm_error_string(e));
		return (e);
	}

	h = gfarm_hash_entry_data(entry);
	h->flags &= ~HOST_STATE_FLAG_AUTH_SUCCEED;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_schedule_host_cache_reset(struct gfm_connection *gfm_server, int nhosts,
	struct gfarm_host_sched_info *infos)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	int i, host_flags;

	host_flags =
	    HOST_STATE_FLAG_JUST_CACHED|
	    HOST_STATE_FLAG_RTT_TRIED|
	    HOST_STATE_FLAG_SCHEDULING|
	    HOST_STATE_FLAG_AVAILABLE|
	    HOST_STATE_FLAG_CACHE_WAS_USED;

	if (staticp->search_idle_hosts_state == NULL)
		return;

	for (i = 0; i < nhosts; ++i) {
		e = gfp_conn_hash_lookup(&staticp->search_idle_hosts_state,
		    HOSTS_HASHTAB_SIZE, infos[i].host, infos[i].port,
		    gfm_client_username(gfm_server), &entry);
		if (e != GFARM_ERR_NO_ERROR)
			continue;

		h = gfarm_hash_entry_data(entry);
		h->flags &= ~host_flags;
	}
	return;
}

static gfarm_error_t
search_idle_candidate_list_reset(struct gfm_connection *gfm_server,
	int host_flags)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	struct search_idle_network *net;

	if (staticp->search_idle_hosts_state == NULL) {
		gfarm_error_t e = search_idle_host_state_init(gfm_server);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001435,
			    "search_idle_candidate_list_reset: "
			    "search_idle_host_state_init: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}

	staticp->search_idle_candidate_host_number = 0;
	staticp->search_idle_candidate_list = NULL;
	staticp->search_idle_candidate_last =
	    &staticp->search_idle_candidate_list;
	host_flags |=
	    HOST_STATE_FLAG_SCHEDULING|
	    HOST_STATE_FLAG_AVAILABLE|
	    HOST_STATE_FLAG_CACHE_WAS_USED;
	for (gfarm_hash_iterator_begin(staticp->search_idle_hosts_state, &it);
	    !gfarm_hash_iterator_is_end(&it); gfarm_hash_iterator_next(&it)) {
		entry = gfarm_hash_iterator_access(&it);
		h = gfarm_hash_entry_data(entry);
		h->flags &= ~host_flags;
	}

	for (net = staticp->search_idle_network_list; net != NULL;
	    net = net->next) {
		net->flags &= ~NET_FLAG_SCHEDULING;
		net->ongoing = 0;
		net->candidate_list = NULL;
		net->candidate_last = &net->candidate_list;
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
search_idle_candidate_list_init(struct gfm_connection *gfm_server)
{
#if 0 /* not yet in gfarm v2 */
	search_idle_arch_filter = NULL;
#endif
	staticp->search_idle_domain_filter = NULL;

	gettimeofday(&staticp->search_idle_now, NULL);

	return (search_idle_candidate_list_reset(gfm_server,
	    HOST_STATE_FLAG_JUST_CACHED));
}

#if 0 /* not yet in gfarm v2 */
/*
 * similar to search_idle_candidate_list_init(),
 * but this does not clear HOST_STATE_FLAG_JUST_CACHED.
 * also, does not reset `search_idle_arch_filter', `search_idle_domain_filter'
 * and `search_idle_now'.
 */
static gfarm_error_t
search_idle_candidate_list_clear(void)
{
	return (search_idle_candidate_list_reset(0));
}
#endif

#define search_idle_set_domain_filter(domain)	\
	(staticp->search_idle_domain_filter = (domain))

#if 0 /* not yet in gfarm v2 */
static gfarm_error_t
search_idle_set_program_filter(char *program)
{
	return (program_arch_set(program, &search_idle_arch_filter));
}

static void
search_idle_free_program_filter(void)
{
	free_arch_set(search_idle_arch_filter);
}
#endif /* not yet in gfarm v2 */

static long long
entropy(void)
{
	/* 0 ... (GFM_PROTO_LOADAVG_FSCALE / 100) */
	return (gfarm_random() * GFM_PROTO_LOADAVG_FSCALE / 100
	    / (RAND_MAX + 1LL));
}

static gfarm_error_t
search_idle_candidate_list_add(struct gfm_connection *gfm_server,
	struct gfarm_host_sched_info *info)
{
	gfarm_error_t e;
	char *hostname = info->host;
	struct search_idle_host_state *h;

	if ((info->flags & GFM_PROTO_SCHED_FLAG_HOST_AVAIL) == 0)
		return (GFARM_ERR_NO_ERROR);

	if (staticp->search_idle_domain_filter != NULL &&
	    !gfarm_host_is_in_domain(hostname,
		staticp->search_idle_domain_filter))
		return (GFARM_ERR_NO_ERROR); /* ignore this host */
#if 0 /* not yet in gfarm v2 */
	if (host_info != NULL && search_idle_arch_filter != NULL &&
	    !IS_IN_ARCH_SET(host_info->architecture,
		search_idle_arch_filter)) {
		/* ignore this host, hostname == NULL case */
		return (GFARM_ERR_NO_ERROR);
	}
#endif

	e = search_idle_host_state_add_host_sched_info(gfm_server, info, &h);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_UNKNOWN_HOST) /* just ignore this host */
			return (GFARM_ERR_NO_ERROR);
		gflog_debug(GFARM_MSG_1001436,
		    "search_idle_candidate_list_add: "
		    "search_idle_host_state_add_host_sched_info: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if ((h->flags & HOST_STATE_FLAG_SCHEDULING) != 0) {
		/* same host is specified twice or more */
		return (GFARM_ERR_NO_ERROR);
	}

#if 0 /* not yet in gfarm v2 */
	if (host_info == NULL && search_idle_arch_filter != NULL &&
	    (h->architecture == NULL /* allow non-spool_host for now */ ||
	     !IS_IN_ARCH_SET(h->architecture, search_idle_arch_filter))) {
		/* ignore this host, hostname != NULL case */
		return (GFARM_ERR_NO_ERROR);
	}
#endif

	if (info->flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
#ifdef __KERNEL__
		int update_loadavg = 1;
#else
		int update_loadavg =
			(h->flags & HOST_STATE_FLAG_RTT_AVAIL) == 0 ||
			h->loadavg_cache_time.tv_sec < info->cache_time;
#endif
		if (update_loadavg) {
			h->loadavg_cache_time.tv_sec = info->cache_time;
			h->loadavg_cache_time.tv_usec = 0;
			/* add entropy to randomize the scheduling result */
			h->loadavg = info->loadavg + entropy();
		}
		h->statfs_cache_time.tv_sec = info->cache_time;
		h->statfs_cache_time.tv_usec = 0;
		/* convert KiByte to Byte */
		h->diskused = info->disk_used * 1024;
		h->diskavail = info->disk_avail * 1024;
		h->flags |= HOST_STATE_FLAG_STATFS_AVAIL;
	}

	h->flags |= HOST_STATE_FLAG_SCHEDULING;
	h->net->flags |= NET_FLAG_SCHEDULING;

	/* link to search_idle_candidate_list */
	h->next = NULL;
	staticp->search_idle_candidate_host_number++;
	*staticp->search_idle_candidate_last = h;
	staticp->search_idle_candidate_last = &h->next;

	/* link to h->net->candidate_list */
	h->next_in_the_net = NULL;
	*h->net->candidate_last = h;
	h->net->candidate_last = &h->next_in_the_net;

	/*
	 * return hostname parameter, instead of cached hostname, as results.
	 * this is needed for gfarm_schedule_hosts()
	 * and gfarm_schedule_hosts_acyclic() which return
	 * input hostnames instead of newly allocated strings.
	 */
	h->return_value = hostname;
	return (GFARM_ERR_NO_ERROR);
}

/* whether need to see authentication or not? */

void
gfarm_schedule_search_mode_use_loadavg(void)
{
	staticp->default_search_method = GFARM_SCHEDULE_SEARCH_BY_LOADAVG;
}

#define IDLE_LOAD_AVERAGE	(gfarm_ctxp->schedule_idle_load * \
				 GFM_PROTO_LOADAVG_FSCALE / GFARM_F2LL_SCALE)
				/* 0.5 * GFM_PROTO_LOADAVG_FSCALE */
#define SEMI_IDLE_LOAD_AVERAGE	(gfarm_ctxp->schedule_busy_load * \
				 GFM_PROTO_LOADAVG_FSCALE / GFARM_F2LL_SCALE)
				/* 0.1 * GFM_PROTO_LOADAVG_FSCALE */
#define VIRTUAL_LOAD_FOR_SCHEDULED_HOST \
				(gfarm_ctxp->schedule_virtual_load * \
				 GFM_PROTO_LOADAVG_FSCALE / GFARM_F2LL_SCALE)
				/* 0.3 * GFM_PROTO_LOADAVG_FSCALE */

struct search_idle_state {
	struct gfarm_eventqueue *q;

	int desired_number;
	int enough_number;
	enum gfarm_schedule_search_mode mode;
	int write_mode;

	int available_hosts_number;
	int usable_hosts_number;
	int idle_hosts_number;
	int semi_idle_hosts_number;

	int concurrency;

	struct gfm_connection *gfm_server;
	struct gfarm_filesystem *filesystem;
};

static gfarm_error_t
search_idle_init_state(struct search_idle_state *s,
	struct gfm_connection *gfm_server, int desired_hosts,
	enum gfarm_schedule_search_mode mode, int write_mode)
{
	int syserr;

	s->desired_number = desired_hosts;
	s->enough_number = desired_hosts * ENOUGH_RATE / GFARM_F2LL_SCALE;
	s->mode = mode;
	s->write_mode = write_mode;
	if (write_mode)
		s->mode =
		    GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_DISKAVAIL;
	/*
	 * If we don't check enough_number or desired_number here,
	 * the behavior of search_idle() becomes undeterministic.
	 * i.e. If there is a candidate, search_idle() returns NULL,
	 * otherwise GFARM_ERR_NO_FILESYSTEM_NODE.
	 */
	if (s->enough_number == 0 || s->desired_number == 0 ||
	    staticp->search_idle_candidate_list == NULL) {
		gflog_debug(GFARM_MSG_1001437,
		    "search_idle_init_state: no answer is requested");
		return (GFARM_ERR_NO_FILESYSTEM_NODE);
	}

	syserr = gfarm_eventqueue_alloc(CONCURRENCY, &s->q);
	if (syserr != 0) {
		gflog_debug(GFARM_MSG_1002720,
		    "search_idle_init_state: gfarm_eventqueue_alloc: %s",
		    strerror(syserr));
		return (GFARM_ERR_NO_MEMORY);
	}
	s->available_hosts_number = s->usable_hosts_number =
	    s->idle_hosts_number = s->semi_idle_hosts_number = 0;
	s->concurrency = 0;
	s->gfm_server = gfm_server;
	s->filesystem = gfarm_filesystem_get_by_connection(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

static void
search_idle_count(struct search_idle_state *s,
	struct search_idle_host_state *h,
	int *usable_numberp, int *idle_numberp, int *semi_idle_numberp)
{
	long long loadavg = h->loadavg;
	int ncpu = h->ncpu;
	int ok = 1;
	char *msg1, *msg2, *msg3;

	msg1 = msg2 = msg3 = "";
	if (s->write_mode &&
	    (h->flags & HOST_STATE_FLAG_STATFS_AVAIL) != 0 &&
	    h->diskavail < gfarm_get_minimum_free_disk_space())
		ok = 0; /* not enough free space */
	if (ok) {
		(*usable_numberp)++;
		msg1 = "usable ";
	}

	if (ncpu <= 0) /* sanity */
		ncpu = 1;
	loadavg += h->scheduled * VIRTUAL_LOAD_FOR_SCHEDULED_HOST;
	if (ok && loadavg / ncpu <= IDLE_LOAD_AVERAGE) {
		(*idle_numberp)++;
		msg2 = "idle ";
	}

	/*
	 * We don't use (loadavg / h->host_info->ncpu) to count
	 * semi_idle_hosts here for now, because it is possible
	 * that there is a process which is consuming 100% of
	 * memory or 100% of I/O bandwidth on the host.
	 */
	if (ok && loadavg <= SEMI_IDLE_LOAD_AVERAGE) {
		(*semi_idle_numberp)++;
		msg3 = "semi-idle ";
	}
	gflog_debug(GFARM_MSG_1004313, "%s: %s%s%s"
	    "(avail %llu load %lld ncpu %d)", h->return_value, msg1, msg2, msg3,
	    (unsigned long long)h->diskavail, loadavg, ncpu);
}

static void
search_idle_record_host(struct search_idle_state *s,
	struct search_idle_host_state *h)
{
	if ((h->flags & HOST_STATE_FLAG_AVAILABLE) != 0)
		return;

	search_idle_count(s, h,
	    &s->usable_hosts_number,
	    &s->idle_hosts_number,
	    &s->semi_idle_hosts_number);

	h->flags |= HOST_STATE_FLAG_AVAILABLE;
	s->available_hosts_number++;
}

/* hack */
static void
search_idle_forget_scheduled(struct search_idle_state *s,
	struct search_idle_host_state *h)
{
	int junk = 0;
	int idle1, semi_idle1;
	int idle2, semi_idle2;

	if (h->scheduled == 0)
		return; /* short cut: nothing will be changed in this case */

	idle1 = semi_idle1 = 0;
	search_idle_count(s, h, &junk, &idle1, &semi_idle1);

	h->scheduled_age++;
	h->scheduled = 0; /* forget it */

	idle2 = semi_idle2 = 0;
	search_idle_count(s, h, &junk, &idle2, &semi_idle2);

	if (idle1 == 0 && idle2 != 0)
		s->idle_hosts_number++;
	if (semi_idle1 == 0 && semi_idle2 != 0)
		s->semi_idle_hosts_number++;
}

static int
search_idle_is_satisfied(struct search_idle_state *s)
{
	return (s->idle_hosts_number >= s->desired_number ||
	    s->semi_idle_hosts_number >= s->enough_number ||
	    s->available_hosts_number >=
		staticp->search_idle_candidate_host_number);
}

struct search_idle_callback_closure {
	struct search_idle_state *state;
	struct search_idle_host_state *h;
	int do_record;
	void *protocol_state;
	struct gfs_connection *gfs_server;
};

static void
search_idle_record(struct search_idle_callback_closure *c)
{
	if (c->do_record)
		search_idle_record_host(c->state, c->h);
}

#ifndef __KERNEL__
static void
search_idle_statfs_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	struct search_idle_host_state *h = c->h;
	gfarm_error_t e;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail;
	gfarm_off_t files, ffree, favail;


	e = gfs_client_statfs_result_multiplexed(c->protocol_state,
	    &bsize, &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e == GFARM_ERR_NO_ERROR) {
		h->flags |= HOST_STATE_FLAG_STATFS_AVAIL;
		h->statfs_cache_time = h->rtt_cache_time;
		h->diskused = blocks * bsize;
		h->diskavail = bavail * bsize;
		search_idle_record(c); /* completed */
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001439,
		    "search_idle_statfs_callback: "
		    "gfs_client_statfs_result_multiplexed: %s",
		    gfarm_error_string(e));
	}
	gfs_client_connection_free(c->gfs_server);
	c->state->concurrency--;
	h->net->ongoing--;
	free(c);
}

static void
search_idle_connect_callback(void *closure)
{
	gfarm_error_t e;
	struct search_idle_callback_closure *c = closure;
#if 0 /* We always see disk free space */
	struct search_idle_state *s = c->state;
#endif
	struct gfs_client_statfs_state *ss;

	e = gfs_client_connect_result_multiplexed(c->protocol_state,
	    &c->gfs_server);
	if (e == GFARM_ERR_NO_ERROR) {
		/* The following may fail, if it's already in the cache */
		gfs_client_connection_enter_cache(c->gfs_server);

		c->h->flags |= HOST_STATE_FLAG_AUTH_SUCCEED;
#if 0 /* We always see disk free space */
		if (s->mode == GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH) {
			/* completed */
			search_idle_record(c);
			gfs_client_connection_free(c->gfs_server);
		} else {
#endif
			e = gfs_client_statfs_request_multiplexed(c->state->q,
			    c->gfs_server, ".", search_idle_statfs_callback, c,
			    &ss);
			if (e == GFARM_ERR_NO_ERROR) {
				c->protocol_state = ss;
				return; /* request continues */
			}
			/* failed to request */
			gfs_client_connection_free(c->gfs_server);
			gflog_debug(GFARM_MSG_1001440,
			    "search_idle_connect_callback: "
			    "gfs_client_statfs_request_multiplexed: %s",
			    gfarm_error_string(e));
#if 0 /* We always see disk free space */
		}
#endif
	} else {
		gflog_debug(GFARM_MSG_1001441,
		    "search_idle_connect_callback: "
		    "gfs_client_connect_result_multiplexed: %s",
		    gfarm_error_string(e));
	}
	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}

static void
search_idle_load_callback(void *closure)
{
	gfarm_error_t e;
	struct search_idle_callback_closure *c = closure;
	struct search_idle_state *s = c->state;
	struct gfs_client_load load;
	struct gfs_client_connect_state *cs;
	struct timeval rtt;

	e = gfs_client_get_load_result_multiplexed(c->protocol_state, &load);
	if (e == GFARM_ERR_NO_ERROR) {
		c->h->flags |= HOST_STATE_FLAG_RTT_AVAIL;
		/* add entropy to randomize the scheduling result */
		c->h->loadavg =
		    load.loadavg_1min * GFM_PROTO_LOADAVG_FSCALE + entropy();
		c->h->loadavg_cache_time = c->h->rtt_cache_time;
		c->h->scheduled_age++;
		c->h->scheduled = 0; /* because now we know real loadavg */

		/* update RTT */
		gettimeofday(&rtt, NULL);
		gfarm_timeval_sub(&rtt, &c->h->rtt_cache_time);
		c->h->rtt_usec = rtt.tv_sec * GFARM_SECOND_BY_MICROSEC +
		    rtt.tv_usec;
		if ((c->h->net->flags & NET_FLAG_RTT_AVAIL) == 0 ||
		    c->h->net->rtt_usec > c->h->rtt_usec) {
			c->h->net->flags |= NET_FLAG_RTT_AVAIL;
			c->h->net->rtt_usec = c->h->rtt_usec;
		}

		if (s->mode == GFARM_SCHEDULE_SEARCH_BY_LOADAVG) {
			/* completed */
			search_idle_record(c);
		} else {
#if 0 /* not actully used */
			c->h->flags |= HOST_STATE_FLAG_AUTH_TRIED;
#endif
			e = gfs_client_connect_request_multiplexed(c->state->q,
			    c->h->return_value, c->h->port,
			    gfm_client_username(s->gfm_server),
			    &c->h->addr, s->filesystem,
			    search_idle_connect_callback, c,
			    &cs);
			if (e == GFARM_ERR_NO_ERROR) {
				c->protocol_state = cs;
				return; /* request continues */
			}
			/* failed to connect */
			gflog_debug(GFARM_MSG_1001442,
			    "search_idle_load_callback: "
			    "gfs_client_connect_request_multiplexed: "
			    "%s", gfarm_error_string(e));
		}
	} else {
		gflog_debug(GFARM_MSG_1001443,
		    "search_idle_load_callback: "
		    "gfs_client_get_load_result_multiplexed: %s",
		    gfarm_error_string(e));
	}
	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}
#else /* __KERNEL__ */
static void
search_idle_connect_and_get_rtt_callback(void *closure)
{
	gfarm_error_t e;
	struct search_idle_callback_closure *c = closure;

	e = gfs_client_connect_result_multiplexed(c->protocol_state,
	    &c->gfs_server);
	if (e == GFARM_ERR_NO_ERROR) {
		struct tcp_info info;
		socklen_t len;

		c->h->flags |= HOST_STATE_FLAG_RTT_AVAIL;
		assert(c->h->flags & HOST_STATE_FLAG_STATFS_AVAIL);
		c->h->scheduled_age++;
		c->h->scheduled = 0; /* because now we know real loadavg */

		/* update RTT */
		len = sizeof(info);
		if (getsockopt(gfs_client_connection_fd(c->gfs_server),
			IPPROTO_TCP, TCP_INFO,  &info, &len) < 0) {
			e = gfarm_errno_to_error(errno);
			gflog_debug(GFARM_MSG_1003989,
			    "search_idle_connect_and_get_rtt_callback: "
			    "getsockopt: %s",
			    gfarm_error_string(e));
		} else {
			c->h->flags |= HOST_STATE_FLAG_RTT_AVAIL;
			c->h->rtt_usec = info.tcpi_rtt;
			if ((c->h->net->flags & NET_FLAG_RTT_AVAIL) == 0 ||
			    c->h->net->rtt_usec > c->h->rtt_usec) {
				c->h->net->flags |= NET_FLAG_RTT_AVAIL;
				c->h->net->rtt_usec = c->h->rtt_usec;
			}
		}

		/* The following may fail, if it's already in the cache */
		gfs_client_connection_enter_cache_tail(c->gfs_server);

		c->h->flags |= HOST_STATE_FLAG_AUTH_SUCCEED;
		search_idle_record(c); /* completed */
	} else {
		gflog_debug(GFARM_MSG_1003990,
		    "search_idle_connect_and_get_rtt_callback: "
		    "gfs_client_connect_result_multiplexed: %s",
		    gfarm_error_string(e));
	}
	gfs_client_connection_free(c->gfs_server);

	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}
#endif /* __KERNEL__ */

static int
net_rtt_compare(const void *a, const void *b)
{
	struct search_idle_network *const *aa = a;
	struct search_idle_network *const *bb = b;
	const struct search_idle_network *p = *aa;
	const struct search_idle_network *q = *bb;
	int l1 = p->rtt_usec;
	int l2 = q->rtt_usec;

	if (l1 < l2)
		return (-1);
	else if (l1 > l2)
		return (1);
	else
		return (0);
}

static int
davail_compare(const void *a, const void *b)
{
	struct search_idle_host_state *const *aa = a;
	struct search_idle_host_state *const *bb = b;
	const struct search_idle_host_state *p = *aa;
	const struct search_idle_host_state *q = *bb;
	const gfarm_off_t df1 = p->diskavail;
	const gfarm_off_t df2 = q->diskavail;

	if (df1 > df2)
		return (-1);
	else if (df1 < df2)
		return (1);
	else
		return (0);
}

static int
loadavg_compare(const void *a, const void *b)
{
	struct search_idle_host_state *const *aa = a;
	struct search_idle_host_state *const *bb = b;
	const struct search_idle_host_state *p = *aa;
	const struct search_idle_host_state *q = *bb;
	const long long l1 = (p->loadavg
	    + p->scheduled * VIRTUAL_LOAD_FOR_SCHEDULED_HOST) / p->ncpu;
	const long long l2 = (q->loadavg
	    + q->scheduled * VIRTUAL_LOAD_FOR_SCHEDULED_HOST) / q->ncpu;

	if (l1 < l2)
		return (-1);
	else if (l1 > l2)
		return (1);
	else
		return (0);
}

static int
search_idle_cache_should_be_used(struct search_idle_host_state *h)
{
	/*
	 * the expiration of HOST_STATE_FLAG_ADDR_AVAIL is already coped
	 * by search_idle_candidate_list_add()
	 */
	if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
		return (1); /* IP address isn't resolvable, even */

	return ((h->flags & HOST_STATE_FLAG_RTT_TRIED) != 0 &&
	    !is_expired(&h->loadavg_cache_time, LOADAVG_EXPIRATION));

}

static int
search_idle_cache_is_available(struct search_idle_state *s,
	struct search_idle_host_state *h)
{
	/*
	 * the expiration of HOST_STATE_FLAG_ADDR_AVAIL is already coped
	 * by search_idle_candidate_list_add()
	 */
	if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
		return (0);

	if (is_expired(&h->loadavg_cache_time, LOADAVG_EXPIRATION))
		return (0);

	switch (s->mode) {
	case GFARM_SCHEDULE_SEARCH_BY_LOADAVG:
		return ((h->flags & HOST_STATE_FLAG_RTT_AVAIL) != 0);
	case GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH:
		return ((h->flags & HOST_STATE_FLAG_AUTH_SUCCEED) != 0);
	case GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH_AND_DISKAVAIL:
		return ((h->flags &
		   (HOST_STATE_FLAG_AUTH_SUCCEED|HOST_STATE_FLAG_STATFS_AVAIL))
		== (HOST_STATE_FLAG_AUTH_SUCCEED|HOST_STATE_FLAG_STATFS_AVAIL)
		&& !is_expired(&h->statfs_cache_time, STATFS_EXPIRATION));
	case GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_DISKAVAIL:
		return ((h->flags & HOST_STATE_FLAG_STATFS_AVAIL) != 0
		&& !is_expired(&h->statfs_cache_time, STATFS_EXPIRATION));
	default:
		assert(0);
		return (0);
	}
}

static gfarm_error_t
search_idle_try_host(struct search_idle_state *s,
	struct search_idle_host_state *h, int do_record)
{
	gfarm_error_t e;
	int rv;
	struct search_idle_callback_closure *c;
	struct gfs_client_get_load_state *gls;

	/* already tried? */
	if ((h->flags & HOST_STATE_FLAG_JUST_CACHED) != 0)
		return (GFARM_ERR_NO_ERROR);

	/* if we know the availability of this host, don't try */
	if (search_idle_cache_should_be_used(h))
		return (GFARM_ERR_NO_ERROR);

	/* We limit concurrency here */
	rv = 0;
	while (s->concurrency >= CONCURRENCY) {
		rv = gfarm_eventqueue_turn(s->q, NULL);
		/* XXX - how to report this error? */
		if (rv != 0 && rv != EAGAIN && rv != EINTR) {
			gflog_debug(GFARM_MSG_1001444,
			    "search_idle_try_host: gfarm_eventqueue_turn: %s",
			    strerror(rv));
			return (gfarm_errno_to_error(rv));
		}
	}

	GFARM_MALLOC(c);
	if (c == NULL) {
		gflog_debug(GFARM_MSG_1001445,
		    "search_idle_try_host: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	c->state = s;
	c->h = h;
	c->do_record = do_record;
	h->flags |=
	    HOST_STATE_FLAG_JUST_CACHED|
	    HOST_STATE_FLAG_RTT_TRIED;
	gettimeofday(&h->rtt_cache_time, NULL);
#ifndef __KERNEL__
	e = gfs_client_get_load_request_multiplexed(s->q, &h->addr,
	    search_idle_load_callback, c, &gls, 1);
#else /* __KERNEL__ */
	e = gfs_client_connect_request_multiplexed(s->q,
			    h->return_value, h->port,
			    gfm_client_username(s->gfm_server),
			    &h->addr, s->filesystem,
			    search_idle_connect_and_get_rtt_callback, c,
			    (struct gfs_client_connect_state **)&gls);
#endif /* __KERNEL__ */
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001446,
		    "search_idle_try_host: "
		    "gfs_client_get_load_request_multiplexed: %s",
		    gfarm_error_string(e));
		free(c);
		return (GFARM_ERR_NO_ERROR); /* We won't report the error */
	}
	c->protocol_state = gls;
	s->concurrency++;
	h->net->ongoing++;
	return (GFARM_ERR_NO_ERROR);
}

static void
search_idle_in_networks(struct search_idle_state *s,
	int nnets, struct search_idle_network **nets)
{
	int i;
	struct search_idle_host_state *h;

	/* see cached hosts with using `scheduled` field */
	for (i = 0; i < nnets; i++) {
		for (h = nets[i]->candidate_list;
		    h != NULL; h = h->next_in_the_net) {
			if (search_idle_cache_is_available(s, h)) {
				search_idle_record_host(s, h);
				h->flags |= HOST_STATE_FLAG_CACHE_WAS_USED;
			}
		}
	}
	if (search_idle_is_satisfied(s))
		return;

	/* try hosts which aren't cached */

	for (i = 0; i < nnets; i++) {
		for (h = nets[i]->candidate_list;
		    h != NULL; h = h->next_in_the_net) {
			/* XXX report this error? */
			(void)search_idle_try_host(s, h, 1);
			if (search_idle_is_satisfied(s))
				goto end_of_trial;
		}
	}
end_of_trial:
	/* ignore return value */
	(void)gfarm_eventqueue_loop(s->q, NULL); /* XXX - report rv? */
	if (search_idle_is_satisfied(s))
		return;

	/* see cached hosts without using `scheduled` field */
	for (i = 0; i < nnets; i++) {
		for (h = nets[i]->candidate_list;
		    h != NULL; h = h->next_in_the_net) {
			if ((h->flags & HOST_STATE_FLAG_CACHE_WAS_USED) != 0)
				search_idle_forget_scheduled(s, h);
		}
	}
}

/*
 * 3. if there is at least one network which RTT isn't known yet,
 *    examine the RTT.
*/
static void
search_idle_examine_rtt_of_all_networks(struct search_idle_state *s)
{
	struct search_idle_network *net;
	struct search_idle_host_state *h;
	int rtt_unknown, todo, all_tried;

	/* initialize cursor */
	for (net = staticp->search_idle_network_list; net != NULL;
	    net = net->next)
		net->cursor = net->candidate_list;

	for (;;) {
		do {
			todo = 0;
			rtt_unknown = 0;
			all_tried = 1;
			for (net = staticp->search_idle_network_list;
			    net != NULL; net = net->next) {
				if ((net->flags &
				    (NET_FLAG_SCHEDULING|NET_FLAG_RTT_AVAIL))
				    != NET_FLAG_SCHEDULING)
					continue; /* RTT is already known */
				rtt_unknown = 1;
				if (net->cursor == NULL)
					continue; /* all hosts were tried */
				all_tried = 0;
				if (net->ongoing >= PER_NET_CONCURRENCY)
					continue;
				h = net->cursor;
				/* XXX report this error? */
				(void)search_idle_try_host(s, h, 0);
				net->cursor = h->next_in_the_net;
				if (net->ongoing < PER_NET_CONCURRENCY &&
				    net->cursor != NULL)
					todo = 1;
			}
		} while (todo);
		if (!rtt_unknown || all_tried)
			break;

		(void)gfarm_eventqueue_turn(s->q, NULL); /* XXX - report rv? */
	}
	(void)gfarm_eventqueue_loop(s->q, NULL); /* XXX - report rv? */
}

/*
 * 4. search networks by RTT order.
 */
static gfarm_error_t
search_idle_by_rtt_order(struct search_idle_state *s)
{
	struct search_idle_network *net, **netarray;
	int nnets, rtt_threshold, i, j;

	nnets = 0;
	for (net = staticp->search_idle_network_list; net != NULL;
	    net = net->next) {
		if (search_idle_network_is_local(net)) /* already searched */
			continue;
		if ((net->flags &
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING)) ==
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING))
			nnets++;
	}
	if (nnets <= 0)
		return (GFARM_ERR_NO_ERROR);

	GFARM_MALLOC_ARRAY(netarray, nnets);
	if (netarray == NULL) {
		gflog_debug(GFARM_MSG_1001447,
		    "search_idle_by_rtt_order: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	i = 0;
	for (net = staticp->search_idle_network_list; net != NULL;
	    net = net->next) {
		if (search_idle_network_is_local(net)) /* already searched */
			continue;
		if ((net->flags &
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING)) ==
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING))
			netarray[i++] = net;
	}
	/* sort hosts in order of RTT */
	qsort(netarray, nnets, sizeof(*netarray), net_rtt_compare);

	for (i = 0; i < nnets; i = j) {
		/*
		 * search network which RTT is less than
		 * min(current * RTT_THRESH_RATIO, current + RTT_THRESH_DIFF)
		 */
		rtt_threshold = netarray[i]->rtt_usec * RTT_THRESH_RATIO
		    / GFARM_F2LL_SCALE;
		if (rtt_threshold > netarray[i]->rtt_usec + RTT_THRESH_DIFF)
			rtt_threshold =
			    netarray[i]->rtt_usec + RTT_THRESH_DIFF;
		for (j = i + 1;
		    j < nnets && netarray[j]->rtt_usec < rtt_threshold; j++)
			;
		/* search netarray[i ... (j-1)] */
		search_idle_in_networks(s, j - i, &netarray[i]);

		if (search_idle_is_satisfied(s))
			break;
	}
	free(netarray);
	return (GFARM_ERR_NO_ERROR);
}

/* `*nohostsp' is INPUT/OUTPUT parameter, and `*ohosts' is OUTPUT parameter */
static gfarm_error_t
search_idle(struct gfm_connection *gfm_server,
	int *nohostsp, char **ohosts, int *oports, int write_mode)
{
	gfarm_error_t e;
	struct search_idle_state s;
	struct search_idle_host_state *h, **results;
	int i, n;
	gfarm_timerval_t t1, t2, t3, t4;

	GFARM_KERNEL_UNUSE2(t1, t2); GFARM_KERNEL_UNUSE2(t3, t4);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);

	gfs_profile(gfarm_gettimerval(&t1));
	e = search_idle_init_state(&s, gfm_server, *nohostsp,
	    staticp->default_search_method, write_mode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001448,
		    "search_idle: search_idle_init_state: %s",
		    gfarm_error_string(e));
		return (e);
	}
	gfs_profile(gfarm_gettimerval(&t2));

	/*
	 * 1. search local hosts
	 */
	if (staticp->search_idle_local_host != NULL) {
		gflog_debug(GFARM_MSG_1004314, "== search local host ==");
		search_idle_in_networks(&s,
			staticp->search_idle_local_host_count,
			staticp->search_idle_local_host);
	}
	/*
	 * 2. search hosts on the local network
	 *   (i.e. the same network with this client host).
	 */
	if (((write_mode && !gfarm_schedule_write_local_priority())
	     || !search_idle_is_satisfied(&s))
	    && staticp->search_idle_local_net != NULL) {
		gflog_debug(GFARM_MSG_1004315, "== search local network ==");
		search_idle_in_networks(&s, 1, &staticp->search_idle_local_net);
	}
	gfs_profile(gfarm_gettimerval(&t3));

	if (!search_idle_is_satisfied(&s)) {
		gflog_debug(GFARM_MSG_1004316, "== search all networks ==");
		search_idle_examine_rtt_of_all_networks(&s);
		e = search_idle_by_rtt_order(&s);
	}
	gfs_profile(gfarm_gettimerval(&t4));

	gfs_profile(
		gflog_debug(GFARM_MSG_1000172,
		    "(search_idle) init %f, local %f, all %f",
			   gfarm_timerval_sub(&t2, &t1),
			   gfarm_timerval_sub(&t3, &t2),
			   gfarm_timerval_sub(&t4, &t3)));

	gfarm_eventqueue_free(s.q);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001449,
		    "search_idle: search_idle_by_rtt_order: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (s.usable_hosts_number == 0) {
		gflog_debug(GFARM_MSG_1001450,
		    "search_idle: no filesystem node");
		return (GFARM_ERR_NO_FILESYSTEM_NODE);
	}

	assert(s.available_hosts_number >= s.usable_hosts_number);
	GFARM_MALLOC_ARRAY(results, s.available_hosts_number);
	if (results == NULL) {
		gflog_debug(GFARM_MSG_1001451, "search_idle: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	i = 0;
	for (h = staticp->search_idle_candidate_list; h != NULL; h = h->next)
		if ((h->flags & HOST_STATE_FLAG_AVAILABLE) != 0)
			results[i++] = h;
	assert(i == s.available_hosts_number);
	if (write_mode) {
		/* sort in order of free disk space */
		qsort(results, s.available_hosts_number, sizeof(*results),
		    davail_compare);
		if (s.usable_hosts_number < s.desired_number) {
			n = s.usable_hosts_number;
		} else {
			assert(s.idle_hosts_number <= s.usable_hosts_number &&
			    s.semi_idle_hosts_number <= s.usable_hosts_number);
			/* sort in order of load average */
			qsort(results, s.usable_hosts_number, sizeof(*results),
			    loadavg_compare);
			if (s.idle_hosts_number < s.desired_number &&
			    s.semi_idle_hosts_number < s.enough_number) {
				n = s.desired_number;
			} else {
				if (s.idle_hosts_number >= s.desired_number)
					n = s.idle_hosts_number;
				else
					n = s.semi_idle_hosts_number;
				/* sort in order of free disk space */
				qsort(results, n, sizeof(*results),
				    davail_compare);
			}
		}
	} else {
		/* sort in order of load average */
		qsort(results, s.available_hosts_number, sizeof(*results),
		    loadavg_compare);
		n = s.available_hosts_number;
	}

	for (i = 0; i < n && i < s.desired_number; i++) {
		ohosts[i] = results[i]->return_value;
		oports[i] = results[i]->port;
		gflog_debug(GFARM_MSG_1004317, "[%d] %s:%d",
		    i, ohosts[i], oports[i]);
	}
	*nohostsp = i;
	free(results);

	return (GFARM_ERR_NO_ERROR);
}

static void
hosts_expand_cyclic(int nsrchosts, char **srchosts, int *srcports,
	int ndsthosts, char **dsthosts, int *dstports)
{
	int i, j;

	for (i = 0, j = 0; i < ndsthosts; i++, j++) {
		if (j >= nsrchosts)
			j = 0;
		dsthosts[i] = srchosts[j];
		dstports[i] = dstports[j];
	}
}

static gfarm_error_t
search_idle_cyclic(struct gfm_connection *gfm_server,
	int nohosts, char **ohosts, int *oports, int write_mode)
{
	gfarm_error_t e;
	int nfound = nohosts;

	e = search_idle(gfm_server, &nfound, ohosts, oports, write_mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (nfound == 0)
		return (GFARM_ERR_NO_FILESYSTEM_NODE);
	if (nohosts > nfound)
		hosts_expand_cyclic(nfound, ohosts, oports,
		    nohosts - nfound, &ohosts[nfound], &oports[nfound]);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * If acyclic, *nohostsp is an input/output parameter,
 * otherwise *nohostsp is an input parameter.
 */
static gfarm_error_t
select_hosts(struct gfm_connection *gfm_server,
	int acyclic, int write_mode, char *write_target_domain,
	int ninfos, struct gfarm_host_sched_info *infos,
	int *nohostsp, char **ohosts, int *oports)
{
	gfarm_error_t e;
	int i;
	gfarm_timerval_t t1, t2, t3, t4;

	GFARM_KERNEL_UNUSE2(t1, t2); GFARM_KERNEL_UNUSE2(t3, t4);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t4);

	/*
	 * if !gfm_client_is_connection_valid(gfm_server)
	 *	gfm_client_username() shouldn't be called,
	 */
	if (!gfm_client_is_connection_valid(gfm_server)) {
		gflog_debug(GFARM_MSG_1001452,
		    "select_hosts: connection cache was purged");
		return (GFARM_ERR_STALE_FILE_HANDLE);
	}

	gfs_profile(gfarm_gettimerval(&t1));
	e = search_idle_candidate_list_init(gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001453,
		    "gfarm_schedule_select_host: "
		    "search_idle_candidate_list_init: %s",
		    gfarm_error_string(e));
		return (e);
	}
	/* set target domain */
	if (write_mode && write_target_domain != NULL)
		search_idle_set_domain_filter(write_target_domain);
	else
		search_idle_set_domain_filter(NULL);
	gfs_profile(gfarm_gettimerval(&t2));
retry_list_add:
	for (i = 0; i < ninfos; i++) {
		e = search_idle_candidate_list_add(gfm_server, &infos[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001454,
			    "gfarm_schedule_select_host: "
			    "search_idle_candidate_list_add: %s",
			    gfarm_error_string(e));
			return (e);
		}
	}
	if (write_mode && write_target_domain != NULL &&
	    staticp->search_idle_candidate_host_number == 0) {
		write_target_domain = NULL;
		search_idle_set_domain_filter(NULL);
		goto retry_list_add;
	}
	gfs_profile(gfarm_gettimerval(&t3));
	if (acyclic)
		e = search_idle(gfm_server, nohostsp, ohosts, oports,
		    write_mode);
	else
		e = search_idle_cyclic(gfm_server, *nohostsp, ohosts, oports,
		    write_mode);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001455,
		    "gfarm_schedule_select_host: search result: %s",
		    gfarm_error_string(e));

	gfs_profile(gfarm_gettimerval(&t4));
	gfs_profile(
		gflog_debug(GFARM_MSG_1002417,
		    "(select_hosts) init %f, add %f, schedule %f",
			   gfarm_timerval_sub(&t2, &t1),
			   gfarm_timerval_sub(&t3, &t2),
			   gfarm_timerval_sub(&t4, &t3)));

	return (e);
}

gfarm_error_t
gfarm_schedule_select_host(struct gfm_connection *gfm_server,
	int nhosts, struct gfarm_host_sched_info *infos,
	int write_mode, char **hostp, int *portp)
{
	gfarm_error_t e;
	char *host, *target_domain = gfarm_schedule_write_target_domain();
	int port, n = 1;

	SCHED_MUTEX_LOCK(staticp)
	e = select_hosts(gfm_server, 1, write_mode, target_domain,
		nhosts, infos, &n, &host, &port);
	if (target_domain != NULL && e == GFARM_ERR_NO_FILESYSTEM_NODE)
		e = select_hosts(gfm_server, 1, write_mode, NULL,
			nhosts, infos, &n, &host, &port);
	SCHED_MUTEX_UNLOCK(staticp)
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (n == 0) { /* although this shouldn't happen */
		gflog_debug(GFARM_MSG_1001456,
		    "gfarm_schedule_select_host: no filesystem node");
		return (GFARM_ERR_NO_FILESYSTEM_NODE);
	}
	host = strdup(host);
	if (host == NULL) {
		gflog_debug(GFARM_MSG_1001457,
		    "gfarm_schedule_select_host: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	*hostp = host;
	*portp = port;
	return (GFARM_ERR_NO_ERROR);
}

struct select_hosts_by_path_info {
	const char *path;
	int acyclic, write_mode;
	char *target_domain;
	int ninfos;
	struct gfarm_host_sched_info *infos;
	int *nohostsp;
	char **ohosts;
	int *oports;
};

static gfarm_error_t
select_hosts_by_path_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	gfarm_error_t e;
	struct select_hosts_by_path_info *si = closure;

	if ((e = gfm_client_connection_and_process_acquire_by_path(si->path,
	    gfm_serverp)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003991,
		    "gfm_client_connection_and_process_acquire_by_path "
		    "path=%s: %s",
		    si->path, gfarm_error_string(e));
		return (e);
	}
	gfm_client_connection_lock(*gfm_serverp);
	if ((e = select_hosts(*gfm_serverp, si->acyclic, si->write_mode,
	    si->target_domain, si->ninfos, si->infos, si->nohostsp, si->ohosts,
	    si->oports))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003992,
		    "select_hosts: %s",
		    gfarm_error_string(e));
	if (si->target_domain != NULL && e == GFARM_ERR_NO_FILESYSTEM_NODE)
		e = select_hosts(*gfm_serverp, si->acyclic, si->write_mode,
			NULL, si->ninfos, si->infos, si->nohostsp, si->ohosts,
			si->oports);
	gfm_client_connection_unlock(*gfm_serverp);
	return (e);
}

static gfarm_error_t
select_hosts_by_path_post_failover(struct gfm_connection *gfm_server,
	void *closure)
{
	if (gfm_server)
		gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

static void
select_hosts_by_path_exit(struct gfm_connection *gfm_server, gfarm_error_t e,
	void *closure)
{
	(void)select_hosts_by_path_post_failover(gfm_server, closure);
}

static gfarm_error_t
select_hosts_by_path(const char *path,
	int acyclic, int write_mode,
	int ninfos, struct gfarm_host_sched_info *infos,
	int *nohostsp, char **ohosts, int *oports)
{
	struct select_hosts_by_path_info si = {
		path, acyclic, write_mode,
		gfarm_schedule_write_target_domain(), ninfos,
		infos, nohostsp, ohosts, oports
	};

	return (gfm_client_rpc_with_failover(select_hosts_by_path_rpc,
	    select_hosts_by_path_post_failover, select_hosts_by_path_exit,
	    NULL, &si));
}

/*
 * Select 'nohosts' hosts among 'nihosts' ihinfos in the order of
 * load average, and return to 'ohosts' and 'oports'.
 * When enough number of hosts are not available, the available hosts
 * will be listed in the cyclic manner.
 * NOTE:
 *	each entry of ohosts[] is not strdup'ed but points ihinfos[], thus,
 *	- DO NOT call free() against each entry of ohosts[].
 *	- DO NOT access ohosts[] after gfarm_host_sched_info_free(ihinfos).
 */
gfarm_error_t
gfarm_schedule_hosts(const char *path,
	int nihosts, struct gfarm_host_sched_info *ihinfos,
	int nohosts, char **ohosts, int *oports)
{
	return (select_hosts_by_path(path, 0, 0, nihosts, ihinfos,
	    &nohosts, ohosts, oports));
}

gfarm_error_t
gfarm_schedule_hosts_to_write(const char *path,
	int nihosts, struct gfarm_host_sched_info *ihinfos,
	int nohosts, char **ohosts, int *oports)
{
	return (select_hosts_by_path(path, 0, 1, nihosts, ihinfos,
	    &nohosts, ohosts, oports));
}

/*
 * Similar to 'gfarm_schedule_hosts()' except for the fact that
 * the available hosts will be listed only once even if enough number of
 * hosts are not available.
 */
gfarm_error_t
gfarm_schedule_hosts_acyclic(const char *path,
	int nihosts, struct gfarm_host_sched_info *ihinfos,
	int *nohostsp, char **ohosts, int *oports)
{
	return (select_hosts_by_path(path, 1, 0, nihosts, ihinfos,
	    nohostsp, ohosts, oports));
}

gfarm_error_t
gfarm_schedule_hosts_acyclic_to_write(const char *path,
	int nihosts, struct gfarm_host_sched_info *ihinfos,
	int *nohostsp, char **ohosts, int *oports)
{
	return (select_hosts_by_path(path, 1, 1, nihosts, ihinfos,
	    nohostsp, ohosts, oports));
}

/* returns scheduled_age */
gfarm_uint64_t
gfarm_schedule_host_used(const char *hostname, int port, const char *username)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	if (staticp->search_idle_hosts_state == NULL)
		return (HOST_STATE_SCHEDULED_AGE_NOT_FOUND);

	e = gfp_conn_hash_lookup(&staticp->search_idle_hosts_state,
	    HOSTS_HASHTAB_SIZE, hostname, port, username, &entry);
	if (e != GFARM_ERR_NO_ERROR)
		return (HOST_STATE_SCHEDULED_AGE_NOT_FOUND);

	h = gfarm_hash_entry_data(entry);
	h->scheduled++;
	return (h->scheduled_age);
}

void
gfarm_schedule_host_unused(const char *hostname, int port, const char *username,
	gfarm_uint64_t scheduled_age)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	static const char diag[] = "search_host_unused";

	if (scheduled_age == HOST_STATE_SCHEDULED_AGE_NOT_FOUND)
		return;
	if (staticp->search_idle_hosts_state == NULL) {
		gflog_notice(GFARM_MSG_1004318, "%s: internal error", diag);
		return;
	}
	e = gfp_conn_hash_lookup(&staticp->search_idle_hosts_state,
	    HOSTS_HASHTAB_SIZE, hostname, port, username, &entry);
	if (e != GFARM_ERR_NO_ERROR)
		return;

	h = gfarm_hash_entry_data(entry);
	if (h->scheduled_age == scheduled_age)
		--h->scheduled;
}

/* this function shouldn't belong to this file, but... */
int
gfm_host_is_in_local_net(struct gfm_connection *gfm_server, const char *host)
{
	gfarm_error_t e;
	struct sockaddr addr;

	/* XXX it's desirable to use struct sockaddr in the scheduling cache */

	/* XXX FIXME this port number (0) is dummy */
	e = gfm_host_address_get(gfm_server, host, 0, &addr, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		return (0);

	if (staticp->search_idle_local_net == NULL &&
	    search_idle_network_list_init(gfm_server) != GFARM_ERR_NO_ERROR)
		return (0);

	return (gfarm_hostspec_match(staticp->search_idle_local_net->network,
	    NULL, &addr));
}

/*
 * the following dump functions are for debuging purpose.
 */
void
gfarm_schedule_network_cache_dump(void)
{
	struct search_idle_network *n;
	char addr[GFARM_HOSTSPEC_STRLEN];

	for (n = staticp->search_idle_network_list; n != NULL; n = n->next) {
		/*
		 * the reason why we don't use inet_ntoa() here is
		 * because inet_ntoa() uses static work area, so it cannot be
		 * called for two instances (ip_min and ip_max) at once.
		 */
		gfarm_hostspec_to_string(n->network, addr, sizeof addr);
		if (n->flags & NET_FLAG_RTT_AVAIL)
			gflog_info(GFARM_MSG_1000174,
			    "%s: RTT %d usec", addr, n->rtt_usec);
		else
			gflog_info(GFARM_MSG_1000175,
			    "%s: RTT unavailable", addr);
	}
}

void
gfarm_schedule_host_cache_dump(void)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	char hostbuf[80];
	char rttbuf[80];
	char loadbuf[80];
	char diskbuf[80];
	char diskusedbuf[GFARM_INT64STRLEN];
	char disktotalbuf[GFARM_INT64STRLEN];
	struct timeval period;

	if (staticp->search_idle_hosts_state == NULL) {
		gflog_info(GFARM_MSG_1000176, "<empty>");
		return;
	}

	gettimeofday(&period, NULL);
	period.tv_sec -= gfarm_ctxp->schedule_cache_timeout;

	for (gfarm_hash_iterator_begin(staticp->search_idle_hosts_state, &it);
	    !gfarm_hash_iterator_is_end(&it); gfarm_hash_iterator_next(&it)) {
		entry = gfarm_hash_iterator_access(&it);
		h = gfarm_hash_entry_data(entry);

		if (h->flags & HOST_STATE_FLAG_ADDR_AVAIL) {
			unsigned char *ip = (unsigned char *)
			    &((struct sockaddr_in *)&h->addr)->sin_addr.s_addr;
			snprintf(hostbuf, sizeof hostbuf,
			    "%s(%d.%d.%d.%d):%d %s",
			    gfp_conn_hash_hostname(entry),
			    ip[0], ip[1], ip[2], ip[3],
			    h->port, gfp_conn_hash_username(entry));
		} else {
			snprintf(hostbuf, sizeof hostbuf, "%s:%d %s",
			    gfp_conn_hash_hostname(entry),
			    h->port, gfp_conn_hash_username(entry));
		}

		if ((h->flags & HOST_STATE_FLAG_RTT_TRIED) == 0) {
			snprintf(rttbuf, sizeof rttbuf, "RTT:no-try");
		} else if ((h->flags & HOST_STATE_FLAG_RTT_AVAIL) == 0) {
			snprintf(rttbuf, sizeof rttbuf, "RTT(%d.%d%s):unavail",
			    (int)h->rtt_cache_time.tv_sec,
			    (int)h->rtt_cache_time.tv_usec,
			    gfarm_timeval_cmp(&h->rtt_cache_time, &period) < 0
			    ? "*" : "");
		} else {
			snprintf(rttbuf, sizeof rttbuf, "RTT(%d.%d%s):%du",
			    (int)h->rtt_cache_time.tv_sec,
			    (int)h->rtt_cache_time.tv_usec,
			    gfarm_timeval_cmp(&h->rtt_cache_time, &period) < 0
			    ? "*" : "",
			    h->rtt_usec);
		}

		if ((h->flags & (HOST_STATE_FLAG_RTT_AVAIL|
		    HOST_STATE_FLAG_STATFS_AVAIL)) == 0) {
			snprintf(loadbuf, sizeof loadbuf, "load:unavail");
		} else {
			long long val = h->loadavg * 1000LL
			    / GFM_PROTO_LOADAVG_FSCALE;

			snprintf(loadbuf, sizeof loadbuf,
			    "load(%d.%d%s):%lld.%02lld",
			    (int)h->loadavg_cache_time.tv_sec,
			    (int)h->loadavg_cache_time.tv_usec,
			    gfarm_timeval_cmp(&h->loadavg_cache_time, &period)
			    < 0 ? "*" : "",
			    val / 1000,
			    (val % 1000 + 5) / 10); /* round off */
		}

		if ((h->flags & HOST_STATE_FLAG_STATFS_AVAIL) == 0) {
			snprintf(diskbuf, sizeof diskbuf, "disk:unavail");
		} else {
			gfarm_humanize_number(diskusedbuf, sizeof diskusedbuf,
			    h->diskused, GFARM_HUMANIZE_BINARY);
			gfarm_humanize_number(disktotalbuf, sizeof disktotalbuf,
			    h->diskused + h->diskavail, GFARM_HUMANIZE_BINARY);
			snprintf(diskbuf, sizeof diskbuf,
			    "disk(%d.%d%s):%sB/%sB",
			    (int)h->statfs_cache_time.tv_sec,
			    (int)h->statfs_cache_time.tv_usec,
			    gfarm_timeval_cmp(&h->statfs_cache_time, &period)
			    < 0 ? "*" : "",
			    diskusedbuf, disktotalbuf);
		}

		gflog_info(GFARM_MSG_1000177,
		    "%s %s %s %s", hostbuf, rttbuf, loadbuf, diskbuf);
	}
}

void
gfarm_schedule_cache_dump(void)
{
	gflog_info(GFARM_MSG_1000178, "== network ==");
	gfarm_schedule_network_cache_dump();
	gflog_info(GFARM_MSG_1000179, "== host ==");
	gfarm_schedule_host_cache_dump();
}


#if 0 /* not yet in gfarm v2 */

/*
 * If acyclic, *nohostsp is an input/output parameter,
 * otherwise *nohostsp is an input parameter.
 */
static char *
schedule_search_idle_common(int acyclic, int write_mode,
	int *nohostsp, char **ohosts)
{
	char *e;
	int i, nihosts;
	struct gfarm_host_info *ihosts;

	e = gfarm_host_info_get_all(&nihosts, &ihosts);
	if (e != NULL)
		return (e);
	for (i = 0; i < nihosts; i++) {
		e = search_idle_candidate_list_add_host_info(&ihosts[i]);
		if (e != NULL)
			goto free_ihosts;
	}
	e = acyclic ?
	    search_idle(nohostsp, ohosts, write_mode) :
	    search_idle_cyclic(*nohostsp, ohosts, write_mode);
	if (e != NULL)
		goto free_ihosts;
	e = gfarm_fixedstrings_dup(*nohostsp, ohosts, ohosts);

free_ihosts:
	gfarm_host_info_free_all(nihosts, ihosts);
	return (e);
}

static char *
schedule_search_idle(int acyclic, int write_mode,
	char *program, const char *domainname, int *nohostsp, char **ohosts)
{
	char *e = search_idle_candidate_list_init();
	int program_filter_alloced = 0;

	if (e != NULL)
		return (e);
	if (program != NULL && gfarm_is_url(program)) {
		e = search_idle_set_program_filter(program);
		if (e != NULL)
			return (e);
		program_filter_alloced = 1;
	}
	if (domainname != NULL)
		search_idle_set_domain_filter(domainname);
	e = schedule_search_idle_common(acyclic, write_mode, nohostsp, ohosts);
	if (program_filter_alloced)
		search_idle_free_program_filter();
	search_idle_set_domain_filter(NULL);
	return (e);
}

char *
gfarm_schedule_search_idle_by_all(int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 0, NULL, NULL, &nohosts, ohosts));
}

char *
gfarm_schedule_search_idle_by_all_to_write(int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 1, NULL, NULL, &nohosts, ohosts));
}

/*
 * lists host names that contains domainname.
 */
char *
gfarm_schedule_search_idle_by_domainname(const char *domainname,
	int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 0, NULL, domainname,
	    &nohosts, ohosts));
}

char *
gfarm_schedule_search_idle_by_domainname_to_write(const char *domainname,
	int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 1, NULL, domainname,
	    &nohosts, ohosts));
}

char *
gfarm_schedule_search_idle_acyclic_by_domainname(const char *domainname,
	int *nohostsp, char **ohosts)
{
	return (schedule_search_idle(1, 0, NULL, domainname,
	    nohostsp, ohosts));
}

char *
gfarm_schedule_search_idle_acyclic_by_domainname_to_write(
	const char *domainname, int *nohostsp, char **ohosts)
{
	return (schedule_search_idle(1, 1, NULL, domainname,
	    nohostsp, ohosts));
}

char *
gfarm_schedule_search_idle_by_program(char *program,
	int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 0, program, NULL, &nohosts, ohosts));
}

char *
gfarm_schedule_search_idle_by_program_to_write(char *program,
	int nohosts, char **ohosts)
{
	return (schedule_search_idle(0, 1, program, NULL, &nohosts, ohosts));
}

static char *
file_section_host_schedule_common(char *gfarm_file, char *section,
	int priority_to_local, int write_mode,
	char **hostp)
{
	char *e, *self_name = NULL;
	int i, ncopies;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
	    gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);
	if (ncopies == 0)
		return (GFARM_ERR_NO_REPLICA);
	/*
	 * We don't honor gfarm_schedule_write_local_priority()
	 * for the priority_to_local case.
	 * because "write_local_priority" only applies to the case
	 * where the file is newly created.
	 */
	if (priority_to_local &&
	    (write_mode ?
	     gfarm_is_active_fsnode_to_write(0) :
	     gfarm_is_active_fsnode()) &&
	    (e = gfarm_host_get_canonical_self_name(&self_name)) == NULL) {
		for (i = 0; i < ncopies; i++) {
			if (strcasecmp(self_name, copies[i].hostname) == 0) {
				*hostp = self_name;
				goto found_host;
			}
		}
	}
	for (i = 0; i < ncopies; i++) {
		e = search_idle_candidate_list_add_host(copies[i].hostname);
		if (e != NULL)
			goto free_copies;
	}
	e = search_idle_cyclic(1, hostp, write_mode);
	if (e != NULL)
		goto free_copies;
 found_host:
	e = gfarm_fixedstrings_dup(1, hostp, hostp);

 free_copies:
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	return (e);
}

static char *
file_section_host_schedule(char *gfarm_file, char *section, char *program,
	int priority_to_local, int write_mode,
	char **hostp)
{
	char *e;
	int program_filter_alloced = 0;

	assert(!priority_to_local || program == NULL);
	/* otherwise not supported */

	e = search_idle_candidate_list_init();
	if (e != NULL)
		return (e);
	if (program != NULL && gfarm_is_url(program)) {
		e = search_idle_set_program_filter(program);
		if (e != NULL)
			return (e);
		program_filter_alloced = 1;
	}
	e = file_section_host_schedule_common(gfarm_file, section,
	    priority_to_local, write_mode, hostp);
	if (program_filter_alloced)
		search_idle_free_program_filter();
	return (e);
}

char *
gfarm_file_section_host_schedule(char *gfarm_file, char *section, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, NULL, 0, 0,
	    hostp));
}

char *
gfarm_file_section_host_schedule_to_write(
	char *gfarm_file, char *section, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, NULL, 0, 1,
	    hostp));
}

char *
gfarm_file_section_host_schedule_by_program(char *gfarm_file, char *section,
	char *program, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, program, 0, 0,
	    hostp));
}

char *
gfarm_file_section_host_schedule_by_program_to_write(
	char *gfarm_file, char *section,
	char *program, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, program, 0, 1,
	    hostp));
}

char *
gfarm_file_section_host_schedule_with_priority_to_local(
	char *gfarm_file, char *section, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, NULL, 1, 0,
	    hostp));
}

char *
gfarm_file_section_host_schedule_with_priority_to_local_to_write(
	char *gfarm_file, char *section, char **hostp)
{
	return (file_section_host_schedule(gfarm_file, section, NULL, 1, 1,
	    hostp));
}

static char *
url_hosts_schedule_common(const char *gfarm_url,
	int not_require_file_affinity, int write_mode,
	char *option,
	int *nhostsp, char ***hostsp)
{
	char *e, *gfarm_file, **hosts, **residual;
	int i, nfrags, shortage;
	char section[GFARM_INT32STRLEN + 1];

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);
	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	GFARM_MALLOC_ARRAY(hosts, nfrags);
	if (hosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_gfarm_file;
	}
	shortage = 0;
	for (i = 0; i < nfrags; i++) {
		sprintf(section, "%d", i);
		if ((e = search_idle_candidate_list_clear()) != NULL ||
		    (e = file_section_host_schedule_common(
		    gfarm_file, section, 0, write_mode,
		    &hosts[i])) != NULL) {
			if (not_require_file_affinity &&
			    e == GFARM_ERR_NO_HOST) {
				hosts[i] = NULL;
				shortage++;
				continue;
			}
			gfarm_strings_free_deeply(i, hosts);
			goto free_gfarm_file;
		}
	}
	if (shortage > 0) {
		assert(not_require_file_affinity);
		GFARM_MALLOC_ARRAY(residual, shortage);
		if (residual == NULL) {
			gfarm_strings_free_deeply(nfrags, hosts);
			e = GFARM_ERR_NO_MEMORY;
			goto free_gfarm_file;
		}
		e = search_idle_candidate_list_clear();
		if (e == NULL)
			e = schedule_search_idle_common(
			    0, write_mode, &shortage, residual);
		if (e == NULL)
			e = gfarm_fixedstrings_dup(shortage, residual,
			    residual);
		if (e != NULL) {
			free(residual);
			gfarm_strings_free_deeply(nfrags, hosts);
			goto free_gfarm_file;
		}
		for (i = 0; i < nfrags; i++) {
			if (hosts[i] == NULL) {
				hosts[i] = residual[--shortage];
				if (shortage == 0)
					break;
			}
		}
		free(residual);
	}
	*nhostsp = nfrags;
	*hostsp = hosts;
 free_gfarm_file:
	free(gfarm_file);
	return (e);
}

char *
gfarm_url_hosts_schedule(const char *gfarm_url, char *option,
	int *nhostsp, char ***hostsp)
{
	char *e;

	e = search_idle_candidate_list_init();
	if (e != NULL)
		return (e);
	return (url_hosts_schedule_common(gfarm_url, 0, 0, option,
	    nhostsp, hostsp));
}

char *
gfarm_url_hosts_schedule_by_program(
	char *gfarm_url, char *program, char *option,
	int *nhostsp, char ***hostsp)
{
	char *e;

	if (!gfarm_is_url(program))
		return (gfarm_url_hosts_schedule(gfarm_url, option,
		    nhostsp, hostsp));

	e = search_idle_candidate_list_init();
	if (e != NULL)
		return (e);
	e = search_idle_set_program_filter(program);
	if (e != NULL)
		return (e);
	e = url_hosts_schedule_common(gfarm_url, 1, 0, option,
	    nhostsp, hostsp);
	search_idle_free_program_filter();
	return (e);
}

static char *
statfsnode(char *canonical_hostname, int use_cache,
	gfarm_int32_t *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e, *e2;
	struct search_idle_host_state *h;

	e = search_idle_host_state_add_host(canonical_hostname, &h);
	if (e != NULL)
		return (e);

	if (!use_cache ||
	    (h->flags & HOST_STATE_FLAG_STATFS_AVAIL) == 0 ||
	    is_expired(&h->statfs_cache_time, STATFS_EXPIRATION)) {

		if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
			return (GFARM_ERR_UNKNOWN_HOST);

		e = gfs_client_statfs_with_reconnect_addr(
		    canonical_hostname, &h->addr, ".", NULL, &e2,
		    &h->bsize,
		    &h->blocks, &h->bfree, &h->bavail,
		    &h->files, &h->ffree, &h->favail);
		if (e != NULL || e2 != NULL)
			return (e != NULL ? e : e2);
		h->statfs_cache_time = staticp->search_idle_now;
		h->flags |=
		    HOST_STATE_FLAG_AUTH_SUCCEED|HOST_STATE_FLAG_STATFS_AVAIL;
	}
	*bsizep = h->bsize;
	*blocksp = h->blocks;
	*bfreep = h->bfree;
	*bavailp = h->bavail;
	*filesp = h->files;
	*ffreep = h->ffree;
	*favailp = h->favail;
	return (NULL);
}

char *
gfs_statfsnode(char *canonical_hostname,
	int *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e;
	int bsize;

	e = statfsnode(canonical_hostname, 0,
	    &bsize,
	    blocksp, bfreep, bavailp,
	    filesp, ffreep, favailp);
	if (e != NULL)
		return (e);
	*bsizep = bsize;
	return (NULL);
}

char *
gfs_statfsnode_cached(char *canonical_hostname,
	int *bsizep,
	file_offset_t *blocksp, file_offset_t *bfreep, file_offset_t *bavailp,
	file_offset_t *filesp, file_offset_t *ffreep, file_offset_t *favailp)
{
	char *e;
	struct search_idle_host_state *h;

	e = search_idle_host_state_add_host(canonical_hostname, &h);
	if (e != NULL)
		return (e);

	if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
		return (GFARM_ERR_UNKNOWN_HOST);
	if ((h->flags & HOST_STATE_FLAG_STATFS_AVAIL) == 0)
		return (GFARM_ERR_NO_SUCH_OBJECT); /* not cached */

	*bsizep = h->bsize;
	*blocksp = h->blocks;
	*bfreep = h->bfree;
	*bavailp = h->bavail;
	*filesp = h->files;
	*ffreep = h->ffree;
	*favailp = h->favail;
	return (NULL);
}

int
gfarm_is_active_fsnode(void)
{
	return (gfarm_is_active_file_system_node);
}

int
gfarm_is_active_fsnode_to_write(file_offset_t size)
{
	char *e, *self_name;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail;
	file_offset_t files, ffree, favail;

	if (!gfarm_is_active_file_system_node)
		return (0);

	e = gfarm_host_get_canonical_self_name(&self_name);
	if (e != NULL)
		return (0);

	e = statfsnode(self_name, 1, &bsize, &blocks, &bfree, &bavail,
	    &files, &ffree, &favail);
	if (size <= 0)
		size = gfarm_get_minimum_free_disk_space();
	return (e == NULL && bavail * bsize >= size);
}

#endif

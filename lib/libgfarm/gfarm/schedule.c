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
#include <time.h>
#include <gfarm/gfarm.h>

#include "gfutil.h"	/* timeval */
#include "gfevent.h"
#include "hash.h"

#include "host.h" /* gfarm_host_info_address_get() */
#include "auth.h" /* XXX gfarm_random_initialize() */
#include "config.h"
#include "gfs_client.h"
#include "schedule.h"

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
 *		if there isn't enough hosts which have enough free space
 *			select only the hosts which have enough free space
 *		else if there isn't enough idle hosts
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
 *	for each: current network ... a network which RTT <= current*RTT_THRESH
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

char GFARM_ERR_NO_REPLICA[] = "no replica";
char GFARM_ERR_NO_HOST[] = "no filesystem node";

#define CONCURRENCY		10
#define PER_NET_CONCURRENCY	3	/* used when examining RTT */
#define ENOUGH_RATE		4

#define	ADDR_EXPIRATION		gfarm_schedule_cache_timeout	/* seconds */
#define	LOADAVG_EXPIRATION	gfarm_schedule_cache_timeout	/* seconds */
#define	STATFS_EXPIRATION	gfarm_schedule_cache_timeout	/* seconds */

#define RTT_THRESH		4 /* range to treat as similar distance */

/*
 * data structure which represents architectures which can run a program
 */

#define ARCH_SET_HASHTAB_SIZE 31		/* prime number */

#define IS_IN_ARCH_SET(arch, arch_set) \
	(gfarm_hash_lookup(arch_set, arch, strlen(arch) + 1) != NULL)

#define free_arch_set(arch_set)	gfarm_hash_table_free(arch_set)

/* Create a set of architectures that the program is registered for */
static char *
program_arch_set(char *program, struct gfarm_hash_table **arch_setp)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info *sections;
	struct gfarm_hash_table *arch_set;
	int i, nsections, created;

	e = gfarm_url_make_path(program, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
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
	if (e != NULL)
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
	return (NULL);
}

/*
 * data structure which represents information about a host
 */

#define HOSTS_HASHTAB_SIZE	3079	/* prime number */

struct search_idle_network;

struct search_idle_host_state {
	char *architecture;
	int ncpu;
	struct timeval addr_cache_time;	/* always available */
	struct sockaddr addr;		/* if HOST_STATE_FLAG_ADDR_AVAIL */

	struct search_idle_network *net;

	struct timeval rtt_cache_time;	/* if HOST_STATE_FLAG_RTT_TRIED */
	int rtt_usec;			/* if HOST_STATE_FLAG_RTT_AVAIL */
	float loadavg;			/* if HOST_STATE_FLAG_RTT_AVAIL */

	/*if HOST_STATE_FLAG_STATFS_AVAIL*/
	struct timeval statfs_cache_time;
	file_offset_t blocks, bfree, bavail; 
	file_offset_t files, ffree, favail;
	gfarm_int32_t bsize;

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

	/* linked in search_idle_candidate_list */
	struct search_idle_host_state *next;

	/* linked in search_idle_network::candidate_list */
	struct search_idle_host_state *next_in_the_net;

	/* work area */
	char *return_value; /* hostname */
};

static struct gfarm_hash_table *search_idle_hosts_state = NULL;

static int search_idle_candidate_host_number;
static struct search_idle_host_state *search_idle_candidate_list;
static struct search_idle_host_state **search_idle_candidate_last;

static const char *search_idle_domain_filter;
static struct gfarm_hash_table *search_idle_arch_filter;

struct search_idle_network {
	struct search_idle_network *next;
	struct sockaddr min, max;
	int rtt_usec;			/* if NET_FLAG_RTT_AVAIL */
	int flags;

#define NET_FLAG_NETMASK_KNOWN	0x01
#define NET_FLAG_RTT_AVAIL	0x02

/* The followings are working area during scheduling */
#define NET_FLAG_SCHEDULING	0x04

	struct search_idle_host_state *candidate_list;
	struct search_idle_host_state **candidate_last;
	struct search_idle_host_state *cursor;
	int ongoing;
};

static struct search_idle_network *search_idle_network_list = NULL;
static struct search_idle_network *search_idle_local_net = NULL;

static struct timeval search_idle_now;

static int
is_expired(struct timeval *cached_timep, int expiration)
{
	struct timeval expired;

	expired = *cached_timep;
	expired.tv_sec += expiration;
	return (gfarm_timeval_cmp(&search_idle_now, &expired) >= 0);
}

static char *
search_idle_network_list_init(void)
{
	char *e, *self_name;
	struct search_idle_network *net;
	struct sockaddr peer_addr;

	assert(search_idle_network_list == NULL);
	e = gfarm_host_get_canonical_self_name(&self_name);
	if (e != NULL)
		self_name =gfarm_host_get_self_name();
	e = gfarm_host_address_get(self_name, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		return (e);
	GFARM_MALLOC(net);
	if (net == NULL)
		return (GFARM_ERR_NO_MEMORY);
	net->rtt_usec = 0; /* i.e. local network */
#if 0 /* XXX not implemented yet */
	/*
	 * XXX should get the netmark of the local network.
	 * but gfarm_addr_range_get_localnet() isn't implemented yet, and
	 * the algorithm to merge networks in search_idle_network_list_add()
	 * doesn't work for a network which is smaller than IPv4 C class.
	 */
	gfarm_addr_range_get_localnet(&peer_addr, &net->min, &net->max);
	net->flags = NET_FLAG_NETMASK_KNOWN | NET_FLAG_RTT_AVAIL;
#else
	gfarm_addr_range_get(&peer_addr, &net->min, &net->max);
	net->flags = NET_FLAG_RTT_AVAIL;
#endif
	net->candidate_list = NULL;
	net->candidate_last = &net->candidate_list;
	net->next = NULL;
	search_idle_network_list = net;
	search_idle_local_net = net;
	return (NULL);
}

static char *
search_idle_network_list_add(struct sockaddr *addr,
	struct search_idle_network **netp)
{
	struct sockaddr min, max;
	struct search_idle_network *net, *net2, **net2p;
	int widened, widened2;

	gfarm_addr_range_get(addr, &min, &max);

	/* XXX - if there are lots of networks, this is too slow */
	for (net = search_idle_network_list; net != NULL;
	    net = net->next) {
		if (!gfarm_addr_is_same_net(addr, &net->min, &net->max,
		    (net->flags & NET_FLAG_NETMASK_KNOWN) == 0,
		    &widened))
			continue;;
		if (widened != 0) {
			/* widen the net to make it include the addr */
			if (widened < 0)
				net->min = min;
			else
				net->max = max;
			for (net2p = &net->next; (net2 = *net2p) != NULL;
			    net2p = &net2->next) {
				struct gfarm_hash_iterator it;
				struct gfarm_hash_entry *entry;
				struct search_idle_host_state *h;

				if (!gfarm_addr_is_same_net(addr,
				    &net2->min, &net2->max,
				    (net2->flags & NET_FLAG_NETMASK_KNOWN)== 0,
				     &widened2))
					continue;

				/* merge net2 into net */
				assert(widened2 == 0 || widened2 == -widened);
				if (widened < 0)
					net->min = net2->min;
				else
					net->max = net2->max;

				/* choose shorter RTT between net & net2 */
				if ((net2->flags & NET_FLAG_RTT_AVAIL) == 0) {
					/* not need to update net->rtt_usec */
				} else if ((net->flags&NET_FLAG_RTT_AVAIL)==0){
					net->flags |= NET_FLAG_RTT_AVAIL;
					net->rtt_usec = net2->rtt_usec;
				} else if (net->rtt_usec > net2->rtt_usec) {
					net->rtt_usec = net2->rtt_usec;
				}
				net->flags |= net2->flags;

				/* adjust hosts_state appropriately */
				for (gfarm_hash_iterator_begin(
				    search_idle_hosts_state, &it);
				    !gfarm_hash_iterator_is_end(&it);
				    gfarm_hash_iterator_next(&it)) {
					entry =
					    gfarm_hash_iterator_access(&it);
					h = gfarm_hash_entry_data(entry);
					if (h->net == net2)
						h->net = net;
				}
				*net->candidate_last = net2->candidate_list;
				net->candidate_last = net2->candidate_last;

				if (search_idle_local_net == net2)
					search_idle_local_net = net;

				*net2p = net2->next;
				free(net2);
				break;
			}
		}
		*netp = net;
		return (NULL);
	}
	/* first host in the network */
	GFARM_MALLOC(net); 
	if (net == NULL)
		return (NULL);
	net->min = min;
	net->max = max;
	net->flags = 0;
	net->candidate_list = NULL;
	net->candidate_last = &net->candidate_list;
	net->ongoing = 0;
	net->next = search_idle_network_list;
	search_idle_network_list = net;
	*netp = net;
	return (NULL);
}

static char *gfarm_schedule_host_cache_clear_auth(const char *);

static char *
search_idle_host_state_initialize(void)
{
	search_idle_hosts_state =
	    gfarm_hash_table_alloc(HOSTS_HASHTAB_SIZE,
	    gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (search_idle_hosts_state == NULL)
		return (GFARM_ERR_NO_MEMORY);

	search_idle_network_list_init(); /* ignore any error here */

	/* when a connection error happens, make the host unavailable. */
	gfs_client_add_hook_for_connection_error(
		gfarm_schedule_host_cache_clear_auth);

	return (NULL);
}

static char *
search_idle_host_state_add_host_or_host_info(
	const char *hostname, struct gfarm_host_info *host_info,
	struct search_idle_host_state **hp)
{
	char *e;
	int created;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;

	if (search_idle_hosts_state == NULL) {
		e = search_idle_host_state_initialize();
		if (e != NULL)
			return (e);
	}

	assert((hostname == NULL) != (host_info == NULL));

	if (hostname == NULL)
		hostname = host_info->hostname;

	entry = gfarm_hash_enter(search_idle_hosts_state,
	    hostname, strlen(hostname) + 1, sizeof(*h), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	h = gfarm_hash_entry_data(entry);
	if (created || (h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0 ||
	    is_expired(&h->addr_cache_time, ADDR_EXPIRATION)) {
		if (created) {
			struct gfarm_host_info hi, *hip = host_info;

			if (hip == NULL) {
				e = gfarm_host_info_get(hostname, &hi);
				if (e == NULL)
					hip = &hi;
			}
			if (hip == NULL) {
				/* allow non-spool_host for now */
				h->ncpu = 1; /* XXX */
				h->architecture = NULL;
			} else {
				h->ncpu = hip->ncpu;
				h->architecture =
				    strdup(hip->architecture);
				if (hip == &hi)
					gfarm_host_info_free(&hi);
				if (h->architecture == NULL) {
					gfarm_hash_purge(
					    search_idle_hosts_state,
					    hostname, strlen(hostname) + 1);
					return (GFARM_ERR_NO_MEMORY);
				}
			}
			h->net = NULL;
			h->scheduled = 0;
			h->flags = 0;
		} else if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0) {
			if (!is_expired(&h->addr_cache_time, ADDR_EXPIRATION))
				return (GFARM_ERR_UNKNOWN_HOST);
		} else { /* cope with address change */
			assert(is_expired(&h->addr_cache_time, ADDR_EXPIRATION)
			    );
			h->flags &= ~HOST_STATE_FLAG_ADDR_AVAIL;
			h->net = NULL;
		}
		e = gfarm_host_address_get(hostname, gfarm_spool_server_port,
		    &h->addr, NULL);
		gettimeofday(&h->addr_cache_time, NULL);
		if (e != NULL)
			return (e);
		h->flags |= HOST_STATE_FLAG_ADDR_AVAIL;
		e = search_idle_network_list_add(&h->addr, &h->net);
		if (e != NULL)
			return (e);
	} else if (h->net == NULL) {
		/* search_idle_network_list_add() failed at the last time */
		e = search_idle_network_list_add(&h->addr, &h->net);
		if (e != NULL)
			return (e);
	}

	*hp = h;
	return (NULL);
}

static char *
search_idle_host_state_add_host(const char *hostname,
	struct search_idle_host_state **hp)
{
	return (search_idle_host_state_add_host_or_host_info(hostname, NULL,
	    hp));
}

/* forget that this user was authenticated by the specified host */
static char *
gfarm_schedule_host_cache_clear_auth(const char *hostname)
{
	char *e;
	struct search_idle_host_state *h;

	e = search_idle_host_state_add_host(hostname, &h);
	if (e != NULL)
		return (e);
	h->flags &= ~HOST_STATE_FLAG_AUTH_SUCCEED;
	return (NULL);
}

static char *
search_idle_candidate_list_reset(int host_flags)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct search_idle_host_state *h;
	struct search_idle_network *net;

	if (search_idle_hosts_state == NULL) {
		char *e = search_idle_host_state_initialize();
		if (e != NULL)
			return (e);
	}

	search_idle_candidate_host_number = 0;
	search_idle_candidate_list = NULL;
	search_idle_candidate_last = &search_idle_candidate_list;
	host_flags |=
	    HOST_STATE_FLAG_SCHEDULING|
	    HOST_STATE_FLAG_AVAILABLE|
	    HOST_STATE_FLAG_CACHE_WAS_USED;
	for (gfarm_hash_iterator_begin(search_idle_hosts_state, &it);
	    !gfarm_hash_iterator_is_end(&it); gfarm_hash_iterator_next(&it)) {
		entry = gfarm_hash_iterator_access(&it);
		h = gfarm_hash_entry_data(entry);
		h->flags &= ~host_flags;
	}

	for (net = search_idle_network_list; net != NULL; net = net->next) {
		net->flags &= ~NET_FLAG_SCHEDULING;
		net->ongoing = 0;
		net->candidate_list = NULL;
		net->candidate_last = &net->candidate_list;
	}
	return (NULL);
}

static char *
search_idle_candidate_list_init(void)
{
	search_idle_arch_filter = NULL;
	search_idle_domain_filter = NULL;

	gettimeofday(&search_idle_now, NULL);

	return (search_idle_candidate_list_reset(HOST_STATE_FLAG_JUST_CACHED));
}

/*
 * similar to search_idle_candidate_list_init(),
 * but this does not clear HOST_STATE_FLAG_JUST_CACHED.
 * also, does not reset `search_idle_arch_filter', `search_idle_domain_filter'
 * and `search_idle_now'.
 */
static char *
search_idle_candidate_list_clear(void)
{
	return (search_idle_candidate_list_reset(0));
}

#define search_idle_set_domain_filter(domain)	\
	(search_idle_domain_filter = (domain))

static char *
search_idle_set_program_filter(char *program)
{
	return (program_arch_set(program, &search_idle_arch_filter));
}

static void
search_idle_free_program_filter(void)
{
	free_arch_set(search_idle_arch_filter);
}

static char *
search_idle_candidate_list_add_host_or_host_info(
	char *host, struct gfarm_host_info *host_info)
{
	char *e, *hostname = host;
	struct search_idle_host_state *h;

	assert((hostname == NULL) != (host_info == NULL));

	if (hostname == NULL)
		hostname = host_info->hostname;
	if (search_idle_domain_filter != NULL &&
	    !gfarm_host_is_in_domain(hostname, search_idle_domain_filter))
		return (NULL); /* ignore this host */
	if (host_info != NULL && search_idle_arch_filter != NULL &&
	    !IS_IN_ARCH_SET(host_info->architecture, search_idle_arch_filter))
		return (NULL); /* ignore this host, hostname == NULL case */

	e = search_idle_host_state_add_host_or_host_info(host, host_info, &h);
	if (e != NULL) {
		if (e == GFARM_ERR_UNKNOWN_HOST) /* just ignore this host */
			return (NULL);
		return (e);
	}
	if ((h->flags & HOST_STATE_FLAG_SCHEDULING) != 0) {
		/* same host is specified twice or more */
		return (NULL);
	}

	if (host_info == NULL && search_idle_arch_filter != NULL &&
	    (h->architecture == NULL /* allow non-spool_host for now */ ||
	     !IS_IN_ARCH_SET(h->architecture, search_idle_arch_filter)))
		return (NULL); /* ignore this host, hostname != NULL case */

	h->flags |= HOST_STATE_FLAG_SCHEDULING;
	h->net->flags |= NET_FLAG_SCHEDULING;

	/* link to search_idle_candidate_list */
	h->next = NULL;
	search_idle_candidate_host_number++;
	*search_idle_candidate_last = h;
	search_idle_candidate_last = &h->next;

	/* link to h->net->candidate_list */
	h->next_in_the_net = NULL;
	*h->net->candidate_last = h;
	h->net->candidate_last = &h->next_in_the_net;

	/*
	 * return hostname parameter, instead of cached hostname, as results.
	 * this is needed for gfarm_schedule_search_idle_hosts()
	 * and gfarm_schedule_search_idle_acyclic_hosts() which return
	 * input hostnames instead of newly allocated onews.
	 */
	h->return_value = hostname;
	return (NULL);
}

static char *
search_idle_candidate_list_add_host_info(struct gfarm_host_info *host_info)
{
	return (search_idle_candidate_list_add_host_or_host_info(
	    NULL, host_info));
}

static char *
search_idle_candidate_list_add_host(char *hostname)
{
	return (search_idle_candidate_list_add_host_or_host_info(
	    hostname, NULL));
}

/* whether need to see authentication, disk free space or not? */

enum gfarm_schedule_search_mode {
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG,
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH,
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH_AND_DISKAVAIL
};

static enum gfarm_schedule_search_mode default_search_method =
	GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH;

void
gfarm_schedule_search_mode_use_loadavg(void)
{
	default_search_method = GFARM_SCHEDULE_SEARCH_BY_LOADAVG;
}

#define IDLE_LOAD_AVERAGE		0.1F
#define SEMI_IDLE_LOAD_AVERAGE		0.5F
#define VIRTUAL_LOAD_FOR_SCHEDULED_HOST	0.3F

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
};

static char *
search_idle_init_state(struct search_idle_state *s, int desired_hosts,
	enum gfarm_schedule_search_mode mode, int write_mode)
{
	s->desired_number = desired_hosts;
	s->enough_number = desired_hosts * ENOUGH_RATE;
	s->mode = mode;
	s->write_mode = write_mode;
	if (write_mode)
		s->mode =
		    GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH_AND_DISKAVAIL;
	/*
	 * If we don't check enough_number or desired_number here,
	 * the behavior of search_idle() becomes undeterministic.
	 * i.e. If there is a candidate, search_idle() returns NULL,
	 * otherwise GFARM_ERR_NO_HOST.
	 */
	if (s->enough_number == 0 || s->desired_number == 0 ||
	    search_idle_candidate_list == NULL)
		return (GFARM_ERR_NO_HOST);

	s->q = gfarm_eventqueue_alloc();
	if (s->q == NULL)
		return (GFARM_ERR_NO_MEMORY);
	s->available_hosts_number = s->usable_hosts_number =
	    s->idle_hosts_number = s->semi_idle_hosts_number = 0;
	s->concurrency = 0;
	return (NULL);
}

static void
search_idle_count(struct search_idle_state *s,
	struct search_idle_host_state *h,
	int *usable_numberp, int *idle_numberp, int *semi_idle_numberp)
{
	float loadavg = h->loadavg;
	int ncpu = h->ncpu;
	int ok = 1;

	if (s->write_mode &&
	    (h->flags & HOST_STATE_FLAG_STATFS_AVAIL) != 0 &&
	    h->bavail * h->bsize < gfarm_get_minimum_free_disk_space())
		ok = 0; /* not enough free space */
	if (ok)
		(*usable_numberp)++;

	if (ncpu <= 0) /* sanity */
		ncpu = 1;
	loadavg += h->scheduled * VIRTUAL_LOAD_FOR_SCHEDULED_HOST;
	if (ok && loadavg / ncpu <= IDLE_LOAD_AVERAGE)
		(*idle_numberp)++;

	/*
	 * We don't use (loadavg / h->host_info->ncpu) to count
	 * semi_idle_hosts here for now, because it is possible
	 * that there is a process which is consuming 100% of
	 * memory or 100% of I/O bandwidth on the host.
	 */
	if (ok && loadavg <= SEMI_IDLE_LOAD_AVERAGE)
		(*semi_idle_numberp)++;
}

static void
search_idle_record_host(struct search_idle_state *s,
	struct search_idle_host_state *h)
{
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
	    s->available_hosts_number >= search_idle_candidate_host_number);
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

static void
search_idle_statfs_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	char *e;
	struct search_idle_host_state *h = c->h;

	e = gfs_client_statfs_result_multiplexed(c->protocol_state,
	    &h->bsize,
	    &h->blocks, &h->bfree, &h->bavail,
	    &h->files, &h->ffree, &h->favail);
	if (e == NULL) {
		c->h->flags |= HOST_STATE_FLAG_STATFS_AVAIL;
		c->h->statfs_cache_time = c->h->rtt_cache_time;
		search_idle_record(c); /* completed */
	}
	gfs_client_disconnect(c->gfs_server);
	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}

static void
search_idle_connect_callback(void *closure)
{
	char *e;
	struct search_idle_callback_closure *c = closure;
#if 0 /* We always see disk free space */
	struct search_idle_state *s = c->state;
#endif
	struct gfs_client_statfs_state *ss;

	e = gfs_client_connect_result_multiplexed(c->protocol_state,
	    &c->gfs_server);
	if (e == NULL) {
		c->h->flags |= HOST_STATE_FLAG_AUTH_SUCCEED;
#if 0 /* We always see disk free space */
		if (s->mode == GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH) {
			/* completed */
			search_idle_record(c);
			gfs_client_disconnect(c->gfs_server);
		} else {
			assert(s->mode ==
			 GFARM_SCHEDULE_SEARCH_BY_LOADAVG_AND_AUTH_AND_DISKAVAIL
			);
#endif
			e = gfs_client_statfs_request_multiplexed(c->state->q,
			    c->gfs_server, ".",
			    search_idle_statfs_callback, c,
			    &ss);
			if (e == NULL) {
				c->protocol_state = ss;
				return; /* request continues */
			}
			/* failed to request */
			gfs_client_disconnect(c->gfs_server);
#if 0 /* We always see disk free space */
		}
#endif
	}
	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}

static void
search_idle_load_callback(void *closure)
{
	struct search_idle_callback_closure *c = closure;
	struct search_idle_state *s = c->state;
	char *e;
	struct gfs_client_load load;
	struct gfs_client_connect_state *cs;
	struct timeval rtt;

	e = gfs_client_get_load_result_multiplexed(c->protocol_state, &load);
	if (e == NULL) {
		c->h->flags |= HOST_STATE_FLAG_RTT_AVAIL;
		c->h->loadavg = load.loadavg_1min;
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
			/* c->h->flags |= HOST_STATE_FLAG_AUTH_TRIED; */
			e = gfs_client_connect_request_multiplexed(c->state->q,
			    c->h->return_value, &c->h->addr,
			    search_idle_connect_callback, c,
			    &cs);
			if (e == NULL) {
				c->protocol_state = cs;
				return; /* request continues */
			}
			/* failed to connect */
		}
	}
	c->state->concurrency--;
	c->h->net->ongoing--;
	free(c);
}

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
	const float df1 = p->bavail * p->bsize;
	const float df2 = q->bavail * q->bsize;

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
	const float l1 =
	 (p->loadavg + p->scheduled*VIRTUAL_LOAD_FOR_SCHEDULED_HOST) / p->ncpu;
	const float l2 =
	 (q->loadavg + q->scheduled*VIRTUAL_LOAD_FOR_SCHEDULED_HOST) / q->ncpu;

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
	 * by search_idle_host_state_add_host_or_host_info()
	 */
	if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
		return (1); /* IP address isn't resolvable, even */

	return ((h->flags & HOST_STATE_FLAG_RTT_TRIED) != 0 &&
	    !is_expired(&h->rtt_cache_time, LOADAVG_EXPIRATION));

}

static int
search_idle_cache_is_available(struct search_idle_state *s,
	struct search_idle_host_state *h)
{
	/*
	 * the expiration of HOST_STATE_FLAG_ADDR_AVAIL is already coped
	 * by search_idle_host_state_add_host_or_host_info()
	 */
	if ((h->flags & HOST_STATE_FLAG_ADDR_AVAIL) == 0)
		return (0);

	if ((h->flags & HOST_STATE_FLAG_RTT_TRIED) == 0 ||
	    is_expired(&h->rtt_cache_time, LOADAVG_EXPIRATION))
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
	default:
		assert(0);
		return (0);
	}
}

static char *
search_idle_try_host(struct search_idle_state *s,
	struct search_idle_host_state *h, int do_record)
{
	char *e;
	int rv;
	struct search_idle_callback_closure *c;
	struct gfs_client_get_load_state *gls;

	/* already tried? */
	if ((h->flags & HOST_STATE_FLAG_JUST_CACHED) != 0)
		return (NULL);

	/* if we know the availability of this host, don't try */
	if (search_idle_cache_should_be_used(h))
		return (NULL);

	/* We limit concurrency here */
	rv = 0;
	while (s->concurrency >= CONCURRENCY) {
		rv = gfarm_eventqueue_turn(s->q, NULL);
		/* XXX - how to report this error? */
		if (rv != 0 && rv != EAGAIN && rv != EINTR)
			return (gfarm_errno_to_error(rv));
	}

	GFARM_MALLOC(c);
	if (c == NULL)
		return (GFARM_ERR_NO_MEMORY);
	c->state = s;
	c->h = h;
	c->do_record = do_record;
	h->flags |=
	    HOST_STATE_FLAG_JUST_CACHED|
	    HOST_STATE_FLAG_RTT_TRIED;
	gettimeofday(&h->rtt_cache_time, NULL);
	e = gfs_client_get_load_request_multiplexed(s->q, &h->addr,
	    search_idle_load_callback, c,
	    &gls);
	if (e != NULL) {
		free(c);
		return (NULL); /* We won't report the error */
	}
	c->protocol_state = gls;
	s->concurrency++;
	h->net->ongoing++;
	return (NULL);
}

static void
search_idle_in_networks(struct search_idle_state *s,
	int nnets, struct search_idle_network **nets)
{
	int i;
	struct search_idle_host_state *h;
	char *e;
	int rv;

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
			e = search_idle_try_host(s, h, 1);
			if (search_idle_is_satisfied(s))
				goto end_of_trial;
		}
	}
end_of_trial:
	rv = gfarm_eventqueue_loop(s->q, NULL); /* XXX - report rv? */
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
	char *e;
	int rv;
	struct search_idle_network *net;
	struct search_idle_host_state *h;
	int rtt_unknown, todo, all_tried;

	/* initialize cursor */
	for (net = search_idle_network_list; net != NULL; net = net->next)
		net->cursor = net->candidate_list;

	for (;;) {
		do {
			todo = 0;
			rtt_unknown = 0;
			all_tried = 1;
			for (net = search_idle_network_list; net != NULL;
			    net = net->next) {
				if ((net->flags &
				    (NET_FLAG_SCHEDULING|NET_FLAG_RTT_AVAIL))!=
				    NET_FLAG_SCHEDULING)
					continue; /* RTT is already known */
				rtt_unknown = 1;
				if (net->cursor == NULL)
					continue; /* all hosts were tried */
				all_tried = 0;
				if (net->ongoing >= PER_NET_CONCURRENCY)
					continue;
				h = net->cursor;
				/* XXX report this error? */
				e = search_idle_try_host(s, h, 0);
				net->cursor = h->next_in_the_net;
				if (net->ongoing < PER_NET_CONCURRENCY &&
				    net->cursor != NULL)
					todo = 1;
			}
		} while (todo);
		if (!rtt_unknown || all_tried)
			break;

		rv = gfarm_eventqueue_turn(s->q, NULL); /* XXX - report rv? */
	}
	rv = gfarm_eventqueue_loop(s->q, NULL); /* XXX - report rv? */
}

/*
 * 4. search networks by RTT order.
 */
static char *
search_idle_by_rtt_order(struct search_idle_state *s)
{
	struct search_idle_network *net, **netarray;
	int nnets, rtt_threshold, i, j;

	nnets = 0;
	for (net = search_idle_network_list; net != NULL; net = net->next) {
		if (net == search_idle_local_net) /* already searched */
			continue;
		if ((net->flags &
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING)) ==
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING))
			nnets++;
	}
	if (nnets <= 0)
		return (NULL);

	GFARM_MALLOC_ARRAY(netarray, nnets);
	if (netarray == NULL)
		return (GFARM_ERR_NO_MEMORY);
	i = 0;
	for (net = search_idle_network_list; net != NULL; net = net->next) {
		if (net == search_idle_local_net) /* already searched */
			continue;
		if ((net->flags &
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING)) ==
		    (NET_FLAG_RTT_AVAIL | NET_FLAG_SCHEDULING))
			netarray[i++] = net;
	}
	/* sort hosts in order of RTT */
	qsort(netarray, nnets, sizeof(*netarray), net_rtt_compare);

	for (i = 0; i < nnets; i = j) {
		/* search network which RTT is current*RTT_THRESH */
		rtt_threshold = netarray[i]->rtt_usec * RTT_THRESH;
		for (j = i + 1;
		    j < nnets && netarray[j]->rtt_usec < rtt_threshold; j++)
			;
		/* search netarray[i ... (j-1)] */
		search_idle_in_networks(s, j - i, &netarray[i]);
		
		if (search_idle_is_satisfied(s))
			break;
	}
	free(netarray);
	return (NULL);
}

/* `*nohostsp' is INPUT/OUTPUT parameter, and `*ohosts' is OUTPUT parameter */
static char *
search_idle(int *nohostsp, char **ohosts, int write_mode)
{
	char *e;
	struct search_idle_state s;
	struct search_idle_host_state *h, **results;
	int i, n;

	e = search_idle_init_state(&s, *nohostsp, default_search_method,
	    write_mode);
	if (e != NULL)
		return (e);

	/*
	 * 2. at first, search hosts on the local network
	 *   (i.e. the same network with this client host).
	 */
	if (search_idle_local_net != NULL)
		search_idle_in_networks(&s, 1, &search_idle_local_net);

	if (!search_idle_is_satisfied(&s)) {
		search_idle_examine_rtt_of_all_networks(&s);
		e = search_idle_by_rtt_order(&s);
	}

	gfarm_eventqueue_free(s.q);
	if (e != NULL)
		return (e);
	if (s.usable_hosts_number == 0)
		return (GFARM_ERR_NO_HOST);

	assert(s.available_hosts_number >= s.usable_hosts_number);
	GFARM_MALLOC_ARRAY(results, s.available_hosts_number);
	if (results == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i = 0;
	for (h = search_idle_candidate_list; h != NULL; h = h->next)
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

	for (i = 0; i < n && i < s.desired_number; i++){
		ohosts[i] = results[i]->return_value;
		results[i]->scheduled++;
	}
	*nohostsp = i;
	free(results);

	return (NULL);
}

void
gfarm_strings_expand_cyclic(int nsrchosts, char **srchosts,
	int ndsthosts, char **dsthosts)
{
	int i, j;

	for (i = 0, j = 0; i < ndsthosts; i++, j++) {
		if (j >= nsrchosts)
			j = 0;
		dsthosts[i] = srchosts[j];
	}
}

static char *
search_idle_cyclic(int nohosts, char **ohosts, int write_mode)
{
	char *e;
	int nfound = nohosts;

	e = search_idle(&nfound, ohosts, write_mode);
	if (e != NULL)
		return (e);
	if (nfound == 0)
		return (GFARM_ERR_NO_HOST);
	if (nohosts > nfound)
		gfarm_strings_expand_cyclic(nfound, ohosts,
		    nohosts - nfound, &ohosts[nfound]);
	return (NULL);
}

/*
 * If acyclic, *nohostsp is an input/output parameter,
 * otherwise *nohostsp is an input parameter.
 */
static char *
schedule_search_idle_hosts(int acyclic, int write_mode,
	int nihosts, char **ihosts, int *nohostsp, char **ohosts)
{
	char *e;
	int i;

	e = search_idle_candidate_list_init();
	if (e != NULL)
		return (e);
	for (i = 0; i < nihosts; i++) {
		e = search_idle_candidate_list_add_host(ihosts[i]);
		if (e != NULL)
			return (e);
	}
	return (acyclic ?
	    search_idle(nohostsp, ohosts, write_mode) :
	    search_idle_cyclic(*nohostsp, ohosts, write_mode));
}

/*
 * Select 'nohosts' hosts among 'nihosts' ihosts in the order of
 * load average, and return to 'ohosts'.
 * When enough number of hosts are not available, the available hosts
 * will be listed in the cyclic manner.
 * NOTE:
 *	- all of ihosts[] must be canonical hostnames.
 *	- each entry of ohosts is not strdup'ed unlike other
 *	  gfarm_schedule_* functions.
 *	  DO NOT call gfarm_strings_free_deeply() or free(*ohosts).
 */
char *
gfarm_schedule_search_idle_hosts(
	int nihosts, char **ihosts, int nohosts, char **ohosts)
{
	return (schedule_search_idle_hosts(0, 0, nihosts, ihosts,
	    &nohosts, ohosts));
}

char *
gfarm_schedule_search_idle_hosts_to_write(
	int nihosts, char **ihosts, int nohosts, char **ohosts)
{
	return (schedule_search_idle_hosts(0, 1, nihosts, ihosts,
	    &nohosts, ohosts));
}

/*
 * Similar to 'gfarm_schedule_search_idle_hosts' except for the fact that
 * the available hosts will be listed only once even if enough number of
 * hosts are not available, 
 */
char *
gfarm_schedule_search_idle_acyclic_hosts(
	int nihosts, char **ihosts, int *nohostsp, char **ohosts)
{
	return (schedule_search_idle_hosts(1, 0, nihosts, ihosts,
	    nohostsp, ohosts));
}

char *
gfarm_schedule_search_idle_acyclic_hosts_to_write(
	int nihosts, char **ihosts, int *nohostsp, char **ohosts)
{
	return (schedule_search_idle_hosts(1, 1, nihosts, ihosts,
	    nohostsp, ohosts));
}

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
			e = gfarm_fixedstrings_dup(shortage,residual,residual);
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
			return (e);
		h->statfs_cache_time = search_idle_now;
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

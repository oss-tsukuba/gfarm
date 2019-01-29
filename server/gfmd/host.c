/*
 * $Id$
 */

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

/* for host_addr_lookup() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "metadb_common.h"	/* gfarm_host_info_free_except_hostname() */
#include "gfp_xdr.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION */
#include "auth.h"
#include "config.h"
#include "context.h"

#include "callout.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "peer.h"
#include "inode.h"
#include "file_copy.h"
#include "abstract_host.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "replica_check.h"

#define HOST_HASHTAB_SIZE	3079	/* prime number */

static pthread_mutex_t total_disk_mutex = PTHREAD_MUTEX_INITIALIZER;
static gfarm_off_t total_disk_used = 0, total_disk_avail = 0;
static gfarm_off_t total_disk_used_change_in_byte = 0; /* in-memory only */
static const char total_disk_diag[] = "total_disk";

static int host_id_count = 0;

static struct host **host_id_to_host = NULL;
static size_t host_id_to_host_size = 0;
#define HOST_ID_TO_HOST_INITIAL_SIZE	64

/* in-core gfarm_host_info */
struct host {
	/* abstract_host is common data between
	 * struct host and struct mdhost */
	struct abstract_host ah; /* must be the first member of this struct */

	/*
	 * resources which are protected by the giant_lock()
	 */
	struct gfarm_host_info hi;

	int host_id;

	/*
	 * This should be included in the struct gfarm_host_info as a
	 * member, but with several reasons (mainly for ABI backward
	 * compatibilities), it is here.
	 */
	char *fsngroupname;

	struct host *removed_host_next;

	/*
	 * the following members are protected by abstract_host_mutex
	 */

#ifdef COMPAT_GFARM_2_3
	/* used by synchronous protocol (i.e. until gfarm-2.3.0) only */
	result_callback_t back_channel_result;
	disconnect_callback_t back_channel_disconnect;
	struct peer *back_channel_callback_peer;
	void *back_channel_callback_closure;
#endif

	int status_reply_waiting;
	gfarm_int32_t report_flags;
	struct host_status status;
	gfarm_off_t disk_used_change_in_byte; /* in-memory only */
	struct callout *status_callout;
	gfarm_time_t last_report;
	gfarm_time_t disconnect_time;
};

static struct gfarm_hash_table *host_hashtab = NULL;
static struct gfarm_hash_table *hostalias_hashtab = NULL;

/*
 * when a host is removed by "gfhost -d", all file_copy entities which
 * point the host must be invalidated, even after the same hostname is
 * reused.  To achieve this, we allocate new struct host for a host
 * with a reused hostname, and let removed_host_hashtab hold the removed hosts.
 */
static struct gfarm_hash_table *removed_host_hashtab = NULL;

/*
 * to make valgrind quiet,
 * we need this list to hold multiple removed hosts with same hostname,
 * because removed_host_hashtab only can hold one host for each hostname.
 */
static struct host *removed_host_list = NULL;

static struct host *host_new(struct gfarm_host_info *, struct callout *);
static void host_free(struct host *);

/* NOTE: each entry should be checked by host_is_valid(h) too */
#define FOR_ALL_HOSTS(it) \
	for (gfarm_hash_iterator_begin(host_hashtab, (it)); \
	    !gfarm_hash_iterator_is_end(it); \
	     gfarm_hash_iterator_next(it))

static const char BACK_CHANNEL_DIAG[] = "back_channel";

struct host *
host_hashtab_lookup(struct gfarm_hash_table *hashtab, const char *hostname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(hashtab, &hostname, sizeof(hostname));
	if (entry == NULL)
		return (NULL);
	return (*(struct host **)gfarm_hash_entry_data(entry));
}

struct host *
host_iterator_access(struct gfarm_hash_iterator *it)
{
	struct host **hp =
	    gfarm_hash_entry_data(gfarm_hash_iterator_access(it));

	return (*hp);
}

static void
host_invalidate(struct host *h)
{
	abstract_host_invalidate(&h->ah);
}

static void
host_validate(struct host *h)
{
	abstract_host_validate(&h->ah);
}

static int
host_is_invalid_unlocked(struct host *h)
{
	return (abstract_host_is_invalid_unlocked(&h->ah));
}

int
host_is_valid(struct host *h)
{
	return (abstract_host_is_valid(&h->ah, BACK_CHANNEL_DIAG));
}

/*
 * Search the host table for a host entry for 'hostname'.
 * Return the found entry or NULL if missing.
 */
struct host *
host_lookup(const char *hostname)
{
	struct host *h = host_hashtab_lookup(host_hashtab, hostname);

	if (h == NULL)
		return (NULL);
	if (host_is_invalid_unlocked(h)) {
		gflog_error(GFARM_MSG_1004261,
		    "unexpected error: invalid host %s in host_hashtab",
		    host_name(h));
		return (NULL);
	}
	return (h);
}

/*
 * Same as host_lookup() but it also returns an invalidated entry.
 */
struct host *
host_lookup_including_invalid(const char *hostname)
{
	struct host *h;

	h = host_hashtab_lookup(host_hashtab, hostname);
	if (h != NULL)
		return (h);
	return (host_hashtab_lookup(removed_host_hashtab, hostname));
}

static gfarm_error_t host_remove_internal(const char *, int);

/*
 * Same as host_lookup() but it adds a host entry to the host table
 * and returns it when an entry for the host is missing.
 */
struct host *
host_lookup_at_loading(const char *hostname)
{
	struct host *h = host_lookup_including_invalid(hostname);
	struct gfarm_host_info hi;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "host_lookup_at_loading";

	if (h != NULL)
		return (h);

	hi.hostname = strdup(hostname);
	hi.architecture = strdup("");  /* dummy */
	hi.ncpu = 0;
	hi.port = 0;
	hi.flags = 0;
	hi.nhostaliases = 0;
	hi.hostaliases = NULL;
	if (hi.hostname == NULL || hi.architecture == NULL)
		e = GFARM_ERR_NO_MEMORY;
	else {
		giant_lock();
		e = host_enter(&hi, &h);
		if (e == GFARM_ERR_NO_ERROR)
			host_remove_internal(hostname, 0);
		giant_unlock();
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003708,
		    "%s: failed to enter a host %s", diag, hostname);
		free(hi.hostname);
		free(hi.architecture);
		return (NULL);
	}

	return (h);
}

struct host *
host_addr_lookup(const char *hostname, struct sockaddr *addr)
{
	struct host *h = host_lookup(hostname);
#if 0
	struct gfarm_hash_iterator it;
	struct sockaddr_in *addr_in;
	struct hostent *hp;
	int i;
#endif

	if (h != NULL)
		return (h);
	if (addr->sa_family != AF_INET)
		return (NULL);

#if 0
	/*
	 * skip the following case since it is extraordinarily slow
	 * when there are some nodes that cannot be resolved.
	 */
	addr_in = (struct sockaddr_in *)addr;

	/* XXX FIXME - this is too damn slow */

	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (!host_is_valid(h))
			continue;
		hp = gethostbyname(h->hi.hostname);
		if (hp == NULL || hp->h_addrtype != AF_INET)
			continue;
		for (i = 0; hp->h_addr_list[i] != NULL; i++) {
			if (memcmp(hp->h_addr_list[i], &addr_in->sin_addr,
			    sizeof(addr_in->sin_addr)) == 0)
				return (h);
		}
	}
#endif
	return (NULL);
}

struct host *
host_namealiases_lookup(const char *hostname)
{
	struct host *h = host_lookup(hostname);

	if (h != NULL)
		return (h);
	h = host_hashtab_lookup(hostalias_hashtab, hostname);
	return ((h == NULL || host_is_invalid_unlocked(h)) ? NULL : h);
}

static void all_host_hostset_cache_purge(void);
static void hostset_of_fsngroup_cache_purge(const char *);

/* XXX FIXME missing hostaliases */
gfarm_error_t
host_enter(struct gfarm_host_info *hi, struct host **hpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct host *h;
	struct callout *callout;
	static const char diag[] = "host_enter";

	h = host_hashtab_lookup(host_hashtab, hi->hostname);
	if (h != NULL) {
		if (host_is_invalid_unlocked(h)) {
			gflog_error(GFARM_MSG_1004262,
			    "unexpected error: invalid host %s "
			    "in host_hashtab", hi->hostname);
			host_validate(h);
			if (hpp != NULL)
				*hpp = h;

			/*
			 * copy host info but keeping address of hostname
			 */
			free(hi->hostname);
			hi->hostname = h->hi.hostname;

			/* see the comment in host_name() */
			gfarm_host_info_free_except_hostname(&h->hi);

			h->hi = *hi;

			all_host_hostset_cache_purge();
			hostset_of_fsngroup_cache_purge(host_fsngroup(h));

			return (GFARM_ERR_NO_ERROR);
		} else
			return (GFARM_ERR_ALREADY_EXISTS);
	}

	callout = callout_new();
	if (callout == NULL) {
		gflog_debug(GFARM_MSG_1002212, "%s: no memory for host %s",
		    diag, hi->hostname);
		return (GFARM_ERR_NO_MEMORY);
	}
	h = host_new(hi, callout);
	if (h == NULL) {
		gflog_debug(GFARM_MSG_1001546,
			"allocation of host failed");
		callout_free(callout);
		return (GFARM_ERR_NO_MEMORY);
	}

	entry = gfarm_hash_enter(host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname), sizeof(struct host *),
	    &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001547,
			"gfarm_hash_enter() failed");
		host_free(h);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1001548,
			"create entry failed");
		host_free(h);
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	if (host_id_to_host_size < host_id_count + 1) {
		size_t new_size;
		struct host **new_array;

		if (host_id_to_host_size == 0)
			new_size = HOST_ID_TO_HOST_INITIAL_SIZE;
		else
			new_size = host_id_to_host_size * 2;

		GFARM_REALLOC_ARRAY(new_array, host_id_to_host, new_size);
		if (new_array == NULL) {
			if (!gfarm_hash_purge(host_hashtab,
			    &h->hi.hostname, sizeof(h->hi.hostname))) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "unexpected error: "
				    "host %s doesn't exist in host_hashtab",
				    h->hi.hostname);
			}
			return (GFARM_ERR_NO_MEMORY);
		}
		host_id_to_host_size = new_size;
		host_id_to_host = new_array;
	}

	/* increase id_count here, to deal with gfarm_hash_enter() failure */
	h->host_id = host_id_count;
	host_id_to_host[host_id_count] = h;
	host_id_count++;

	*(struct host **)gfarm_hash_entry_data(entry) = h;
	host_validate(h);

	all_host_hostset_cache_purge();
	hostset_of_fsngroup_cache_purge(host_fsngroup(h));

	if (hpp != NULL)
		*hpp = h;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX FIXME missing hostaliases */
static gfarm_error_t
host_remove_internal(const char *hostname, int update_deadfilecopy)
{
	struct host *h = host_lookup(hostname);
	struct gfarm_hash_entry *entry;
	int created;

	if (h == NULL) {
		gflog_debug(GFARM_MSG_1001549,
		    "host_remove(%s): not exist", hostname);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	all_host_hostset_cache_purge();
	hostset_of_fsngroup_cache_purge(host_fsngroup(h));

	host_invalidate(h);

	if (!gfarm_hash_purge(host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname))) {
		gflog_error(GFARM_MSG_1004263,
		    "unexpected error: host %s doesn't exist in host_hashtab",
		    host_name(h));
	}

	entry = gfarm_hash_enter(removed_host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname), sizeof(struct host *),
	    &created);
	if (entry == NULL) {
		gflog_error(GFARM_MSG_1004264,
		    "host %s: cannot register removed_host_hashtab: no memory",
		    host_name(h));
	}
	if (created) /* !created means that the hostname was removed at once */
		*(struct host **)gfarm_hash_entry_data(entry) = h;

	h->removed_host_next = removed_host_list;
	removed_host_list = h;

	if (update_deadfilecopy)
		dead_file_copy_host_removed(h);

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
host_remove(const char *hostname)
{
	gfarm_error_t e = host_remove_internal(hostname, 1);

	file_copy_removal_by_host_start();
	return (e);
}

gfarm_error_t
host_remove_in_cache(const char *hostname)
{
	gfarm_error_t e = host_remove_internal(hostname, 0);

	file_copy_removal_by_host_start();
	return (e);
}

struct abstract_host *
host_to_abstract_host(struct host *h)
{
	return (&h->ah);
}

static struct host *
host_downcast_to_host(struct abstract_host *h)
{
	return ((struct host *)h);
}

static struct mdhost *
host_downcast_to_mdhost(struct abstract_host *h)
{
	gflog_error(GFARM_MSG_1002761, "downcasting host %p to mdhost", h);
	abort();
	return (NULL);
}

static const char *
host_name0(struct abstract_host *h)
{
	return (host_name(abstract_host_to_host(h)));
}

/*
 * PREREQUISITE: nothing
 * LOCKS: nothing
 *
 * host_name() is usable without any mutex.
 * See hash_enter(), it's using gfarm_host_info_free_except_hostname()
 * to make h->hi.hostname always available.
 * It's implemented that way because host_name() is useful especially
 * for error logging, and acquiring giant_lock just for error logging is
 * not what we want to do.
 */
char *
host_name(struct host *h)
{
	return (h->hi.hostname);
}

static int
host_port0(struct abstract_host *h)
{
	return (host_port(abstract_host_to_host(h)));
}

int
host_port(struct host *h)
{
	return (h->hi.port);
}

char *
host_architecture(struct host *h)
{
	return (h->hi.architecture);
}

int
host_ncpu(struct host *h)
{
	return (h->hi.ncpu);
}

int
host_flags(struct host *h)
{
	return (h->hi.flags);
}

char *
host_fsngroup(struct host *h)
{
	return ((h->fsngroupname != NULL) ? h->fsngroupname : "");
}

int
host_supports_async_protocols(struct host *h)
{
	return (abstract_host_get_protocol_version(&h->ah)
		>= GFS_PROTOCOL_VERSION_V2_4);
}

int
host_supports_cksum_protocols(struct host *h)
{
	return (abstract_host_get_protocol_version(&h->ah)
		>= GFS_PROTOCOL_VERSION_V2_6);
}

#ifdef COMPAT_GFARM_2_3

void
host_set_callback(struct abstract_host *ah, struct peer *peer,
	result_callback_t result_callback,
	disconnect_callback_t disconnect_callback,
	void *closure)
{
	struct host *h = abstract_host_to_host(ah);
	static const char diag[] = "host_set_callback";

	/* XXX FIXME sanity check? */
	abstract_host_mutex_lock(ah, diag);
	h->back_channel_result = result_callback;
	h->back_channel_disconnect = disconnect_callback;
	h->back_channel_callback_peer = peer;
	h->back_channel_callback_closure = closure;
	abstract_host_mutex_unlock(ah, diag);
}

int
host_get_result_callback(struct host *h, struct peer *peer,
	result_callback_t *callbackp, void **closurep)
{
	int ok;
	static const char diag[] = "host_get_result_callback";

	abstract_host_mutex_lock(&h->ah, diag);

	if (h->back_channel_result == NULL ||
	    h->back_channel_callback_peer != peer) {
		ok = 0;
	} else {
		*callbackp = h->back_channel_result;
		*closurep = h->back_channel_callback_closure;
		h->back_channel_result = NULL;
		h->back_channel_disconnect = NULL;
		h->back_channel_callback_peer = NULL;
		h->back_channel_callback_closure = NULL;
		ok = 1;
	}

	abstract_host_mutex_unlock(&h->ah, diag);
	return (ok);
}

int
host_get_disconnect_callback(struct host *h,
	disconnect_callback_t *callbackp,
	struct peer **peerp, void **closurep)
{
	int ok;
	static const char diag[] = "host_get_disconnect_callback";

	abstract_host_mutex_lock(&h->ah, diag);

	if (h->back_channel_disconnect == NULL) {
		ok = 0;
	} else {
		*callbackp = h->back_channel_disconnect;
		*peerp = h->back_channel_callback_peer;
		*closurep = h->back_channel_callback_closure;
		h->back_channel_result = NULL;
		h->back_channel_disconnect = NULL;
		h->back_channel_callback_peer = NULL;
		h->back_channel_callback_closure = NULL;
		ok = 1;
	}

	abstract_host_mutex_unlock(&h->ah, diag);
	return (ok);
}

#endif /* COMPAT_GFARM_2_3 */

/*
 * PREREQUISITE: abstract_host::mutex
 * LOCKS: nothing
 * SLEEPS: no
 */
static int
host_is_up_unlocked(struct host *h)
{
	return (abstract_host_is_up_unlocked(&h->ah));
}

/*
 * PREREQUISITE: nothing
 * LOCKS: host::acstract_host::mutex
 * SLEEPS: no
 */
int
host_is_up(struct host *h)
{
	return (abstract_host_is_up(&h->ah));
}

int
host_is_up_with_grace(struct host *h, gfarm_time_t grace)
{
	static const char diag[] = "host_is_up_with_grace";
	int rv;

	if (host_is_up(h))
		return (1);
	if (grace <= 0)
		return (0);

	abstract_host_mutex_lock(&h->ah, diag);
	rv = h->disconnect_time + grace > time(NULL) ? 1 : 0;
	abstract_host_mutex_unlock(&h->ah, diag);
	return (rv);
}

static int
host_is_busy_unlocked(struct host *h)
{
	long long load = 0, busy = 0;

	if (host_is_up_unlocked(h)) {
		load = h->status.loadavg_1min * GFARM_F2LL_SCALE;
		busy = h->hi.ncpu * gfarm_ctxp->schedule_busy_load;
	}
	return (load >= busy);
}

static int
host_is_busy(struct host *h)
{
	int busy = 0;
	static const char diag[] = "host_is_busy";

	abstract_host_mutex_lock(&h->ah, diag);
	busy = host_is_busy_unlocked(h);
	abstract_host_mutex_unlock(&h->ah, diag);

	return (busy);
}

int
host_is_disk_available(struct host *h, gfarm_off_t size)
{
	gfarm_off_t avail, minfree = gfarm_get_minimum_free_disk_space();
	static const char diag[] = "host_is_disk_available";

	abstract_host_mutex_lock(&h->ah, diag);

	if (host_is_up_unlocked(h))
		avail = h->status.disk_avail * 1024 -
		    h->disk_used_change_in_byte;
	else
		avail = 0;
	abstract_host_mutex_unlock(&h->ah, diag);

	/* to reduce no space risk, keep minimum disk space */
	return (avail >= minfree + size);
}

int
host_check_busy(struct host *host, gfarm_int64_t now)
{
	return (abstract_host_check_busy(host_to_abstract_host(host), now,
		BACK_CHANNEL_DIAG));
}

struct callout *
host_status_callout(struct host *h)
{
	return (h->status_callout);
}

/*
 * if host_get_peer() is called,
 * same number of host_put_peer() calls should be made.
 */
struct peer *
host_get_peer(struct host *h)
{
	static const char diag[] = "host_get_peer";

	return (abstract_host_get_peer(&h->ah, diag));
}

void
host_put_peer(struct host *h, struct peer *peer)
{
	abstract_host_put_peer(&h->ah, peer);
}

static struct peer *
host_get_peer_for_replication(struct host *h)
{
	struct peer *peer = host_get_peer(h);

	if (peer != NULL) {
		peer_add_ref_for_replication(peer);
		host_put_peer(h, peer);
	}
	return (peer);
}

void
host_put_peer_for_replication(struct host *h, struct peer *peer)
{
	peer_del_ref_for_replication(peer);
}

void
host_status_reply_waiting_set(struct host *host)
{
	static const char diag[] = "host_status_reply_waiting_set";

	abstract_host_mutex_lock(&host->ah, diag);
	host->status_reply_waiting = 1;
	abstract_host_mutex_unlock(&host->ah, diag);
}

void
host_status_reply_waiting_reset(struct host *host)
{
	static const char diag[] = "host_status_reply_waiting_reset";

	abstract_host_mutex_lock(&host->ah, diag);
	host->status_reply_waiting = 0;
	abstract_host_mutex_unlock(&host->ah, diag);
}

int
host_status_reply_is_waiting(struct host *host)
{
	int waiting;
	static const char diag[] = "host_status_reply_is_waiting";

	abstract_host_mutex_lock(&host->ah, diag);
	waiting = host->status_reply_waiting;
	abstract_host_mutex_unlock(&host->ah, diag);

	return (waiting);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: total_disk_mutex
 * SLEEPS: no
 */
static void
host_total_disk_update(
	gfarm_uint64_t old_used, gfarm_uint64_t old_avail,
	gfarm_off_t old_used_change_in_byte,
	gfarm_uint64_t new_used, gfarm_uint64_t new_avail)
{
	static const char diag[] = "host_total_disk_update";

	gfarm_mutex_lock(&total_disk_mutex, diag, total_disk_diag);
	total_disk_used += new_used - old_used;
	total_disk_avail += new_avail - old_avail;
	total_disk_used_change_in_byte -= old_used_change_in_byte;
	gfarm_mutex_unlock(&total_disk_mutex, diag, total_disk_diag);
}

void
host_status_update(struct host *host, struct host_status *status)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;
	gfarm_off_t saved_used_change_in_byte;
	int saved_busy = 0, busy = 0;
	const char diag[] = "status_update";

	abstract_host_mutex_lock(&host->ah, diag);

	host->status_reply_waiting = 0;

	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = host->status.disk_used;
		saved_avail = host->status.disk_avail;
		saved_busy = host_is_busy_unlocked(host);
	}
	saved_used_change_in_byte = host->disk_used_change_in_byte;

	host->last_report = time(NULL);
	host->report_flags =
		GFM_PROTO_SCHED_FLAG_HOST_AVAIL |
		GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL;
	host->status = *status;
	host->disk_used_change_in_byte = 0;
	busy = host_is_busy_unlocked(host);

	abstract_host_mutex_unlock(&host->ah, diag);

	host_total_disk_update(saved_used, saved_avail,
	    saved_used_change_in_byte,
	    status->disk_used, status->disk_avail);

	if (saved_busy && !busy)
		replica_check_start_host_is_not_busy();
	if (saved_busy != busy)
		gflog_info(GFARM_MSG_1004747, "%s becomes %s", host_name(host),
		    busy ? "busy" : "not busy");
}

void
host_status_get_disk_usage(struct host *host,
	gfarm_off_t *used, gfarm_off_t *avail)
{
	const char diag[] = "host_status_get_disk_usage";

	abstract_host_mutex_lock(&host->ah, diag);
	if (used)
		*used = host->status.disk_used * 1024 +
		    host->disk_used_change_in_byte;
	if (avail)
		*avail = host->status.disk_avail * 1024 -
		    host->disk_used_change_in_byte;
	abstract_host_mutex_unlock(&host->ah, diag);
}

/* 0 - 100 % */
float
host_status_get_disk_usage_percent(struct host *host)
{
	gfarm_off_t used;
	gfarm_off_t avail;

	host_status_get_disk_usage(host, &used, &avail);

	return (100 * (float)used / (float)(used + avail));
}

void
host_status_update_disk_usage(struct host *host, gfarm_off_t filesize)
{
	const char diag[] = "host_status_update_disk_usage";

	abstract_host_mutex_lock(&host->ah, diag);
	host->disk_used_change_in_byte += filesize;
	abstract_host_mutex_unlock(&host->ah, diag);

	gfarm_mutex_lock(&total_disk_mutex, diag, total_disk_diag);
	total_disk_used_change_in_byte += filesize;
	gfarm_mutex_unlock(&total_disk_mutex, diag, total_disk_diag);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: abstract_host::mutex, dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe (see the comment of dead_file_copy_host_becomes_up())
 *	but abstract_host::mutex, dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 */
static void
host_set_peer_locked(struct abstract_host *ah, struct peer *p)
{
	struct host *h = abstract_host_to_host(ah);

#ifdef COMPAT_GFARM_2_3
	h->back_channel_result = NULL;
	h->back_channel_disconnect = NULL;
	h->back_channel_callback_peer = NULL;
	h->back_channel_callback_closure = NULL;
#endif
	h->status_reply_waiting = 0;
}

static void
host_set_peer_unlocked(struct abstract_host *ah, struct peer *p)
{
	dead_file_copy_host_becomes_up(abstract_host_to_host(ah));
	replica_check_start_host_up();
}

/*
 * PREREQUISITE: abstract_host::mutex
 * LOCKS: removal_pendingq.mutex, host_busyq.mutex
 * SLEEPS: no
 */
static void
host_unset_peer(struct abstract_host *ah, struct peer *peer)
{
	callout_stop(abstract_host_to_host(ah)->status_callout);
}

/*
 * PREREQUISITE: abstract_host::mutex
 */
static void
host_disable(struct abstract_host *ah)
{
	struct host *h = abstract_host_to_host(ah);
	gfarm_uint64_t saved_used, saved_avail;
	gfarm_off_t saved_used_change_in_byte;

	if (h->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = h->status.disk_used;
		saved_avail = h->status.disk_avail;
	} else {
		saved_used = 0;
		saved_avail = 0;
	}
	saved_used_change_in_byte = h->disk_used_change_in_byte;

	h->report_flags = 0;
	h->disconnect_time = time(NULL);
	h->disk_used_change_in_byte = 0;

	host_total_disk_update(saved_used, saved_avail,
	    saved_used_change_in_byte, 0, 0);
}

static void
host_disabled(struct abstract_host *ah, struct peer *peer)
{
	replica_check_start_host_down();
	dead_file_copy_host_becomes_down(abstract_host_to_host(ah));
}

/*
 * PREREQUISITE: nothing
 * LOCKS:
 *  - abstract_host::mutex
 *    in abstract_host_disconnect_request()
 *    in abstract_host::ops::disable() == host_disable()
 *    which is called from abstract_host::ops::disable() == host_disable()
 *  - callout_module::mutex
 *    in host_unset_peer()
 *    which is called from abstract_host_peer_unset()
 *    which is called from abstract_host_disconnect_request()
 *  - total_disk_mutex
 *    in abstract_host::ops::disable() == host_disable()
 *    which is called from abstract_host::ops::disable() == host_disable()
 *  - replica_check_mutex and config_var_mutex
 *    in replica_check_signal_host_down()
 *    which is called from abstract_host::ops::disable() == host_disable()
 *  - removal_pendingq:mutex -> dead_file_copy::mutex
 *    in abstract_host::ops::disabled() == host_disabled()
 *    which is called from abstract_host_disconnect_request()
 *  - peer_closing_queue.mutex
 *    in peer_del_ref()
 *    which is called from abstract_host_disconnect_request()
 *
 * NOTE: peer may be NULL.
 */
void
host_disconnect_request(struct host *h, struct peer *peer)
{
	static const char diag[] = "host_disconnect_request";

	return (abstract_host_disconnect_request(&h->ah, peer, diag));
}

struct abstract_host_ops host_ops = {
	host_downcast_to_host,
	host_downcast_to_mdhost,
	host_name0,
	host_port0,
	host_set_peer_locked,
	host_set_peer_unlocked,
	host_unset_peer,
	host_disable,
	host_disabled,
};

static struct host *
host_new(struct gfarm_host_info *hi, struct callout *callout)
{
	struct host *h;
	static const char diag[] = "host_new";

	GFARM_MALLOC(h);
	if (h == NULL)
		return (NULL);
	abstract_host_init(&h->ah, &host_ops, diag);
	h->hi = *hi;
	h->host_id = -1; /* initialized by host_enter(). see comment there */
	h->fsngroupname = NULL;
#ifdef COMPAT_GFARM_2_3
	h->back_channel_result = NULL;
	h->back_channel_disconnect = NULL;
	h->back_channel_callback_peer = NULL;
	h->back_channel_callback_closure = NULL;
#endif
	h->status_reply_waiting = 0;
	h->report_flags = 0;
	h->status.loadavg_1min =
	h->status.loadavg_5min =
	h->status.loadavg_15min = 0.0;
	h->status.disk_used =
	h->status.disk_avail = 0;
	h->disk_used_change_in_byte = 0;
	h->status_callout = callout;
	h->last_report = 0;
	h->disconnect_time = time(NULL);
	h->removed_host_next = NULL;
	return (h);
}

static void
host_free(struct host *h)
{
	free(h->status_callout);
	free(h);
}

/* only file_replicating_new() is allowed to call this routine */
gfarm_error_t
host_replicating_new(struct host *dst, struct file_replicating **frp)
{
	/* increment replication_refcount */
	struct peer *peer = host_get_peer_for_replication(dst);

	if (peer == NULL)
		return (GFARM_ERR_NO_ROUTE_TO_HOST);
	return (peer_replicating_new(peer, dst, frp));
}

static int
host_order_by_disk_avail(const void *a, const void *b)
{
	const struct host *const *h1 = a, *const *h2 = b;
	gfarm_off_t h1_avail, h2_avail;

	host_status_get_disk_usage((struct host *)*h1, NULL, &h1_avail);
	host_status_get_disk_usage((struct host *)*h2, NULL, &h2_avail);

	if (h1_avail < h2_avail)
		return (-1);
	else if (h1_avail > h2_avail)
		return (1);
	else
		return (0);
}

void
host_sort_to_remove_replicas(int nhosts, struct host **hosts)
{
	if (nhosts <= 0) /* 2nd parameter of qsort(3) is unsigned */
		return;

	qsort(hosts, nhosts, sizeof(*hosts), host_order_by_disk_avail);
}


gfarm_error_t
host_is_disk_available_filter(struct host *host, void *closure)
{
	gfarm_off_t *sizep = closure;

	return (host_is_disk_available(host, *sizep));
}

/*
 * PREREQUISITE: giant_lock (for gfarm_replication_busy_host)
 * LOCKS:
 *  - abstract_host::mutex
 *    in host_is_busy() and host_is_disk_available()
 */
gfarm_error_t
host_is_not_busy_and_disk_available_filter(struct host *host, void *closure)
{
	return ((gfarm_replication_busy_host || !host_is_busy(host))
		&& host_is_disk_available_filter(host, closure));
}

gfarm_error_t
host_array_alloc(int *nhostsp, struct host ***hostsp)
{
	int i, nhosts;
	struct host *h, **hosts;
	struct gfarm_hash_iterator it;

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_valid(h))
			++nhosts;
	}

	GFARM_MALLOC_ARRAY(hosts, nhosts > 0 ? nhosts : 1);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i = 0;
	FOR_ALL_HOSTS(&it) {
		if (i >= nhosts) /* always false due to giant_lock */
			break;
		h = host_iterator_access(&it);
		if (host_is_valid(h))
			hosts[i++] = h;
	}
	*nhostsp = i;
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_from_all(int (*filter)(struct host *, void *), void *closure,
	gfarm_int32_t *nhostsp, struct host ***hostsp)
{
	gfarm_error_t e;
	int i, j, nhosts;
	struct host **hosts;

	e = host_array_alloc(&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	j = 0;
	for (i = 0; i < nhosts; i++) {
		if ((*filter)(hosts[i], closure))
			hosts[j++] = hosts[i];
	}
	*nhostsp = j;
	*hostsp = hosts;
	return (e);
}

gfarm_error_t
host_from_all_except(int (*filter)(struct host *, void *), void *closure,
	struct hostset *exceptions,
	gfarm_int32_t *nhostsp, struct host ***hostsp)
{
	gfarm_error_t e;
	int i, j, nhosts;
	struct host **hosts;

	e = host_array_alloc(&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	j = 0;
	for (i = 0; i < nhosts; i++) {
		if (!hostset_has_host(exceptions, hosts[i]) &&
		    (*filter)(hosts[i], closure))
			hosts[j++] = hosts[i];
	}
	*nhostsp = j;
	*hostsp = hosts;
	return (e);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: abstract_host::mutex
 * SLEEPS: no
 */
int
host_number()
{
	struct gfarm_hash_iterator it;
	struct host *h;
	int nhosts = 0;

	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_valid(h))
			++nhosts;
	}
	return (nhosts);
}

/*
 * just select randomly		XXX FIXME: needs to improve
 */
int
host_select_one(int nhosts, struct host **hosts, const char *diag)
{
	if (nhosts > 1) {
		return (gfarm_random() % nhosts);
	}
	if (nhosts <= 0) {
		/* shouldn't happen */
		gflog_warning(GFARM_MSG_1005012, "%s: nhosts=%d",
		    diag, nhosts);
	}
	return (0);
}

#ifdef NOT_USED
/*
 * save all to text file
 */

static FILE *host_fp;

gfarm_error_t
host_info_open_for_seq_write(void)
{
	host_fp = fopen("host", "w");
	if (host_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_write_next(struct gfarm_host_info *hi)
{
	int i;

	fprintf(host_fp, "%s %d %d %d %s", hi->hostname, hi->port,
	    hi->ncpu, hi->flags, hi->architecture);
	for (i = 0; i < hi->nhostaliases; i++)
		fprintf(host_fp, " %s", hi->hostaliases[i]);
	fprintf(host_fp, "\n");
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_close_for_seq_write(void)
{
	fclose(host_fp);
	return (GFARM_ERR_NO_ERROR);
}
#endif /* NOT_USED */

/* The memory owner of `*hi' is changed to host.c */
void
host_add_one(void *closure,
	struct gfarm_internal_host_info *hi)
{
	struct host *h = NULL;
	gfarm_error_t e = host_enter(&(hi->hi), &h);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000266,
		    "host_add_one: %s", gfarm_error_string(e));

	if (h != NULL) {
		/*
		 * all_host_hostset_cache_purge() is not needed,
		 * because it's done in host_enter()
		 */

		/*
		 * usually hostset_of_fsngroup_cache_purge() is done in
		 * host_enter(), but this is for e == GFARM_ERR_ALREADY_EXISTS
		 */
		hostset_of_fsngroup_cache_purge(host_fsngroup(h));

		if (hi->fsngroupname != NULL)
			/*
			 * Not strdup() but just change the ownership.
			 */
			h->fsngroupname = hi->fsngroupname;
		else
			h->fsngroupname = NULL;

		/* this is for new fsngroupname */
		hostset_of_fsngroup_cache_purge(host_fsngroup(h));
	}
}

void
host_init(void)
{
	gfarm_error_t e;

	host_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (host_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1000267, "no memory for host hashtab");
	hostalias_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (hostalias_hashtab == NULL) {
		gfarm_hash_table_free(host_hashtab);
		gflog_fatal(GFARM_MSG_1000268,
		    "no memory for hostalias hashtab");
	}
	removed_host_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (removed_host_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1004265,
		    "no memory for host removed_hashtab");

	e = db_host_load(NULL, host_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000269,
		    "loading hosts: %s", gfarm_error_string(e));
}

#ifndef TEST
/*
 * protocol handler
 */

/*
 * PREREQUISITE: giant_lock
 * LOCKS: abstract_host::mutex
 * SLEEPS: maybe
 *	but abstract_host::mutex won't be blocked while sleeping.
 */
static gfarm_error_t
gfm_server_host_generic_get(struct peer *peer,
	gfarm_error_t (*reply)(struct host *, struct peer *, const char *),
	int (*filter)(struct host *, void *), void *closure,
	int no_match_is_ok, const char *diag)
{
	gfarm_error_t e, e2;
	gfarm_int32_t nhosts, nmatch, i, answered;
	struct gfarm_hash_iterator it;
	struct host *h;
	char *match;

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		++nhosts;
	}

	/*
	 * remember the matching result to return consistent answer.
	 * note that the result of host_is_valid() may vary at each call.
	 */
	GFARM_MALLOC_ARRAY(match, nhosts > 0 ? nhosts : 1);
	nmatch = 0;
	if (match == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002216,
		    "%s: no memory for %d hosts", diag, nhosts);
	} else {
		i = 0;
		FOR_ALL_HOSTS(&it) {
			if (i >= nhosts) /* always false due to giant_lock */
				break;
			h = host_iterator_access(&it);
			if (host_is_valid(h) &&
			    (filter == NULL || (*filter)(h, closure))) {
				match[i] = 1;
				++nmatch;
			} else {
				match[i] = 0;
			}
			++i;
		}
		if (no_match_is_ok || nmatch > 0) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			gflog_debug(GFARM_MSG_1002217,
			    "%s: no matching host", diag);
		}
	}
	e2 = gfm_server_put_reply(peer, diag, e, "i", nmatch);
	/* if network error doesn't happen, e2 == e here */
	if (e2 == GFARM_ERR_NO_ERROR) {
		i = answered = 0;
		FOR_ALL_HOSTS(&it) {
			if (i >= nhosts || answered >= nmatch)
				break;
			h = host_iterator_access(&it);
			if (match[i]) {
				e2 = (*reply)(h, peer, diag);
				if (e2 != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_1002219,
					    "%s: host_info_send(): %s",
					    diag, gfarm_error_string(e));
					break;
				}
				++answered;
			}
			i++;
		}
	}
	if (match != NULL)
		free(match);

	return (e2);
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
host_info_send(struct gfp_xdr *client, struct host *h)
{
	struct gfarm_host_info *hi;

	hi = &h->hi;
	return (gfp_xdr_send(client, "ssiiii",
	    hi->hostname, hi->architecture,
	    hi->ncpu, hi->port, hi->flags, hi->nhostaliases));
}

static gfarm_error_t
host_info_reply(struct host *h, struct peer *peer, const char *diag)
{
	return (host_info_send(peer_get_conn(peer), h));
}

gfarm_error_t
gfm_server_host_info_get_common(struct peer *peer,
	int (*filter)(struct host *, void *), void *closure, const char *diag)
{
	gfarm_error_t e;

	/* XXX FIXME too long giant lock */
	giant_lock();

	e = gfm_server_host_generic_get(peer, host_info_reply, filter, closure,
	    filter == NULL, diag);

	giant_unlock();

	return (e);
}

gfarm_error_t
gfm_server_host_info_get_all(struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_HOST_INFO_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	return (gfm_server_host_info_get_common(peer, NULL, NULL, diag));
}

static int
arch_filter(struct host *h, void *closure)
{
	char *architecture = closure;

	return (strcmp(h->hi.architecture, architecture) == 0);
}

gfarm_error_t
gfm_server_host_info_get_by_architecture(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *architecture;
	static const char diag[] = "GFM_PROTO_HOST_INFO_GET_BY_ARCHITECTURE";

	e = gfm_server_get_request(peer, diag, "s", &architecture);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001555,
		    "host_info_get_by_architecture request failure: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	e = gfm_server_host_info_get_common(peer, arch_filter, architecture,
	    diag);

	free(architecture);
	return (e);
}

gfarm_error_t
gfm_server_host_info_get_by_names_common(struct peer *peer,
	int from_client, int skip,
	struct host *(*lookup)(const char *), const char *diag)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t nhosts;
	char *host, **hosts;
	int i, j, eof, no_memory = 0;
	struct host *h;

	e = gfm_server_get_request(peer, diag, "i", &nhosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL) {
		no_memory = 1;
		/* Continue processing. */
	}

	for (i = 0; i < nhosts; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &host);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1003470,
			    "%s: gfp_xdr_recv(): %s",
			    diag, gfarm_error_string(e));
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (hosts != NULL) {
				for (j = 0; j < i; j++)
					free(hosts[j]);
				free(hosts);
			}
			return (e);
		}
		if (hosts == NULL) {
			free(host);
		} else {
			if (host == NULL)
				no_memory = 1;
			hosts[i] = host;
		}
	}
	if (skip) {
		e = GFARM_ERR_NO_ERROR; /* ignore GFARM_ERR_NO_MEMORY */
		goto free_hosts;
	}

	e = gfm_server_put_reply(peer, diag,
	    no_memory ? GFARM_ERR_NO_MEMORY : GFARM_ERR_NO_ERROR, "");
	/* if network error doesn't happen, `e' holds RPC result here */
	if (e != GFARM_ERR_NO_ERROR)
		goto free_hosts;

	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < nhosts; i++) {
		h = (*lookup)(hosts[i]);
		if (h == NULL) {
			gflog_debug(GFARM_MSG_1003471,
			    "%s: host lookup <%s>: failed", diag, hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_UNKNOWN_HOST, "");
		} else {
			gflog_debug(GFARM_MSG_1003472,
			    "%s: host lookup <%s>: ok", diag, hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = host_info_send(client, h);
		}
		if (peer_had_protocol_error(peer))
			break;
	}
	/*
	 * if (!peer_had_protocol_error(peer))
	 *	the variable `e' holds last host's reply code
	 */
	giant_unlock();

free_hosts:
	if (hosts != NULL) {
		for (i = 0; i < nhosts; i++)
			free(hosts[i]);
		free(hosts);
	}
	return (e);
}

gfarm_error_t
gfm_server_host_info_get_by_names(struct peer *peer, int from_client, int skip)
{
	return (gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, host_lookup, "host_info_get_by_names"));
}

gfarm_error_t
gfm_server_host_info_get_by_namealiases(struct peer *peer,
	int from_client, int skip)
{
	return (gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, host_namealiases_lookup,
	    "host_info_get_by_namealiases"));
}

gfarm_error_t
host_info_verify(struct gfarm_host_info *hi, const char *diag)
{
	if (strlen(hi->hostname) > GFARM_HOST_NAME_MAX) {
		gflog_debug(GFARM_MSG_1002421, "%s: too long hostname: %s",
		    diag, hi->hostname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (strlen(hi->architecture) > GFARM_HOST_ARCHITECTURE_NAME_MAX) {
		gflog_debug(GFARM_MSG_1002422,
		    "%s: %s: too long architecture: %s",
		    diag, hi->hostname, hi->architecture);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (hi->ncpu < 0) {
		gflog_debug(GFARM_MSG_1002423,
		    "%s: %s: invalid cpu number: %d",
		    diag, hi->hostname, hi->ncpu);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (hi->port <= 0 || hi->port >= 65536) {
		gflog_debug(GFARM_MSG_1002424,
		    "%s: %s: invalid port number: %d",
		    diag, hi->hostname, hi->port);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_host_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user = peer_get_user(peer);
	gfarm_int32_t ncpu, port, flags;
	struct gfarm_host_info hi;
	static const char diag[] = "GFM_PROTO_HOST_INFO_SET";

	e = gfm_server_get_request(peer, diag, "ssiii",
	    &hi.hostname, &hi.architecture, &ncpu, &port, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001562,
			"host_info_set request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hi.hostname);
		free(hi.architecture);
		return (GFARM_ERR_NO_ERROR);
	}
	hi.ncpu = ncpu;
	hi.port = port;
	hi.flags = flags;
	/* XXX FIXME missing hostaliases */
	hi.nhostaliases = 0;
	hi.hostaliases = NULL;

	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_1001563,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (host_lookup(hi.hostname) != NULL) {
		gflog_debug(GFARM_MSG_1001564,
			"host already exists");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else if ((e = host_info_verify(&hi, diag)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((e = host_enter(&hi, NULL)) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((e = db_host_add(&hi)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001565,
			"db_host_add() failed: %s",
			gfarm_error_string(e));
		host_remove(hi.hostname);
		hi.hostname = hi.architecture = NULL;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001566,
			"error occurred during process: %s",
			gfarm_error_string(e));
		if (hi.hostname != NULL)
			free(hi.hostname);
		if (hi.architecture != NULL)
			free(hi.architecture);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

void
host_modify(struct host *h, struct gfarm_host_info *hi)
{
	free(h->hi.architecture);
	h->hi.architecture = hi->architecture;
	hi->architecture = NULL;
	h->hi.ncpu = hi->ncpu;
	h->hi.port = hi->port;
	h->hi.flags = hi->flags;
}

gfarm_error_t
host_fsngroup_modify(struct host *h, const char *fsngroupname,
	const char *diag)
{
	char *g;

	if (strlen(fsngroupname) > GFARM_CLUSTER_NAME_MAX) {
		gflog_debug(GFARM_MSG_1004005,
		    "%s: host %s: too long fsngroupname \"%s\"",
		    diag, host_name(h), fsngroupname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (fsngroupname == NULL)
		g = NULL;
	else if ((g = strdup_log(fsngroupname, diag)) == NULL)
		return (GFARM_ERR_NO_MEMORY);

	hostset_of_fsngroup_cache_purge(host_fsngroup(h));

	if (h->fsngroupname != NULL)
		free(h->fsngroupname);
	h->fsngroupname = g;

	hostset_of_fsngroup_cache_purge(host_fsngroup(h));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_host_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	gfarm_int32_t ncpu, port, flags;
	struct gfarm_host_info hi;
	struct host *h;
	int needs_free = 0;
	static const char diag[] = "GFM_PROTO_HOST_INFO_MODIFY";

	e = gfm_server_get_request(peer, diag, "ssiii",
	    &hi.hostname, &hi.architecture, &ncpu, &port, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001567,
			"host_info_modify request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hi.hostname);
		free(hi.architecture);
		return (GFARM_ERR_NO_ERROR);
	}
	hi.ncpu = ncpu;
	hi.port = port;
	hi.flags = flags;
	/* XXX FIXME missing hostaliases */

	/* XXX should we disconnect a back channel to the host? */
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_1001568,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		needs_free = 1;
	} else if ((h = host_lookup(hi.hostname)) == NULL) {
		gflog_debug(GFARM_MSG_1001569, "host does not exists");
		e = GFARM_ERR_NO_SUCH_OBJECT;
		needs_free = 1;
	} else if ((e = host_info_verify(&hi, diag)) != GFARM_ERR_NO_ERROR) {
		needs_free = 1;
	} else if ((e = db_host_modify(&hi,
	    DB_HOST_MOD_ARCHITECTURE|DB_HOST_MOD_NCPU|DB_HOST_MOD_FLAGS,
	    /* XXX */ 0, NULL, 0, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001570,
			"db_host_modify failed: %s",
			gfarm_error_string(e));
		needs_free = 1;
	} else {
		host_modify(h, &hi);
		free(hi.hostname);
	}
	if (needs_free) {
		free(hi.hostname);
		free(hi.architecture);
	}
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
host_info_remove_default(const char *hostname, const char *diag)
{
	gfarm_error_t e, e2;
	struct host *host;

	if ((host = host_lookup(hostname)) == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	/* disconnect the back channel */
	gflog_info(GFARM_MSG_1002425,
	    "back_channel(%s): disconnecting: host info removed", hostname);
	host_disconnect_request(host, NULL);

	if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004006, "%s: db_begin: %s",
		    diag, gfarm_error_string(e));
	} else if ((e = host_remove(hostname)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004007, "%s: host_remove: %s",
		    diag, gfarm_error_string(e));
	} else if ((e2 = db_host_remove(hostname)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000272,
		    "%s: db_host_remove: %s",
		    diag, gfarm_error_string(e2));
	}
	db_end(diag);

	return (e);
}

/* this interface is made as a hook for a private extension */
gfarm_error_t (*host_info_remove)(const char *, const char *) =
	host_info_remove_default;

gfarm_error_t
gfm_server_host_info_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	char *hostname;
	static const char diag[] = "GFM_PROTO_HOST_INFO_REMOVE";

	e = gfm_server_get_request(peer, diag, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001571,
			"host_info_remove request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}
	/*
	 * XXX should we remove all file copy entries stored on the
	 * specified host?
	 */
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_1001572,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		e = host_info_remove(hostname, diag);
	free(hostname);
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
}

/* called from fs.c:gfm_server_schedule_file() as well */
gfarm_error_t
host_schedule_reply(struct host *h, struct peer *peer, const char *diag)
{
	struct host_status status;
	gfarm_off_t disk_used_change;
	gfarm_time_t last_report;
	gfarm_int32_t report_flags;

	abstract_host_mutex_lock(&h->ah, diag);
	status = h->status;
	disk_used_change = h->disk_used_change_in_byte / 1024;
	last_report = h->last_report;
	report_flags = h->report_flags;
	abstract_host_mutex_unlock(&h->ah, diag);
	return (gfp_xdr_send(peer_get_conn(peer), "siiillllii",
	    h->hi.hostname, h->hi.port, h->hi.ncpu,
	    (gfarm_int32_t)(status.loadavg_1min * GFM_PROTO_LOADAVG_FSCALE),
	    last_report,
	    (gfarm_int64_t)(status.disk_used + disk_used_change),
	    (gfarm_int64_t)(status.disk_avail - disk_used_change),
	    (gfarm_int64_t)0 /* rtt_cache_time */,
	    (gfarm_int32_t)0 /* rtt_usec */,
	    report_flags));
}

gfarm_error_t
host_schedule_reply_all(struct peer *peer,
	int (*filter)(struct host *, void *), void *closure, const char *diag)
{
	return (gfm_server_host_generic_get(peer, host_schedule_reply,
	    filter, closure, 1, diag));
}

gfarm_error_t
gfm_server_hostname_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	char *hostname;
	static const char diag[] = "GFM_PROTO_HOSTNAME_SET";

	e = gfm_server_get_request(peer, diag, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001577,
			"gfm_server_get_request() failure");
		return (e);
	}
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();
	if (from_client) {
		gflog_debug(GFARM_MSG_1001578,
			"operation is not permitted for from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		e = peer_set_host(peer, hostname);
	giant_unlock();
	free(hostname);

	return (gfm_server_put_reply(peer, diag, e, ""));
}

static int
up_and_domain_filter(struct host *h, void *d)
{
	const char *domain = d;

	return (host_is_up(h) &&
	    gfarm_host_is_in_domain(host_name(h), domain));
}

gfarm_error_t
gfm_server_schedule_host_domain(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	char *domain;
	static const char diag[] = "GFM_PROTO_SCHEDULE_HOST_DOMAIN";

	e = gfm_server_get_request(peer, diag, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001579,
			"schedule_host_domain request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();
	e = host_schedule_reply_all(peer, up_and_domain_filter, domain, diag);
	giant_unlock();
	free(domain);

	return (e);
}

gfarm_error_t
gfm_server_statfs(struct peer *peer, int from_client, int skip)
{
	gfarm_uint64_t used, avail, files;
	gfarm_off_t used_change;
	static const char diag[] = "GFM_PROTO_STATFS";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	files = inode_total_num();
	gfarm_mutex_lock(&total_disk_mutex, diag, total_disk_diag);
	used_change = total_disk_used_change_in_byte / 1024;
	used = total_disk_used + used_change;
	avail = total_disk_avail - used_change;
	gfarm_mutex_unlock(&total_disk_mutex, diag, total_disk_diag);

	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "lll",
		    used, avail, files));
}

#endif /* TEST */

/*
 * A generic function to select filesystem nodes.
 * NOTE: only valid hosts are returned.
 *
 * REQUISITE: giant_lock
 */
gfarm_error_t
host_iterate(
	/**
	 * A filtering and recording function to select filesystem nodes
	 *
	 *	@param [in] hp	A pointer of a struct host.
	 *	@param [in] closure	closure argument will be passed back
	 *	@param [out] an_elem	An arbitarary pointer.
	 *	host_iterate() returns an array of this if it is not NULL.
	 *
	 *	@return boolean value. true if the entry is valid and
	 *	should be included in the return value.
	 */
	int (*do_record)(struct host *, void *, void *),
	void *closure,		/* The second argumrent of the f */
	size_t esize,		/* A size of one element to be allocated */
	size_t *nelemp,		/* Returns # of allocated elements */
	void **arrayp)		/* Returns Allocated array */
{
	struct gfarm_hash_iterator it;
	size_t nhosts, nmatches;
	struct host *h;
	void *ret;
	char *p;
	int of;
	size_t sz;

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_valid(h))
			nhosts++;
	}
	if (nhosts == 0) {
		*nelemp = 0;
		*arrayp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}

	of = 0;
	sz = gfarm_size_mul(&of, esize, nhosts);
	if (of || (ret = malloc(sz)) == NULL)
		return (GFARM_ERR_NO_MEMORY);

	nmatches = 0;
	p = ret;
	FOR_ALL_HOSTS(&it) {
		if (nmatches >= nhosts)
			break;
		h = host_iterator_access(&it);
		if (host_is_valid(h) && (*do_record)(h, closure, p)) {
			p += esize;
			nmatches++;
		}
	}

	*nelemp = nmatches;
	*arrayp = ret;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * hostset
 */

typedef unsigned long long hostset_word_t;
#define HOSTSET_WORD_BITS	(sizeof(hostset_word_t) * CHAR_BIT)
#define HOSTSET_NUM_WORDS(bits)	\
	(((bits) + HOSTSET_WORD_BITS - 1) / HOSTSET_WORD_BITS)

struct hostset {
	size_t nwords;
	hostset_word_t *words; /* maybe realloc'ed, see hostset_add_host() */
};

void
hostset_free(struct hostset *hs)
{
	free(hs->words);
	free(hs);
}

static struct hostset *
hostset_dup(struct hostset *hs)
{
	struct hostset *new_hs;
	size_t i, nwords = hs->nwords;

	GFARM_MALLOC(new_hs);
	if (new_hs == NULL)
		return (NULL); /* GFARM_ERR_NO_MEMORY */

	new_hs->nwords = nwords;
	GFARM_MALLOC_ARRAY(new_hs->words, nwords);
	if (new_hs->words == NULL) {
		free(new_hs);
		return (NULL); /* GFARM_ERR_NO_MEMORY */
	}
	for (i = 0; i < nwords; i++)
		new_hs->words[i] = hs->words[i];

	return (new_hs);

}

static struct hostset *
hostset_alloc(void)
{
	struct hostset *hs;

	/* popcount64() implementation assumes this */
	assert(HOSTSET_WORD_BITS == 64);

	GFARM_MALLOC(hs);
	if (hs == NULL)
		return (NULL); /* GFARM_ERR_NO_MEMORY */

	hs->nwords = HOSTSET_NUM_WORDS(host_id_count);
	if (hs->nwords == 0)
		hs->nwords = 1;
	GFARM_MALLOC_ARRAY(hs->words, hs->nwords);
	if (hs->words == NULL) {
		free(hs);
		return (NULL); /* GFARM_ERR_NO_MEMORY */
	}

	return (hs);
}

struct hostset *
hostset_empty_alloc(void)
{
	struct hostset *hs = hostset_alloc();
	size_t i;

	if (hs == NULL)
		return (NULL); /* GFARM_ERR_NO_MEMORY */
	for (i = 0; i < hs->nwords; i++)
		hs->words[i] = 0ULL;
	return (hs);
}

/* should be used only when (*filter)() is true for nearly half of the hosts */
static struct hostset *
hostset_alloc_by(int (*filter)(struct host *, void *), void *closure,
	int *n_hostsp)
{
	size_t i;
	int j, n_hosts = 0, host_index = 0;
	hostset_word_t bits;
	struct hostset *hs = hostset_alloc();

	if (hs == NULL)
		return (NULL); /* GFARM_ERR_NO_MEMORY */

	for (i = 0; i < hs->nwords; i++) {
		hs->words[i] = 0ULL;
		bits = 1ULL;
		for (j = 0;
		    j < HOSTSET_WORD_BITS && host_index < host_id_count;
		    j++, host_index++) {
			if (filter(host_id_to_host[host_index], closure)) {
				hs->words[i] |= bits;
				n_hosts++;
			}
			bits <<= 1;
		}
	}
	if (n_hostsp != NULL)
		*n_hostsp = n_hosts;
	return (hs);
}

static int
host_valid_filter(struct host *h, void *closure)
{
	return (!host_is_invalid_unlocked(h));
}

struct hostset *
hostset_of_all_hosts_alloc_internal(int *n_all_hostsp)
{
	return (hostset_alloc_by(host_valid_filter, NULL, n_all_hostsp));
}

static struct hostset *all_host_hostset_cache = NULL;
static int all_host_nhosts = 0;

struct hostset *
hostset_of_all_hosts_alloc(int *n_all_hostsp)
{
	struct hostset *hs;

	if (all_host_hostset_cache == NULL) {
		all_host_hostset_cache =
		    hostset_of_all_hosts_alloc_internal(&all_host_nhosts);
		if (all_host_hostset_cache == NULL)
			return (NULL); /* GFARM_ERR_NO_MEMORY */
	}

	hs = hostset_dup(all_host_hostset_cache);
	if (hs == NULL)
		return (hs);

	if (n_all_hostsp != NULL)
		*n_all_hostsp = all_host_nhosts;
	return (hs);
}

static void
all_host_hostset_cache_purge(void)
{
	if (all_host_hostset_cache != NULL)
		hostset_free(all_host_hostset_cache);
	all_host_hostset_cache = NULL;
	all_host_nhosts = 0;
}

static int
host_fsngroup_filter(struct host *h, void *closure)
{
	const char *fsngroup = closure;

	return (!host_is_invalid_unlocked(h) &&
		strcmp(host_fsngroup(h), fsngroup) == 0);
}

static struct hostset *
hostset_of_fsngroup_alloc_internal(const char *fsngroup, int *n_hostsp)
{
	return (hostset_alloc_by(
	    host_fsngroup_filter, (void *)fsngroup /*UNCONST*/,
	    n_hostsp));
}

int
hostset_has_host(struct hostset *hs, struct host *h)
{
	unsigned int host_id = h->host_id;
	unsigned int word_index = host_id / HOSTSET_WORD_BITS;
	unsigned int bit_index = host_id % HOSTSET_WORD_BITS;

	if (hs->nwords <= word_index)
		return (0); /* dosn't have the host */
	return ((hs->words[word_index] & (1ULL << bit_index)) != 0);
}

gfarm_error_t
hostset_add_host(struct hostset *hs, struct host *h)
{
	unsigned int host_id = h->host_id;
	unsigned int word_index = host_id / HOSTSET_WORD_BITS;
	unsigned int bit_index = host_id % HOSTSET_WORD_BITS;

	if (hs->nwords <= word_index) {
		size_t i, new_nwords = hs->nwords * 2;
		hostset_word_t *new_words;

		while (new_nwords <= word_index)
			new_nwords *= 2;
		GFARM_REALLOC_ARRAY(new_words, hs->words, new_nwords);
		if (new_words == NULL)
			return (GFARM_ERR_NO_MEMORY);
		for (i = hs->nwords; i < new_nwords; i++)
			new_words[i] = 0;
		hs->nwords = new_nwords;
		hs->words = new_words;
	}
	hs->words[word_index] |= 1ULL << bit_index;
	return (GFARM_ERR_NO_ERROR);
}

static void
hostset_delete_host(struct hostset *hs, struct host *h)
{
	unsigned int host_id = h->host_id;
	unsigned int word_index = host_id / HOSTSET_WORD_BITS;
	unsigned int bit_index = host_id % HOSTSET_WORD_BITS;

	if (hs->nwords <= word_index)
		return; /* nothing to do */
	hs->words[word_index] &= ~(1ULL << bit_index);
}

void
hostset_intersect(struct hostset *hs, struct hostset *hs2)
{
	size_t i;

	for (i = 0; i < hs->nwords && i < hs2->nwords; i++)
		hs->words[i] &= hs2->words[i];
}

static void
hostset_except(struct hostset *hs, struct hostset *except)
{
	size_t i;

	for (i = 0; i < hs->nwords && i < except->nwords; i++)
		hs->words[i] &= ~except->words[i];
}

/*
 * this implementation is optimized for the case
 * where number of hosts in hs is relatively smaller
 */
static void
hostset_foreach(struct hostset *hs,
	void (*f)(struct host *, void *), void *closure)
{
	int i, j;
	int host_index = 0;

	for (i = 0; i < hs->nwords; i++, host_index += HOSTSET_WORD_BITS) {
		hostset_word_t bit, w = hs->words[i];

		if (w == 0ULL)
			continue;
		if (w & 0x00000000ffffffffULL) {
			if (w & 0x000000000000ffffULL) {
				if (w & 0x00000000000000ffULL) {
					for (bit = 1ULL,
					    j = 0;
					    j < 8 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
				if (w & 0x000000000000ff00ULL) {
					for (bit = 0x100ULL,
					    j =  8;
					    j < 16 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
			}
			if (w & 0x00000000ffff0000ULL) {
				if (w & 0x0000000000ff0000ULL) {
					for (bit = 0x10000ULL,
					    j = 16;
					    j < 24 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
				if (w & 0x00000000ff000000ULL) {
					for (bit = 0x1000000ULL,
					    j = 24;
					    j < 32 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
			}
		}
		if (w & 0xffffffff00000000ULL) {
			if (w & 0x0000ffff00000000ULL) {
				if (w & 0x000000ff00000000ULL) {
					for (bit = 0x100000000ULL,
					    j = 32;
					    j < 40 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
				if (w & 0x0000ff0000000000ULL) {
					for (bit = 0x10000000000ULL,
					    j = 40;
					    j < 48 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
			}
			if (w & 0xffff000000000000ULL) {
				if (w & 0x00ff000000000000ULL) {
					for (bit = 0x1000000000000ULL,
					    j = 48;
					    j < 56 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
				if (w & 0xff00000000000000ULL) {
					for (bit = 0x100000000000000ULL,
					    j = 56;
					    j < 64 &&
					    host_index + j < host_id_count;
					    j++, bit <<= 1)
						if (w & bit)
							f(host_id_to_host[
							  host_index + j],
							  closure);
				}
			}
		}
	}
}

struct hostset_filter_closure {
	struct hostset *hs;
	int (*filter)(struct host *, void *);
	void *closure;
};

static void
hostset_filter_apply(struct host *h, void *closure)
{
	struct hostset_filter_closure *c = closure;

	if (!(*c->filter)(h, c->closure))
		hostset_delete_host(c->hs, h);
}

static void
hostset_filter(struct hostset *hs,
	int (*filter)(struct host *, void *), void *closure)
{
	struct hostset_filter_closure c;

	c.hs = hs;
	c.filter = filter;
	c.closure = closure;
	hostset_foreach(hs, hostset_filter_apply, &c);
}

#ifndef HAVE_POPCOUNT64
static int
popcount64(unsigned long long bits)
{
#ifdef HAVE___BUILTIN_POPCOUNTLL
	return (__builtin_popcountll(bits)); /* gcc extension */
#else /* 64bit version of HAKMEM #169 */
	bits = ((bits >>  1) & 0x5555555555555555) +
		(bits        & 0x5555555555555555);
	bits = ((bits >>  2) & 0x3333333333333333) +
		(bits        & 0x3333333333333333);
	bits = ((bits >>  4) & 0x0f0f0f0f0f0f0f0f) + 
		(bits        & 0x0f0f0f0f0f0f0f0f);
	bits = ((bits >>  8) & 0x00ff00ff00ff00ff) +
		(bits        & 0x00ff00ff00ff00ff);
	bits = ((bits >> 16) & 0x0000ffff0000ffff) + 
		(bits        & 0x0000ffff0000ffff);
	bits = ((bits >> 32) & 0x00000000ffffffff) + 
		(bits        & 0x00000000ffffffff);
	return ((int)bits);
#endif /* !defined(HAVE___BUILTIN_POPCOUNTLL) */
}
#endif /* !defined(HAVE_POPCOUNT64) */

static int
hostset_count_hosts(struct hostset *hs)
{
	size_t i;
	int count = 0;

	for (i = 0; i < hs->nwords; i++)
		count += popcount64(hs->words[i]);
	return (count);
}

struct host_count_up_closure {
	gfarm_time_t grace;
	int n_up;
};

static void
host_count_up(struct host *h, void *closure)
{
	struct host_count_up_closure *c = closure;

	if (host_is_up_with_grace(h, c->grace))
		c->n_up++;
}

int
hostset_count_hosts_up(struct hostset *hs, gfarm_time_t grace)
{
	struct host_count_up_closure c;

	c.grace = grace;
	c.n_up = 0;
	hostset_foreach(hs, host_count_up, &c);
	return (c.n_up);
}

struct hostset_to_hosts_closure {
	struct host **hosts;
	int index;
};

static void
hostset_to_host_record(struct host *h, void *closure)
{
	struct hostset_to_hosts_closure *c = closure;

	c->hosts[c->index++] = h;
}

static void
hostset_to_hosts(struct hostset *hs, struct host **hosts)
{
	struct hostset_to_hosts_closure c;

	c.hosts = hosts;
	c.index = 0;
	hostset_foreach(hs, hostset_to_host_record, &c);
}

struct hostset_collect_host_id_closure {
	int *host_id_array;
	int index;
};

static void
hostset_collect_host_id_each(struct host *h, void *closure)
{
	struct hostset_collect_host_id_closure *c = closure;

	c->host_id_array[c->index++] = h->host_id;
}

static void
hostset_collect_host_id(struct hostset *hs, int *host_id_array)
{
	struct hostset_collect_host_id_closure c;

	c.host_id_array = host_id_array;
	c.index = 0;
	hostset_foreach(hs, hostset_collect_host_id_each, &c);
}


/*
 * just select randomly		XXX FIXME: needs to improve
 */
static void
select_hosts(int nsrc, int *src, int nresults, int *results)
{
	int i, j;

	assert(nsrc > nresults);
	for (i = 0; i < nresults; i++) {
		j = gfarm_random() % nsrc;
		results[i] = src[j];
		src[j] = src[--nsrc];
	}
}

static gfarm_error_t
hostset_select_n(struct hostset *scope, int n_shortage,
	int *n_targetsp, struct host ***targetsp)
{
	int n_scope = hostset_count_hosts(scope);
	int i;
	struct host **targets;
	int *scope_host_id_array, *target_host_id_array;

	if (n_scope <= n_shortage) { /* just enough or shortage */
		GFARM_MALLOC_ARRAY(targets, n_scope);
		if (targets == NULL)
			return (GFARM_ERR_NO_MEMORY);
		hostset_to_hosts(scope, targets);
		*n_targetsp = n_scope;

	} else { /* too enough targets */

		GFARM_MALLOC_ARRAY(scope_host_id_array, n_scope);
		if (scope_host_id_array == NULL)
			return (GFARM_ERR_NO_MEMORY);

		GFARM_MALLOC_ARRAY(target_host_id_array, n_shortage);
		if (target_host_id_array == NULL) {
			free(scope_host_id_array);
			return (GFARM_ERR_NO_MEMORY);
		}

		GFARM_MALLOC_ARRAY(targets, n_shortage);
		if (targets == NULL) {
			free(target_host_id_array);
			free(scope_host_id_array);
			return (GFARM_ERR_NO_MEMORY);
		}

		hostset_collect_host_id(scope, scope_host_id_array);
		select_hosts(n_scope, scope_host_id_array,
		    n_shortage, target_host_id_array);
		free(scope_host_id_array);

		for (i = 0; i < n_shortage; i++)
			targets[i] = host_id_to_host[target_host_id_array[i]];
		free(target_host_id_array);

		*n_targetsp = n_shortage;
	}
	*targetsp = targets;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * this function breaks `scope', and it cannot be used later.
 * this function does not modify `existing' and `being_removed',
 * and they may be able to be used later.
 *
 * *n_validp > n_desired: already too enough
 * n_desired <= *n_validp + *n_taregetsp: will be enough
 * n_desired >  *n_validp + *n_taregetsp: shortage
 */
gfarm_error_t
hostset_schedule_n_except(
	struct hostset *scope,
	struct hostset *existing, gfarm_time_t grace,
	struct hostset *being_removed,
	int (*filter)(struct host *, void *), void *closure,
	int n_desired,
	int *n_targetsp, struct host ***targetsp, int *n_validp)
{
	int n_shortage, n_up = 0;
	struct hostset *existing_within_scope;

	hostset_except(scope, being_removed);

	existing_within_scope = hostset_dup(scope);
	if (existing_within_scope == NULL)
		return (GFARM_ERR_NO_MEMORY);
	hostset_intersect(existing_within_scope, existing);
	n_up = hostset_count_hosts_up(existing_within_scope, grace);
	hostset_free(existing_within_scope);

	*n_validp = n_up; /* existing valid replicas */

	if (n_desired <= n_up) { /* sufficient */
		*n_targetsp = 0;
		*targetsp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	n_shortage = n_desired - n_up;

	hostset_except(scope, existing);

	hostset_filter(scope, filter, closure);
	return (hostset_select_n(scope, n_shortage, n_targetsp, targetsp));
}

/*
 * hostset cache by fsngroup
 */

#define FSNGROUP_HASHTAB_SIZE	31	/* prime number */

/* protected by giant_lock */
static struct gfarm_hash_table *fsngroup_hostset_hashtab = NULL;

struct hostset_cache_entry {
	struct hostset *hs;
	int nhosts;
};

/* PREREQUISITE: giant_lock */
struct hostset *
hostset_of_fsngroup_alloc(const char *fsngroup, int *n_hostsp)
{
	size_t fsng_size = strlen(fsngroup) + 1;
	struct gfarm_hash_entry *entry;
	struct hostset_cache_entry *hce;
	int created;

	if (fsngroup_hostset_hashtab == NULL) {
		fsngroup_hostset_hashtab = gfarm_hash_table_alloc(
		    FSNGROUP_HASHTAB_SIZE,
		    gfarm_hash_default, gfarm_hash_key_equal_default);
		if (fsngroup_hostset_hashtab == NULL)
			return (NULL); /* GFARM_ERR_NO_MEMORY */
	}

	entry = gfarm_hash_enter(fsngroup_hostset_hashtab,
	    fsngroup, fsng_size, sizeof(*hce), &created);
	if (entry == NULL)
		return (NULL); /* GFARM_ERR_NO_MEMORY */

	hce = gfarm_hash_entry_data(entry);
	if (created) {
		hce->hs = hostset_of_fsngroup_alloc_internal(
		    fsngroup, &hce->nhosts);
		if (hce->hs == NULL) { /* GFARM_ERR_NO_MEMORY */
			if (!gfarm_hash_purge(fsngroup_hostset_hashtab,
			    fsngroup, fsng_size)) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "unexpected error: fsngroup %s doesn't "
				    "exist in fsngroup_hostset_hashtab",
				    fsngroup);
			}
			return (NULL); /* GFARM_ERR_NO_MEMORY */
		}
	}
	if (n_hostsp != NULL)
		*n_hostsp = hce->nhosts;
	return (hostset_dup(hce->hs));
}

/* PREREQUISITE: giant_lock */
static void
hostset_of_fsngroup_cache_purge(const char *fsngroup)
{
	size_t fsng_size = strlen(fsngroup) + 1;
	struct gfarm_hash_entry *entry;
	struct hostset_cache_entry *hce;

	if (fsngroup_hostset_hashtab == NULL)
		return; /* not cached */

	entry = gfarm_hash_lookup(fsngroup_hostset_hashtab,
	    fsngroup, fsng_size);
	if (entry == NULL)
		return;; /* not cached */

	/* uncache */

	hce = gfarm_hash_entry_data(entry);
	hostset_free(hce->hs);

	if (!gfarm_hash_purge(fsngroup_hostset_hashtab, fsngroup, fsng_size)) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "unexpected error: fsngroup %s doesn't "
		    "exist in fsngroup_hostset_hashtab", fsngroup);
	}
}

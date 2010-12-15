/*
 * $Id$
 */

#include <assert.h>
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

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "metadb_common.h"	/* gfarm_host_info_free_except_hostname() */
#include "gfp_xdr.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION */
#include "auth.h"
#include "config.h"

#include "callout.h"
#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "peer.h"
#include "inode.h"
#include "dead_file_copy.h"
#include "back_channel.h"

#define HOST_HASHTAB_SIZE	3079	/* prime number */

static pthread_mutex_t total_disk_mutex = PTHREAD_MUTEX_INITIALIZER;
static gfarm_off_t total_disk_used, total_disk_avail;
static const char total_disk_diag[] = "total_disk";

/* in-core gfarm_host_info */
struct host {
	/*
	 * resources which are protected by the giant_lock()
	 */
	struct gfarm_host_info hi;

	int invalid;	/* set when deleted */

	/*
	 * resources which are protected by the host::back_channel_mutex
	 */
	pthread_mutex_t back_channel_mutex;
	pthread_cond_t ready_to_send, ready_to_receive;

	int can_send, can_receive;

	struct peer *peer;
	int protocol_version;
	volatile int is_active;

#ifdef COMPAT_GFARM_2_3
	/* used by synchronous protocol (i.e. until gfarm-2.3.0) only */
	gfarm_int32_t (*back_channel_result)(void *, void *, size_t);
	void (*back_channel_disconnect)(void *, void *);
	struct peer *back_channel_callback_peer;
	void *back_channel_callback_closure;
#endif

	int status_reply_waiting;
	gfarm_int32_t report_flags;
	struct host_status status;
	struct callout *status_callout;
	gfarm_time_t last_report;
	int status_callout_retry;

	gfarm_time_t busy_time;
};

static struct gfarm_hash_table *host_hashtab = NULL;
static struct gfarm_hash_table *hostalias_hashtab = NULL;

/* NOTE: each entry should be checked by host_is_active(h) too */
#define FOR_ALL_HOSTS(it) \
	for (gfarm_hash_iterator_begin(host_hashtab, (it)); \
	    !gfarm_hash_iterator_is_end(it); \
	     gfarm_hash_iterator_next(it))

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
	h->invalid = 1;
}

static void
host_activate(struct host *h)
{
	h->invalid = 0;
}

static int
host_is_invalidated(struct host *h)
{
	return (h->invalid == 1);
}

static int
host_is_valid_unlocked(struct host *h)
{
	return (!host_is_invalidated(h));
}

int
host_is_active(struct host *h)
{
	int valid;
	static const char diag[] = "host_is_active";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, "back_channel");
	valid = host_is_valid_unlocked(h);
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, "back_channel");
	return (valid);
}

static struct host *
host_lookup_internal(const char *hostname)
{
	return (host_hashtab_lookup(host_hashtab, hostname));
}

struct host *
host_lookup(const char *hostname)
{
	struct host *h = host_lookup_internal(hostname);

	return ((h == NULL || host_is_invalidated(h)) ? NULL : h);
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
		if (!host_is_active(h))
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
	return ((h == NULL || host_is_invalidated(h)) ? NULL : h);
}

/* XXX FIXME missing hostaliases */
gfarm_error_t
host_enter(struct gfarm_host_info *hi, struct host **hpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct host *h;
	struct callout *callout;
	static const char diag[] = "host_enter";

	h = host_lookup_internal(hi->hostname);
	if (h != NULL) {
		if (host_is_invalidated(h)) {
			host_activate(h);
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
	GFARM_MALLOC(h);
	if (h == NULL) {
		gflog_debug(GFARM_MSG_1001546,
			"allocation of host failed");
		callout_free(callout);
		return (GFARM_ERR_NO_MEMORY);
	}
	h->hi = *hi;

	entry = gfarm_hash_enter(host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname), sizeof(struct host *),
	    &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001547,
			"gfarm_hash_enter() failed");
		free(h);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1001548,
			"create entry failed");
		free(h);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	h->peer = NULL;
	h->protocol_version = 0;
	h->can_send = 1;
	h->can_receive = 1;
	h->is_active = 0;
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
	h->status_callout = callout;
	h->status_callout_retry = 0;
	h->last_report = 0;
	h->busy_time = 0;

	gfarm_cond_init(&h->ready_to_send, diag, "ready_to_send");
	gfarm_cond_init(&h->ready_to_receive, diag, "ready_to_receive");
	gfarm_mutex_init(&h->back_channel_mutex, diag, "back_channel");
	*(struct host **)gfarm_hash_entry_data(entry) = h;
	host_activate(h);
	if (hpp != NULL)
		*hpp = h;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX FIXME missing hostaliases */
static gfarm_error_t
host_remove(const char *hostname)
{
	struct host *h = host_lookup(hostname);

	if (h == NULL) {
		gflog_debug(GFARM_MSG_1001549,
		    "host_remove(%s): not exist", hostname);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	/*
	 * do not purge the hash entry.  Instead, invalidate it so
	 * that it can be activated later.
	 */
	host_invalidate(h);

	dead_file_copy_host_removed(h);

	return (GFARM_ERR_NO_ERROR);
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

int
host_port(struct host *h)
{
	return (h->hi.port);
}

int
host_supports_async_protocols(struct host *h)
{
	return (h->protocol_version >= GFS_PROTOCOL_VERSION_V2_4);
}

#ifdef COMPAT_GFARM_2_3
void
host_set_callback(struct host *h, struct peer *peer,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure)
{
	static const char diag[] = "host_set_callback";
	static const char back_channel_diag[] = "back_channel";

	/* XXX FIXME sanity check? */
	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);
	h->back_channel_result = result_callback;
	h->back_channel_disconnect = disconnect_callback;
	h->back_channel_callback_peer = peer;
	h->back_channel_callback_closure = closure;
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
}

int
host_get_result_callback(struct host *h, struct peer *peer,
	gfarm_int32_t (**callbackp)(void *, void *, size_t), void **closurep)
{
	int ok;
	static const char diag[] = "host_get_result_callback";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

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

	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
	return (ok);
}

int
host_get_disconnect_callback(struct host *h,
	void (**callbackp)(void *, void *),
	struct peer **peerp, void **closurep)
{
	int ok;
	static const char diag[] = "host_get_disconnect_callback";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

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

	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
	return (ok);
}

#endif

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 */
static int
host_is_up_unlocked(struct host *h)
{
	return (host_is_valid_unlocked(h) && h->is_active);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: host::back_channel_mutex
 * SLEEPS: no
 */
int
host_is_up(struct host *h)
{
	int up;
	static const char diag[] = "host_is_up";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, "back_channel");
	up = host_is_up_unlocked(h);
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, "back_channel");
	return (up);
}

int
host_is_disk_available(struct host *h, gfarm_off_t size)
{
	gfarm_off_t avail, minfree = gfarm_get_minimum_free_disk_space();
	static const char diag[] = "host_get_disk_avail";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, "back_channel_mutex");

	if (host_is_up_unlocked(h))
		avail = h->status.disk_avail * 1024;
	else
		avail = 0;
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, "back_channel_mutex");

	if (minfree < size)
		minfree = size;
	return (avail >= minfree);
}

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 */
static int
host_is_unresponsive(struct host *host, gfarm_int64_t now, const char *diag)
{
	int unresponsive = 0;

	if (!host->is_active || host->invalid)
		;
	else if (host->can_send)
		host->busy_time = 0;
	else if (host->busy_time != 0 &&
	    now > host->busy_time + gfarm_metadb_heartbeat_interval) {
		gflog_warning(GFARM_MSG_1002213,
		    "host %s: too long busy since %lld",
		    host_name(host), (long long)host->busy_time);
		unresponsive = 1;
	}

	return (unresponsive);
}

void
host_peer_busy(struct host *host)
{
	struct peer *unresponsive_peer = NULL;
	static const char diag[] = "host_peer_busy";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);
	if (!host->is_active || host->invalid)
		;
	else if (host->busy_time == 0)
		host->busy_time = time(NULL);
	else if (host_is_unresponsive(host, time(NULL), diag))
		unresponsive_peer = host->peer;
	gfarm_mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);

	if (unresponsive_peer != NULL) {
		gflog_error(GFARM_MSG_1002419,
		    "back_channel(%s): disconnecting: busy at sending",
		    host_name(host));
		host_disconnect_request(host, unresponsive_peer);
	}
}

void
host_peer_unbusy(struct host *host)
{
	static const char diag[] = "host_peer_unbusy";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);
	host->busy_time = 0;
	gfarm_mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);
}

int
host_check_busy(struct host *host, gfarm_int64_t now)
{
	int busy = 0;
	struct peer *unresponsive_peer = NULL;
	static const char diag[] = "host_check_busy";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);

	if (!host->is_active || host->invalid)
		busy = 1;
	else if (host_is_unresponsive(host, now, diag))
		unresponsive_peer = host->peer;

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);

	if (unresponsive_peer != NULL) {
		gflog_error(GFARM_MSG_1002420,
		    "back_channel(%s): disconnecting: busy during queue scan",
		    host_name(host));
		host_disconnect_request(host, unresponsive_peer);
	}

	return (busy || unresponsive_peer != NULL);
}

struct callout *
host_status_callout(struct host *h)
{
	return (h->status_callout);
}

struct peer *
host_peer(struct host *h)
{
	struct peer *peer;
	static const char diag[] = "host_sender_trylock";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, "back_channel");
	peer = h->peer;
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, "back_channel");
	return (peer);
}

gfarm_error_t
host_sender_trylock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	static const char diag[] = "host_sender_trylock";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	if (!host_is_up_unlocked(host)) {
		e = GFARM_ERR_CONNECTION_ABORTED;
	} else if (host->can_send) {
		host->can_send = 0;
		host->busy_time = 0;
		peer_add_ref(host->peer);
		*peerp = host->peer;
		e = GFARM_ERR_NO_ERROR;
	} else {
		e = GFARM_ERR_DEVICE_BUSY;
	}

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

gfarm_error_t
host_sender_lock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	struct peer *peer0;
	static const char diag[] = "host_sender_lock";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	for (;;) {
		if (!host_is_up_unlocked(host)) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		if (host->can_send) {
			host->can_send = 0;
			host->busy_time = 0;
			peer_add_ref(host->peer);
			*peerp = host->peer;
			e = GFARM_ERR_NO_ERROR;
			break;
		}
		peer0 = host->peer;
		gfarm_cond_wait(&host->ready_to_send, &host->back_channel_mutex,
		    diag, "ready_to_send");
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

void
host_sender_unlock(struct host *host, struct peer *peer)
{
	static const char diag[] = "host_sender_unlock";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	if (peer == host->peer) {
		host->can_send = 1;
		host->busy_time = 0;
	}
	peer_del_ref(peer);
	gfarm_cond_signal(&host->ready_to_send, diag, "ready_to_send");

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

gfarm_error_t
host_receiver_lock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	struct peer *peer0;
	static const char diag[] = "host_receiver_lock";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	for (;;) {
		if (!host_is_up_unlocked(host)) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		if (host->can_receive) {
			host->can_receive = 0;
			peer_add_ref(host->peer);
			*peerp = host->peer;
			e = GFARM_ERR_NO_ERROR;
			break;
		}
		/* may happen at gfsd restart? */
		peer0 = host->peer;
		gflog_error(GFARM_MSG_1002318,
		    "waiting for host_receiver_lock: maybe gfsd restarted?");
		gfarm_cond_wait(&host->ready_to_receive,
		    &host->back_channel_mutex, diag, "ready_to_receive");
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

void
host_receiver_unlock(struct host *host, struct peer *peer)
{
	static const char diag[] = "host_receiver_unlock";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	if (peer == host->peer) {
		host->can_receive = 1;
	}
	peer_del_ref(peer);
	gfarm_cond_signal(&host->ready_to_receive, diag, "ready_to_receive");

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 *
 * should be called after host->is_active = 0;
 */
static void
host_break_locks(struct host *host)
{
	static const char diag[] = "host_break_locks";

	gfarm_cond_broadcast(&host->ready_to_send, diag, "ready_to_send");
	gfarm_cond_broadcast(&host->ready_to_receive, diag, "ready_to_receive");
}

int
host_status_callout_retry(struct host *host)
{
	long interval;
	int ok;
	static const char diag[] = "host_status_callout_retry";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	++host->status_callout_retry;
	interval = 1 << host->status_callout_retry;
	ok = (interval <= gfarm_metadb_heartbeat_interval);

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	if (ok) {
		callout_schedule(host->status_callout, interval);
		gflog_debug(GFARM_MSG_1002215,
		    "%s(%s): retrying in %ld seconds",
		    diag, host_name(host), interval);
	}
	return (ok);
}

void
host_status_reply_waiting(struct host *host)
{
	static const char diag[] = "host_status_reply_waiting";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	host->status_reply_waiting = 1;

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

int
host_status_reply_is_waiting(struct host *host)
{
	int waiting;
	static const char diag[] = "host_status_reply_waiting";

	gfarm_mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	waiting = host->status_reply_waiting;

	gfarm_mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

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
	gfarm_uint64_t new_used, gfarm_uint64_t new_avail)
{
	static const char diag[] = "host_total_disk_update";

	gfarm_mutex_lock(&total_disk_mutex, diag, total_disk_diag);
	total_disk_used += new_used - old_used;
	total_disk_avail += new_avail - old_avail;
	gfarm_mutex_unlock(&total_disk_mutex, diag, total_disk_diag);
}

void
host_status_update(struct host *host, struct host_status *status)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;

	gfarm_mutex_lock(&host->back_channel_mutex, "host back_channel",
	    "status_update");

	host->status_reply_waiting = 0;
	host->status_callout_retry = 0;

	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = host->status.disk_used;
		saved_avail = host->status.disk_avail;
	}

	host->last_report = time(NULL);
	host->report_flags =
		GFM_PROTO_SCHED_FLAG_HOST_AVAIL |
		GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL;
	host->status = *status;

	gfarm_mutex_unlock(&host->back_channel_mutex, "host back_channel",
	    "status_update");

	host_total_disk_update(saved_used, saved_avail,
	    status->disk_used, status->disk_avail);
}

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 */
static void
host_status_disable_unlocked(struct host *host,
	gfarm_uint64_t *saved_usedp, gfarm_uint64_t *saved_availp)
{
	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		*saved_usedp = host->status.disk_used;
		*saved_availp = host->status.disk_avail;
	} else {
		*saved_usedp = 0;
		*saved_availp = 0;
	}

	host->report_flags = 0;
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: host::back_channel_mutex, dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe (see the comment of dead_file_copy_host_becomes_up())
 *	but host::back_channel_mutex, dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 */
void
host_peer_set(struct host *h, struct peer *p, int version)
{
	static const char diag[] = "host_peer_set";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	h->can_send = 1;
	h->can_receive = 1;

	h->peer = p;
	h->protocol_version = version;
#ifdef COMPAT_GFARM_2_3
	h->back_channel_result = NULL;
	h->back_channel_disconnect = NULL;
	h->back_channel_callback_peer = NULL;
	h->back_channel_callback_closure = NULL;
#endif
	h->is_active = 1;
	h->status_reply_waiting = 0;
	h->status_callout_retry = 0;
	h->busy_time = 0;

	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);

	dead_file_copy_host_becomes_up(h);
}

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: removal_pendingq.mutex, host_busyq.mutex
 * SLEEPS: no
 */
static void
host_peer_unset(struct host *h)
{
	h->peer = NULL;
	h->protocol_version = 0;
	h->is_active = 0;

	callout_stop(h->status_callout);

	host_break_locks(h);
}

/* giant_lock should be held before calling this */
void
host_disconnect(struct host *h, struct peer *peer)
{
#if 0
	/*
	 * commented out,
	 * not to sleep while holding host::back_channel_mutex
	 */

	int disabled = 0;
	gfarm_uint64_t saved_used, saved_avail;
	static const char diag[] = "host_disconnect";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	if (h->is_active && (peer == h->peer || peer == NULL)) {
		peer_record_protocol_error(h->peer);

		if (h->can_send && h->can_receive) {
			/*
			 * NOTE: this shouldn't need db_begin()/db_end()
			 * at least for now,
			 * because only externalized descriptor needs the calls.
			 */
			peer_free(h->peer);
		} else
			peer_free_request(h->peer);

		host_peer_unset(h);

		host_status_disable_unlocked(&saved_used, &saved_avail);

		disabled = 1;
	}

	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);

	if (disabled) {
		host_total_disk_update(saved_used, saved_avail, 0, 0);
		dead_file_copy_host_becomes_down(h);
	}
#else
	host_disconnect_request(h, peer);
#endif
}

void
host_disconnect_request(struct host *h, struct peer *peer)
{
	int disabled = 0;
	gfarm_uint64_t saved_used, saved_avail;
	static const char diag[] = "host_disconnect_request";
	static const char back_channel_diag[] = "back_channel";

	gfarm_mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	if (h->is_active && (peer == h->peer || peer == NULL)) {
		peer_record_protocol_error(h->peer);

		peer_free_request(h->peer);

		host_peer_unset(h);

		host_status_disable_unlocked(h, &saved_used, &saved_avail);

		disabled = 1;
	}

	gfarm_mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);

	if (disabled) {
		host_total_disk_update(saved_used, saved_avail, 0, 0);
		dead_file_copy_host_becomes_down(h);
	}
}

/* only file_replicating_new() is allowed to call this routine */
gfarm_error_t
host_replicating_new(struct host *dst, struct file_replicating **frp)
{
	if (dst->peer == NULL)
		return (GFARM_ERR_NO_ROUTE_TO_HOST);
	return (peer_replicating_new(dst->peer, dst, frp));
}

static int
host_order(const void *a, const void *b)
{
	const struct host *const *h1 = a, *const *h2 = b;

	if (*h1 < *h2)
		return (-1);
	else if (*h1 > *h2)
		return (1);
	else
		return (0);			
}

static void
host_sort(int nhosts, struct host **hosts)
{
	if (nhosts <= 0) /* 2nd parameter of qsort(3) is unsigned */
		return;

	qsort(hosts, nhosts, sizeof(*hosts), host_order);
}

/*
 * remove duplicated hosts.
 * NOTE: this function assumes that hosts[] is sorted by host_order()
 */
static int
host_unique(int nhosts, struct host **hosts)
{
	int l, r;

	if (nhosts <= 0)
		return (0);
	l = 0;
	r = l + 1;
	for (;;) {
		for (;;) {
			if (r >= nhosts)
				return (l + 1);
			if (hosts[l] != hosts[r])
				break;
			r++;
		}
		++l;
		hosts[l] = hosts[r]; /* maybe l == r here */
		++r;
	}
}

int
host_unique_sort(int nhosts, struct host **hosts)
{
	host_sort(nhosts, hosts);
	return (host_unique(nhosts, hosts));
}

/* NOTE: both hosts[] and excludings[] must be host_unique_sort()ed */
static gfarm_error_t
host_exclude(int *nhostsp, struct host **hosts,
	int n_excludings, struct host **excludings,
	int (*filter)(struct host *, void *), void *closure)
{
	int cmp, i, j, nhosts = *nhostsp;
	unsigned char *candidates;

	GFARM_MALLOC_ARRAY(candidates, nhosts > 0 ? nhosts : 1);
	if (candidates == NULL)
		return (GFARM_ERR_NO_MEMORY);

	memset(candidates, 1, nhosts);

	if (n_excludings > 0) {
		/* exclude excludings[] from hosts[] */
		i = j = 0;
		while (i < nhosts && j < n_excludings) {
			cmp = host_order(&hosts[i], &excludings[j]);
			if (cmp < 0) {
				i++;
			} else if (cmp == 0) {
				candidates[i++] = 0;
			} else if (cmp > 0) {
				j++;
			}
		}
	}

	if (filter != NULL) {
		for (i = 0; i < nhosts; i++) {
			if (!candidates[i])
				continue;
			if (!filter(hosts[i], closure))
				candidates[i] = 0;
		}
	}

	/* compaction */
	j = 0;
	for (i = 0; i < nhosts; i++) {
		if (candidates[i])
			hosts[j++] = hosts[i];
	}
	free(candidates);

	*nhostsp = j;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_is_disk_available_filter(struct host *host, void *closure)
{
	gfarm_off_t *sizep = closure;

	return (host_is_disk_available(host, *sizep));
}

static gfarm_error_t
host_array_alloc(int *nhostsp, struct host ***hostsp)
{
	int i, nhosts;
	struct host **hosts;
	struct gfarm_hash_iterator it;

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		host_iterator_access(&it);
		++nhosts;
	}

	GFARM_MALLOC_ARRAY(hosts, nhosts > 0 ? nhosts : 1);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	i = 0;
	FOR_ALL_HOSTS(&it) {
		if (i >= nhosts) /* always false due to giant_lock */
			break;
		hosts[i++] = host_iterator_access(&it);
	}
	*nhostsp = i;
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * just select randomly		XXX FIXME: needs to improve
 */
static void
select_hosts(int nhosts, struct host **hosts,
	int nresults, struct host **results)
{
	int i, j;

	assert(nhosts > nresults);
	for (i = 0; i < nresults; i++) {
		j = gfarm_random() % nhosts;
		results[i] = hosts[j];
		hosts[j] = hosts[--nhosts];
	}
}

/*
 * this function sorts excludings[] as a side effect.
 * but caller shouldn't depend the fact.
 */
gfarm_error_t
host_schedule_except(int n_excludings, struct host **excludings,
	int (*filter)(struct host *, void *), void *closure,
	int n_shortage, int *n_new_targetsp, struct host **new_targets)
{
	gfarm_error_t e;
	struct host **hosts;
	int nhosts;
	int i;

	e = host_array_alloc(&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	n_excludings = host_unique_sort(n_excludings, excludings);
	nhosts = host_unique_sort(nhosts, hosts);

	e = host_exclude(&nhosts, hosts, n_excludings, excludings,
	    filter, closure);
	if (e != GFARM_ERR_NO_ERROR) {
		free(hosts);
		return (e);
	}

	if (nhosts <= n_shortage) {
		for (i = 0; i < nhosts; i++)
			new_targets[i] = hosts[i];
		*n_new_targetsp = nhosts;
	} else {
		select_hosts(nhosts, hosts, n_shortage, new_targets);
		*n_new_targetsp = n_shortage;
	}

	free(hosts);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * this function sorts excludings[] as a side effect.
 * but caller shouldn't depend on the side effect.
 */
gfarm_error_t
host_schedule_all_except(int n_excludings, struct host **excludings,
	int (*filter)(struct host *, void *), void *closure,
	gfarm_int32_t *nhostsp, struct host ***hostsp)
{
	gfarm_error_t e;
	int nhosts;
	struct host **hosts;

	e = host_array_alloc(&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	host_unique_sort(n_excludings, excludings);
	host_unique_sort(nhosts, hosts);

	e = host_exclude(&nhosts, hosts, n_excludings, excludings,
	    filter, closure);
	if (e == GFARM_ERR_NO_ERROR) {
		*nhostsp = nhosts;
		*hostsp = hosts;
	} else {
		free(hosts);
	}
	return (e);
}

/* give the top priority to the local host */
int
host_schedule_one_except(struct peer *peer,
	int n_excludings, struct host **excludings,
	int (*filter)(struct host *, void *), void *closure,
	gfarm_int32_t *np, struct host ***hostsp, gfarm_error_t *errorp)
{
	struct host **hosts, *h = peer_get_host(peer);
	int i;

	if (h != NULL && (*filter)(h, closure)) {

		/* is the host excluded? */
		for (i = 0; i < n_excludings; i++) {
			if (h == excludings[i])
				return (0); /* not scheduled */
		}

		GFARM_MALLOC_ARRAY(hosts, 1);
		if (hosts == NULL) {
			*errorp = GFARM_ERR_NO_MEMORY;
			return (1); /* scheduled, sort of */
		}
		hosts[0] = h;
		*np = 1;
		*hostsp = hosts;
		*errorp = GFARM_ERR_NO_ERROR;
		return (1); /* scheduled */
	}
	return (0); /* not scheduled */
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
host_add_one(void *closure, struct gfarm_host_info *hi)
{
	gfarm_error_t e = host_enter(hi, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000266,
		    "host_add_one: %s", gfarm_error_string(e));
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
 * LOCKS: host::back_channel_mutex
 * SLEEPS: maybe
 *	but host::back_channel_mutex won't be blocked while sleeping.
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
	 * note that the result of host_is_active() may vary at each call.
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
			if (host_is_active(h) &&
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
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002218,
		    "gfm_server_put_reply(%s) failed: %s",
		    diag, gfarm_error_string(e2));
	} else if (e == GFARM_ERR_NO_ERROR) {
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
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001558,
			"gfm_server_get_request() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL)
		no_memory = 1;
	for (i = 0; i < nhosts; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &host);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1001559,
				"gfp_xdr_recv(host) failed: %s",
				gfarm_error_string(e));
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (hosts != NULL) {
				for (j = 0; j < i; j++) {
					if (hosts[j] != NULL)
						free(hosts[j]);
				}
				free(hosts);
			}
			return (e);
		}
		if (hosts == NULL) {
			free(host);
		} else {
			hosts[i] = host;
		}
	}
	if (no_memory)
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_MEMORY, "");
	else
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "");
	if (no_memory || e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001560,
			"gfp_xdr_recv(host) failed: %s",
			gfarm_error_string(e));
		if (hosts != NULL) {
			for (i = 0; i < nhosts; i++) {
				if (hosts[i] != NULL)
					free(hosts[i]);
			}
			free(hosts);
		}
		return (e);
	}
	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < nhosts; i++) {
		h = (*lookup)(hosts[i]);
		if (h == NULL) {
			if (debug_mode)
				gflog_info(GFARM_MSG_1000270,
				    "host lookup <%s>: failed",
				    hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_UNKNOWN_HOST, "");
		} else {
			if (debug_mode)
				gflog_info(GFARM_MSG_1000271,
				    "host lookup <%s>: ok", hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = host_info_send(client, h);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001561,
				"error occurred during process: %s",
				gfarm_error_string(e));
			break;
		}
	}
	for (i = 0; i < nhosts; i++)
		free(hosts[i]);
	free(hosts);
	giant_unlock();
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

static gfarm_error_t
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
		free(h->hi.architecture);
		h->hi.architecture = hi.architecture;
		h->hi.ncpu = hi.ncpu;
		h->hi.port = hi.port;
		h->hi.flags = hi.flags;
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
	host_disconnect(host, NULL);

	if ((e = host_remove(hostname)) == GFARM_ERR_NO_ERROR) {
		e2 = db_host_remove(hostname);
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000272,
			    "%s: db_host_remove: %s",
			    diag, gfarm_error_string(e2));
	}
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
	gfarm_time_t last_report;
	gfarm_int32_t report_flags;

	gfarm_mutex_lock(&h->back_channel_mutex, diag, "schedule_reply");
	status = h->status;
	last_report = h->last_report;
	report_flags = h->report_flags;
	gfarm_mutex_unlock(&h->back_channel_mutex, diag, "schedule_reply");
	return (gfp_xdr_send(peer_get_conn(peer), "siiillllii",
	    h->hi.hostname, h->hi.port, h->hi.ncpu,
	    (gfarm_int32_t)(status.loadavg_1min * GFM_PROTO_LOADAVG_FSCALE),
	    last_report,
	    status.disk_used, status.disk_avail,
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
	static const char diag[] = "GFM_PROTO_STATFS";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	files = inode_total_num();
	gfarm_mutex_lock(&total_disk_mutex, diag, total_disk_diag);
	used = total_disk_used;
	avail = total_disk_avail;
	gfarm_mutex_unlock(&total_disk_mutex, diag, total_disk_diag);

	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "lll",
		    used, avail, files));
}

#endif /* TEST */

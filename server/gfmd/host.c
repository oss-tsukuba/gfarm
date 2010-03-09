/*
 * $Id$
 */

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

#include "metadb_common.h"	/* gfarm_host_info_free_except_hostname() */
#include "gfp_xdr.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION */
#include "auth.h"
#include "config.h"

#include "thrsubr.h"
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
	pthread_cond_t ready_to_send;

	int can_send, can_receive;

	struct peer *peer;
	int protocol_version;
	volatile int is_active;

	/* used by synchronous protocol (i.e. until gfarm-2.3.0) only */
	gfarm_int32_t (*back_channel_result)(void *, size_t);
	void *back_channel_closure;

	int status_reply_waiting;
	gfarm_int32_t report_flags;
	struct host_status status;
	struct callout *status_callout;
	gfarm_time_t last_report;
	int status_callout_retry;

	gfarm_time_t busy_time;

	/*
	 * resources which are protected by the host::replication_mutex
	 */
	pthread_mutex_t replication_mutex;
	struct file_replicating replicating_inodes; /* dummy header */
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

	mutex_lock(&h->back_channel_mutex, diag, "back_channel");
	valid = host_is_valid_unlocked(h);
	mutex_unlock(&h->back_channel_mutex, diag, "back_channel");
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
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no memory for host %s",
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
	h->back_channel_result = NULL;
	h->back_channel_closure = NULL;
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

	/* make circular list `replicating_inodes' empty */
	h->replicating_inodes.prev_inode =
	h->replicating_inodes.next_inode = &h->replicating_inodes;

	cond_init(&h->ready_to_send, diag, "able_to_send");
	mutex_init(&h->back_channel_mutex, diag, "back_channel");
	mutex_init(&h->replication_mutex, diag, "replication");
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

void
host_set_callback(struct host *h,
	gfarm_int32_t (*callback)(void *, size_t), void *closure)
{
	static const char diag[] = "host_set_callback";
	static const char back_channel_diag[] = "back_channel";

	/* XXX FIXME sanity check? */
	mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);
	h->back_channel_result = callback;
	h->back_channel_closure = closure;
	mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
}

int
host_get_callback(struct host *h,
	gfarm_int32_t (**callbackp)(void *, size_t), void **closurep)
{
	int ok;
	static const char diag[] = "host_get_callback";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	if (h->back_channel_result == NULL) {
		ok = 0;
	} else {
		*callbackp = h->back_channel_result;
		*closurep = h->back_channel_closure;
		ok = 1;
	}

	mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
	return (ok);
}

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

	mutex_lock(&h->back_channel_mutex, diag, "back_channel");
	up = host_is_up_unlocked(h);
	mutex_unlock(&h->back_channel_mutex, diag, "back_channel");
	return (up);
}

int
host_is_disk_available(struct host *h, gfarm_off_t size)
{
	gfarm_off_t avail;
	static const char diag[] = "host_get_disk_avail";

	mutex_lock(&h->back_channel_mutex, diag, "back_channel_mutex");

	if (host_is_up_unlocked(h))
		avail = h->status.disk_avail * 1024;
	else
		avail = 0;
	mutex_unlock(&h->back_channel_mutex, diag, "back_channel_mutex");

	if (size <= 0)
		size = gfarm_get_minimum_free_disk_space();
	return (avail >= size);
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
		gflog_warning(GFARM_MSG_UNFIXED,
		    "host %s: too long busy since %lld",
		    host_name(host), (long long)host->busy_time);
		unresponsive = 1;
	}

	return (unresponsive);
}

void
host_peer_busy(struct host *host)
{
	int unresponsive = 0;
	static const char diag[] = "host_peer_busy";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);
	if (!host->is_active || host->invalid)
		;
	else if (host->busy_time == 0)
		host->busy_time = time(NULL);
	else
		unresponsive = host_is_unresponsive(host, time(NULL), diag);
	mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);

	if (unresponsive)
		host_disconnect_request(host);
}

void
host_peer_unbusy(struct host *host)
{
	static const char diag[] = "host_peer_unbusy";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);
	host->busy_time = 0;
	mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);
}

int
host_check_busy(struct host *host, gfarm_int64_t now)
{
	int busy = 0, unresponsive = 0;
	static const char diag[] = "host_check_busy";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&host->back_channel_mutex, diag, back_channel_diag);

	if (!host->is_active || host->invalid)
		busy = 1;
	else
		unresponsive = host_is_unresponsive(host, now, diag);

	mutex_unlock(&host->back_channel_mutex, diag, back_channel_diag);

	if (unresponsive)
		host_disconnect_request(host);

	return (busy || unresponsive);
}

struct callout *
host_status_callout(struct host *h)
{
	return (h->status_callout);
}

struct peer *
host_peer(struct host *h)
{
	return (h->peer);
}

gfarm_error_t
host_sender_trylock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	static const char diag[] = "host_sender_trylock";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

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

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

gfarm_error_t
host_sender_lock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	struct peer *peer0;
	static const char diag[] = "host_sender_lock";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

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
		cond_wait(&host->ready_to_send, &host->back_channel_mutex,
		    diag, "ready_to_send");
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

void
host_sender_unlock(struct host *host)
{
	static const char diag[] = "host_sender_unlock";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	host->can_send = 1;
	host->busy_time = 0;
	peer_del_ref(host->peer);
	cond_signal(&host->ready_to_send, diag, "ready_to_send");

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

gfarm_error_t
host_receiver_lock(struct host *host, struct peer **peerp)
{
	gfarm_error_t e;
	static const char diag[] = "host_receiver_lock";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	if (!host_is_up_unlocked(host)) {
		e = GFARM_ERR_CONNECTION_ABORTED;
	} else if (host->can_receive) {
		host->can_receive = 0;
		peer_add_ref(host->peer);
		*peerp = host->peer;
		e = GFARM_ERR_NO_ERROR;
	} else { /* shound't happen */
		gflog_fatal(GFARM_MSG_UNFIXED, 
		    "%s: host_receiver_lock(%s): assertion failure",
		    diag, host_name(host));
		e = GFARM_ERR_DEVICE_BUSY;
	}

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (e);
}

void
host_receiver_unlock(struct host *host)
{
	static const char diag[] = "host_receiver_unlock";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	host->can_receive = 1;
	peer_del_ref(host->peer);

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

/*
 * PREREQUISITE: host::back_channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 *
 * should be called after host->is_active = 0;
 */
static void
host_sender_break_locks(struct host *host)
{
	static const char diag[] = "host_sender_break_locks";

	cond_broadcast(&host->ready_to_send, diag, "ready_to_send");
}

int
host_status_callout_retry(struct host *host)
{
	long interval;
	int ok;
	static const char diag[] = "host_status_callout_retry";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	++host->status_callout_retry;
	interval = 1 << host->status_callout_retry;
	ok = (interval <= gfarm_metadb_heartbeat_interval);

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	if (ok) {
		callout_schedule(host->status_callout, interval);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s(%s): retrying in %ld seconds",
		    diag, host_name(host), interval);
	}
	return (ok);
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

	mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	h->can_send = 1;
	h->can_receive = 1;

	h->peer = p;
	h->protocol_version = version;
	h->back_channel_result = NULL;
	h->back_channel_closure = NULL;
	h->is_active = 1;
	h->status_reply_waiting = 0;
	h->status_callout_retry = 0;
	h->busy_time = 0;

	mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);

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

	dead_file_copy_host_becomes_down(h);
	/* we won't remove h->replicating_inodes list at least for now. */

	/*
	 * NOTE:
	 * we don't remove of h->replicating_inodes list,
	 * just continue the replication after next call of host_peer_set().
	 */

	host_sender_break_locks(h);
}

/* giant_lock should be held before calling this */
void
host_disconnect(struct host *h)
{
#if 0
	/*
	 * commented out,
	 * not to sleep while holding host::back_channel_mutex
	 */

	static const char diag[] = "host_disconnect";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	if (h->is_active) {
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
	}

	mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
#else
	host_disconnect_request(h);
#endif
}

void
host_disconnect_request(struct host *h)
{
	static const char diag[] = "host_disconnect_request";
	static const char back_channel_diag[] = "back_channel";

	mutex_lock(&h->back_channel_mutex, diag, back_channel_diag);

	if (h->is_active) {
		peer_record_protocol_error(h->peer);

		peer_free_request(h->peer);

		host_peer_unset(h);
	}

	mutex_unlock(&h->back_channel_mutex, diag, back_channel_diag);
}

/* only file_replicating_new() is allowed to call this routine */
struct file_replicating *
host_replicating_new(struct host *dst)
{
	struct file_replicating *fr;
	static const char diag[] = "host_replicating_new";
	static const char replication_diag[] = "replication";

	GFARM_MALLOC(fr);
	if (fr == NULL)
		return (NULL);

	fr->dst = dst;
	fr->handle = -1;

	/* the followings should be initialized by inode_replicating() */
	fr->prev_host = fr;
	fr->next_host = fr;

	mutex_lock(&dst->replication_mutex, diag, replication_diag);
	fr->prev_inode = &dst->replicating_inodes;
	fr->next_inode = dst->replicating_inodes.next_inode;
	dst->replicating_inodes.next_inode = fr;
	fr->next_inode->prev_inode = fr;
	mutex_unlock(&dst->replication_mutex, diag, replication_diag);
	return (fr);
}

/* only file_replicating_free() is allowed to call this routine */
void
host_replicating_free(struct file_replicating *fr)
{
	struct host *dst = fr->dst;
	static const char diag[] = "host_replicating_free";
	static const char replication_diag[] = "replication";

	mutex_lock(&dst->replication_mutex, diag, replication_diag);
	fr->prev_inode->next_inode = fr->next_inode;
	fr->next_inode->prev_inode = fr->prev_inode;
	mutex_unlock(&dst->replication_mutex, diag, replication_diag);
	free(fr);
}

void
file_replicating_set_handle(struct file_replicating *fr, gfarm_int64_t handle)
{
	fr->handle = handle;
}

gfarm_int64_t
file_replicating_get_handle(struct file_replicating *fr)
{
	return (fr->handle);
}

gfarm_error_t
host_replicated(struct host *host, gfarm_ino_t ino, gfarm_int64_t gen, 
	gfarm_int64_t handle,
	gfarm_int32_t src_errcode, gfarm_int32_t dst_errcode, gfarm_off_t size)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	static const char diag[] = "host_replicated";
	static const char replication_diag[] = "replication";

	mutex_lock(&host->replication_mutex, diag, replication_diag);

	if (handle == -1) {
		for (fr = host->replicating_inodes.next_inode;
		    fr != &host->replicating_inodes; fr = fr->next_inode) {
			if (fr->igen == gen &&
			    inode_get_number(fr->inode) == ino)
				break;
		}
	} else {
		for (fr = host->replicating_inodes.next_inode;
		    fr != &host->replicating_inodes; fr = fr->next_inode) {
			if (fr->handle == handle &&
			    fr->igen == gen &&
			    inode_get_number(fr->inode) == ino)
				break;
		}
	}
	if (fr == &host->replicating_inodes)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = GFARM_ERR_NO_ERROR;
	mutex_unlock(&host->replication_mutex, diag, replication_diag);

	if (e == GFARM_ERR_NO_ERROR)
		e = inode_replicated(fr, src_errcode, dst_errcode, size);
	return (e);
}

void
host_status_reply_waiting(struct host *host)
{
	static const char diag[] = "host_status_reply_waiting";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	host->status_reply_waiting = 1;

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");
}

int
host_status_reply_is_waiting(struct host *host)
{
	int waiting;
	static const char diag[] = "host_status_reply_waiting";

	mutex_lock(&host->back_channel_mutex, diag, "back_channel");

	waiting = host->status_reply_waiting;

	mutex_unlock(&host->back_channel_mutex, diag, "back_channel");

	return (waiting);
}

void
host_status_update(struct host *host, struct host_status *status)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;

	mutex_lock(&host->back_channel_mutex, "host back_channel",
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

	mutex_unlock(&host->back_channel_mutex, "host back_channel",
	    "status_update");

	pthread_mutex_lock(&total_disk_mutex);
	total_disk_used += status->disk_used - saved_used;
	total_disk_avail += status->disk_avail - saved_avail;
	pthread_mutex_unlock(&total_disk_mutex);
}

void
host_status_disable(struct host *host)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;

	mutex_lock(&host->back_channel_mutex, "host back_channel",
	    "status_disable");

	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = host->status.disk_used;
		saved_avail = host->status.disk_avail;
	}

	host->report_flags = 0;

	mutex_unlock(&host->back_channel_mutex, "host back_channel",
	    "status_disable");

	pthread_mutex_lock(&total_disk_mutex);
	total_disk_used -= saved_used;
	total_disk_avail -= saved_avail;
	pthread_mutex_unlock(&total_disk_mutex);
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

gfarm_error_t
gfm_server_host_info_get_common(struct peer *peer,
	int (*filter)(struct host *, void *), void *closure, const char *diag)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e, e2;
	gfarm_int32_t nhosts, nmatch, i, answered;
	struct gfarm_hash_iterator it;
	struct host *h;
	char *match;

	/* XXX FIXME too long giant lock */
	giant_lock();

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
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: no memory for %d hosts", diag, nhosts);
	} else {
		i = 0;
		FOR_ALL_HOSTS(&it) {
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
		if (filter == NULL || nmatch > 0) {
			e = GFARM_ERR_NO_ERROR;
		} else {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: no matching host", diag);
		}
	}
	e2 = gfm_server_put_reply(peer, diag, e, "i", nmatch);
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfm_server_put_reply(%s) failed: %s",
		    diag, gfarm_error_string(e2));
	} else if (e == GFARM_ERR_NO_ERROR) {
		i = answered = 0;
		FOR_ALL_HOSTS(&it) {
			if (i >= nhosts || answered >= nmatch)
				break;
			h = host_iterator_access(&it);
			if (match[i]) {
				e2 = host_info_send(client, h);
				if (e2 != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_UNFIXED,
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

	giant_unlock();
	return (e2);
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
			if (host == NULL)
				no_memory = 1;
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

gfarm_error_t
gfm_server_host_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user = peer_get_user(peer);
	char *hostname, *architecture;
	gfarm_int32_t ncpu, port, flags;
	struct gfarm_host_info hi;
	static const char diag[] = "GFM_PROTO_HOST_INFO_SET";

	e = gfm_server_get_request(peer, diag, "ssiii",
	    &hostname, &architecture, &ncpu, &port, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001562,
			"host_info_set request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hostname);
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_1001563,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (host_lookup(hostname) != NULL) {
		gflog_debug(GFARM_MSG_1001564,
			"host already exists");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else {
		hi.hostname = hostname;
		hi.port = port;
		/* XXX FIXME missing hostaliases */
		hi.nhostaliases = 0;
		hi.hostaliases = NULL;
		hi.architecture = architecture;
		hi.ncpu = ncpu;
		hi.flags = flags;
		e = host_enter(&hi, NULL);
		if (e == GFARM_ERR_NO_ERROR) {
			e = db_host_add(&hi);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001565,
					"db_host_add() failed: %s",
					gfarm_error_string(e));
				host_remove(hostname);
				hostname = architecture = NULL;
			}
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001566,
			"error occurred during process: %s",
			gfarm_error_string(e));
		if (hostname != NULL)
			free(hostname);
		if (architecture != NULL)
			free(architecture);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_host_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct gfarm_host_info hi;
	struct host *h;
	int needs_free = 0;
	static const char diag[] = "GFM_PROTO_HOST_INFO_MODIFY";

	e = gfm_server_get_request(peer, diag, "ssiii",
	    &hi.hostname, &hi.architecture, &hi.ncpu, &hi.port, &hi.flags);
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
	host_disconnect(host);

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

/* called from inode.c:inode_schedule_file_reply() */

gfarm_error_t
host_schedule_reply_n(struct peer *peer, gfarm_int32_t n, const char *diag)
{
	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "i", n));
}

gfarm_error_t
host_schedule_reply(struct host *h, struct peer *peer, const char *diag)
{
	struct host_status status;
	gfarm_time_t last_report;
	gfarm_int32_t report_flags;

	mutex_lock(&h->back_channel_mutex, "host back_channel",
	    "schedule_reply");
	status = h->status;
	last_report = h->last_report;
	report_flags = h->report_flags;
	mutex_unlock(&h->back_channel_mutex, "host back_channel",
	    "schedule_reply");
	return (gfp_xdr_send(peer_get_conn(peer), "siiillllii",
	    h->hi.hostname, h->hi.port, h->hi.ncpu,
	    (gfarm_int32_t)(status.loadavg_1min * GFM_PROTO_LOADAVG_FSCALE),
	    last_report,
	    status.disk_used, status.disk_avail,
	    (gfarm_int64_t)0 /* rtt_cache_time */,
	    (gfarm_int32_t)0 /* rtt_usec */,
	    report_flags));
}

/* XXX does not care about hostaliases and architecture */
static gfarm_error_t
host_copy(struct host **dstp, const struct host *src)
{
	struct host *dst;

	GFARM_MALLOC(dst);
	if (dst == NULL) {
		gflog_debug(GFARM_MSG_1001573,
			"allocation of host failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	*dst = *src;
	if ((dst->hi.hostname = strdup(dst->hi.hostname)) == NULL) {
		gflog_debug(GFARM_MSG_1001574,
			"allocation of hostname failed");
		free(dst);
		return (GFARM_ERR_NO_MEMORY);
	}
	*dstp = dst;
	return (GFARM_ERR_NO_ERROR);
}

static void
host_free(struct host *h)
{
	if (h == NULL)
		return;
	if (h->hi.hostname != NULL)
		free(h->hi.hostname);
	free(h);
	return;
}

static void
host_free_all(int n, struct host **h)
{
	int i;

	for (i = 0; i < n; ++i)
		host_free(h[i]);
	free(h);
}

gfarm_error_t
host_active_hosts(int (*filter)(struct host *, void *), void *arg,
	int *nhostsp, struct host ***hostsp)
{
	struct gfarm_hash_iterator it;
	struct host **hosts, *h;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, n;

	n = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_up(h) && filter(h, arg))
			++n;
	}
	GFARM_MALLOC_ARRAY(hosts, n);
	if (hosts == NULL) {
		gflog_debug(GFARM_MSG_1001575,
			"allocation of hosts failed");
		e = GFARM_ERR_NO_MEMORY;
	}

	i = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (hosts != NULL && host_is_up(h) && filter(h, arg) &&
		    i < n) {
			e = host_copy(&hosts[i], h);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1001576,
				    "host_copy() failed: %s",
				    gfarm_error_string(e));
				host_free_all(i, hosts);
				return (e);
			}
			++i;
		}
	}
	*nhostsp = i;
	*hostsp = hosts;
	return (e);
}

static int
null_filter(struct host *host, void *arg)
{
	return (1);
}

gfarm_error_t
host_schedule_reply_all(struct peer *peer, const char *diag,
	int (*filter)(struct host *, void *), void *arg)
{
	gfarm_error_t e, e_save;
	struct host **hosts;
	int i, n;

	e = host_active_hosts(filter, arg, &n, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		n = 0;

	e_save = host_schedule_reply_n(peer, n, diag);
	for (i = 0; i < n; ++i)
		e = host_schedule_reply(hosts[i], peer, diag); {
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	host_free_all(n, hosts);
	return (e_save);
}

gfarm_error_t
host_schedule_reply_one_or_all(struct peer *peer, const char *diag)
{
	gfarm_error_t e, e_save;
	struct host *h = peer_get_host(peer);

	/*
	 * give the top priority to the local host if it has enough space
	 * Note that disk_avail is reported in KiByte.
	 */
	if (h != NULL && host_is_disk_available(h, 0)) {
		e_save = host_schedule_reply_n(peer, 1, diag);
		e = host_schedule_reply(h, peer, diag);
		return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
	} else
		return (host_schedule_reply_all(
				peer, diag, null_filter, NULL));
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
domain_filter(struct host *h, void *d)
{
	const char *domain = d;

	return (gfarm_host_is_in_domain(host_name(h), domain));
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
	e = host_schedule_reply_all(peer, diag, domain_filter, domain);
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
	pthread_mutex_lock(&total_disk_mutex);
	used = total_disk_used;
	avail = total_disk_avail;
	pthread_mutex_unlock(&total_disk_mutex);

	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "lll",
		    used, avail, files));
}

#endif /* TEST */

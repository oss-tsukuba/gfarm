/*
 * $Id$
 */

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include <pthread.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "auth.h"
#include "gfp_xdr.h"
#include "gfs_proto.h" /* GFS_PROTOCOL_VERSION_V2_4 */
#include "config.h"

#include "subr.h"
#include "rpcsubr.h"
#include "user.h"
#include "peer.h"
#include "abstract_host.h"

static const char *
back_channel_type_name(struct peer *peer)
{
	if (peer == NULL)
		return ("*_channel");
	switch (peer_get_auth_id_type(peer)) {
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		return ("back_channel");
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		return ("gfmd_channel");
	default:
		gflog_error(GFARM_MSG_1003282,
		    "(%s@%s) unexpected auth_id_type: %d",
		    peer_get_username(peer), peer_get_hostname(peer),
		    peer_get_auth_id_type(peer));
		abort();
		return ("unexpected_channel");
	}
}

#define ABSTRACT_HOST_MUTEX_DIAG "abstract_host_mutex"

void
abstract_host_init(struct abstract_host *h, struct abstract_host_ops *ops,
	const char *diag)
{
	h->ops = ops;
	h->invalid = 0;
	h->peer = NULL;
	h->protocol_version = 0;
	h->can_send = 1;
	h->can_receive = 1;
	h->is_active = 0;
	h->busy_time = 0;

	gfarm_cond_init(&h->ready_to_send, diag, "ready_to_send");
	gfarm_cond_init(&h->ready_to_receive, diag, "ready_to_receive");
	gfarm_mutex_init(&h->mutex, diag, ABSTRACT_HOST_MUTEX_DIAG);
}

int
abstract_host_get_protocol_version(struct abstract_host *h)
{
	return (h->protocol_version);
}

void
abstract_host_invalidate(struct abstract_host *h)
{
	h->invalid = 1;
}

void
abstract_host_validate(struct abstract_host *h)
{
	h->invalid = 0;
}

int
abstract_host_is_invalid_unlocked(struct abstract_host *h)
{
	return (h->invalid != 0);
}

int
abstract_host_is_valid_unlocked(struct abstract_host *h)
{
	return (h->invalid == 0);
}

static void
abstract_host_mutex_lock(struct abstract_host *h, const char *diag)
{
	gfarm_mutex_lock(&h->mutex, diag, ABSTRACT_HOST_MUTEX_DIAG);
}

static void
abstract_host_mutex_unlock(struct abstract_host *h, const char *diag)
{
	gfarm_mutex_unlock(&h->mutex, diag, ABSTRACT_HOST_MUTEX_DIAG);
}

int
abstract_host_is_valid(struct abstract_host *h, const char *diag)
{
	int valid;

	abstract_host_mutex_lock(h, diag);
	valid = abstract_host_is_valid_unlocked(h);
	abstract_host_mutex_unlock(h, diag);
	return (valid);
}

void
abstract_host_activate(struct abstract_host *h, const char *diag)
{
	abstract_host_mutex_lock(h, diag);
	h->is_active = 1;
	abstract_host_mutex_unlock(h, diag);
}

struct host *
abstract_host_to_host(struct abstract_host *h)
{
	return (h->ops->abstract_host_to_host(h));
}

struct mdhost *
abstract_host_to_mdhost(struct abstract_host *h)
{
	return (h->ops->abstract_host_to_mdhost(h));
}

const char *
abstract_host_get_name(struct abstract_host *h)
{
	return (h->ops->get_name(h));
}

int
abstract_host_get_port(struct abstract_host *h)
{
	return (h->ops->get_port(h));
}

int
abstract_host_is_up_unlocked(struct abstract_host *h)
{
	return (abstract_host_is_valid_unlocked(h) && h->is_active);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: host::channel_mutex
 * SLEEPS: no
 */
int
abstract_host_is_up(struct abstract_host *h)
{
	int up;
	static const char diag[] = "abstract_host_is_up";

	abstract_host_mutex_lock(h, diag);
	up = abstract_host_is_up_unlocked(h);
	abstract_host_mutex_unlock(h, diag);
	return (up);
}

/*
 * PREREQUISITE: host::channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 */
static int
abstract_host_is_unresponsive(struct abstract_host *host, gfarm_int64_t now,
	const char *diag)
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
		    abstract_host_get_name(host), (long long)host->busy_time);
		unresponsive = 1;
	}

	return (unresponsive);
}

static void
abstract_host_peer_unbusy(struct abstract_host *host, const char *diag)
{
	abstract_host_mutex_lock(host, diag);
	host->busy_time = 0;
	abstract_host_mutex_unlock(host, diag);
}

/*
 * if abstract_host_get_peer() is called,
 * same number of abstract_host_put_peer() calls should be made.
 */
struct peer *
abstract_host_get_peer(struct abstract_host *h, const char *diag)
{
	struct peer *peer;

	abstract_host_mutex_lock(h, diag);
	peer = h->peer;
	if (peer != NULL)
		peer_add_ref(peer);
	abstract_host_mutex_unlock(h, diag);

	return (peer);
}

void
abstract_host_put_peer(struct abstract_host *h, struct peer *peer)
{
	if (peer != NULL)
		peer_del_ref(peer);
}

static gfarm_error_t
abstract_host_sender_trylock(struct abstract_host *host, struct peer **peerp,
	const char *diag, long timeout_microsec)
{
	gfarm_error_t e;
	struct peer *peer0;
	struct timeval tv;
	struct timespec ts;

	abstract_host_mutex_lock(host, diag);

	for (;;) {
		peer0 = host->peer;
		if (peer0 == NULL) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		if (host->can_send) {
			host->can_send = 0;
			host->busy_time = 0;
			peer_add_ref(peer0);
			*peerp = peer0;
			e = GFARM_ERR_NO_ERROR;
			break;
		}
		gettimeofday(&tv, NULL);
		gfarm_timeval_add_microsec(&tv, timeout_microsec);
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
		if (gfarm_cond_timedwait(&host->ready_to_send, &host->mutex,
		    &ts, diag, "ready_to_send") == 0) {
			e = GFARM_ERR_DEVICE_BUSY;
			break;
		}
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	abstract_host_mutex_unlock(host, diag);

	return (e);
}

static gfarm_error_t
abstract_host_sender_lock(struct abstract_host *host, struct peer **peerp,
	const char *diag)
{
	gfarm_error_t e;
	struct peer *peer0;

	abstract_host_mutex_lock(host, diag);

	for (;;) {
		peer0 = host->peer;
		if (peer0 == NULL) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		if (host->can_send) {
			host->can_send = 0;
			host->busy_time = 0;
			peer_add_ref(peer0);
			*peerp = peer0;
			e = GFARM_ERR_NO_ERROR;
			break;
		}
		gfarm_cond_wait(&host->ready_to_send, &host->mutex,
		    diag, "ready_to_send");
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	abstract_host_mutex_unlock(host, diag);

	return (e);
}

static void
abstract_host_sender_unlock(struct abstract_host *host, struct peer *peer,
	const char *diag)
{
	abstract_host_mutex_lock(host, diag);

	if (peer == host->peer) {
		host->can_send = 1;
		host->busy_time = 0;
	}
	peer_del_ref(peer);
	gfarm_cond_signal(&host->ready_to_send, diag, "ready_to_send");

	abstract_host_mutex_unlock(host, diag);
}

static gfarm_error_t
abstract_host_receiver_lock(struct abstract_host *host, struct peer **peerp,
	const char *diag)
{
	gfarm_error_t e;
	struct peer *peer0;

	abstract_host_mutex_lock(host, diag);

	for (;;) {
		peer0 = host->peer;
		if (peer0 == NULL) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		if (host->can_receive) {
			host->can_receive = 0;
			peer_add_ref(peer0);
			*peerp = peer0;
			e = GFARM_ERR_NO_ERROR;
			break;
		}
		/* may happen at gfsd restart? */
		gflog_info(GFARM_MSG_1003691,
		    "waiting for abstract_host_receiver_lock: "
		    "maybe %s on %s restarted?",
		    peer_get_service_name(peer0),
		    abstract_host_get_name(host));
		gfarm_cond_wait(&host->ready_to_receive,
		    &host->mutex, diag, "ready_to_receive");
		if (host->peer != peer0) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
	}

	abstract_host_mutex_unlock(host, diag);

	return (e);
}

static void
abstract_host_receiver_unlock(struct abstract_host *host, struct peer *peer)
{
	static const char diag[] = "abstract_host_receiver_unlock";

	abstract_host_mutex_lock(host, diag);

	if (peer == host->peer)
		host->can_receive = 1;
	peer_del_ref(peer);
	gfarm_cond_signal(&host->ready_to_receive, diag, "ready_to_receive");

	abstract_host_mutex_unlock(host, diag);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: host::channel_mutex, dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe (see the comment of dead_file_copy_host_becomes_up())
 *	but host::channel_mutex, dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 */
void
abstract_host_set_peer(struct abstract_host *h, struct peer *p, int version)
{
	static const char diag[] = "abstract_host_set_peer";

	abstract_host_mutex_lock(h, diag);

	h->can_send = 1;
	h->can_receive = 1;
	h->peer = p;
	h->protocol_version = version;
	h->is_active = 1;
	h->busy_time = 0;
	h->ops->set_peer_locked(h, p);

	peer_add_ref(p);

	abstract_host_mutex_unlock(h, diag);

	h->ops->set_peer_unlocked(h, p);
}

/*
 * PREREQUISITE: host::channel_mutex
 * LOCKS: nothing
 * SLEEPS: no
 *
 * should be called after host->is_active = 0;
 */
static void
abstract_host_break_locks(struct abstract_host *host)
{
	static const char diag[] = "abstract_host_break_locks";

	gfarm_cond_broadcast(&host->ready_to_send, diag, "ready_to_send");
	gfarm_cond_broadcast(&host->ready_to_receive, diag, "ready_to_receive");
}

/*
 * PREREQUISITE: host::channel_mutex
 * LOCKS: removal_pendingq.mutex, host_busyq.mutex
 * SLEEPS: no
 */
static void
abstract_host_peer_unset(struct abstract_host *h)
{
	struct peer *peer = h->peer;

	h->peer = NULL;
	h->protocol_version = 0;
	h->is_active = 0;
	h->ops->unset_peer(h, peer);

#if 0
	/*
	 * XXX FIXME: peer_del_ref() is currently called from
	 * abstract_host_disconnect_request()
	 */
	peer_del_ref(peer);
#endif

	abstract_host_break_locks(h);
}

/* peer may be NULL */
void
abstract_host_disconnect_request(struct abstract_host *h, struct peer *peer,
	const char *diag)
{
	int disabled = 0;
	struct peer *hpeer;

	abstract_host_mutex_lock(h, diag);

	hpeer = h->peer;
	/*
	 * hpeer != NULL:
	 *	do disconnect only if this host is currently connected.
	 * peer == hpeer:
	 *	parameter peer is same with hpeer (currently connected peer)?
	 * peer == NULL:
	 *	caller doesn't specify peer, thus, disconnect anyway.
	 */
	if (hpeer != NULL && (peer == hpeer || peer == NULL)) {
		disabled = 1;
		abstract_host_peer_unset(h);
		h->ops->disable(h);

		peer_record_protocol_error(hpeer);
		/* must be after abstract_host_peer_unset() */
		peer_free_request(hpeer);
	} else {
		if (hpeer == NULL)
			gflog_notice(GFARM_MSG_1003475,
			    "%s: already disconnected",
			    abstract_host_get_name(h));
		else
			gflog_notice(GFARM_MSG_1003476,
			    "%s: already disconnected & reconnected",
			    abstract_host_get_name(h));
	}

	abstract_host_mutex_unlock(h, diag);

	if (disabled) {
		h->ops->disabled(h, hpeer);

		/*
		 * XXX FIXME: hpeer argument should be removed from
		 * h->ops->disabled(), and this peer_del_ref() should be
		 * moved to abstract_host_peer_unset().
		 */
		peer_del_ref(hpeer);
	}
}

static void
abstract_host_peer_busy(struct abstract_host *host, const char *diag)
{
	struct peer *unresponsive_peer = NULL;

	abstract_host_mutex_lock(host, diag);
	if (!host->is_active || host->invalid)
		;
	else if (host->busy_time == 0)
		host->busy_time = time(NULL);
	else if (abstract_host_is_unresponsive(host, time(NULL), diag))
		unresponsive_peer = host->peer;
	abstract_host_mutex_unlock(host, diag);

	if (unresponsive_peer != NULL) {
		gflog_error(GFARM_MSG_1002774,
		    "%s(%s): disconnecting: busy at sending",
		    back_channel_type_name(unresponsive_peer),
		    abstract_host_get_name(host));
		abstract_host_disconnect_request(host, unresponsive_peer,
		    diag);
	}
}

int
abstract_host_check_busy(struct abstract_host *host, gfarm_int64_t now,
	const char *diag)
{
	int busy = 0;
	struct peer *unresponsive_peer = NULL;

	abstract_host_mutex_lock(host, diag);

	if (!host->is_active || host->invalid)
		busy = 1;
	else if (abstract_host_is_unresponsive(host, now, diag))
		unresponsive_peer = host->peer;

	abstract_host_mutex_unlock(host, diag);

	if (unresponsive_peer != NULL) {
		gflog_error(GFARM_MSG_1002775,
		    "%s(%s): disconnecting: busy during queue scan",
		    back_channel_type_name(unresponsive_peer),
		    abstract_host_get_name(host));
		abstract_host_disconnect_request(host, unresponsive_peer,
		    diag);
	}

	return (busy || unresponsive_peer != NULL);
}

static gfarm_error_t
async_channel_protocol_switch(struct abstract_host *host, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size,
	channel_protocol_switch_t channel_protocol_switch)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_int32_t request;
	int unknown_request = 0;

	e = gfp_xdr_recv_request_command(client, 0, &size, &request);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = channel_protocol_switch(host, peer, request, xid, size,
	    &unknown_request);
	if (unknown_request) {
		gflog_error(GFARM_MSG_1002776,
		    "(%s) unknown request %d (xid:%d size:%d), reset",
		    back_channel_type_name(peer),
		    (int)request, (int)xid, (int)size);
		e = gfp_xdr_purge(client, 0, size);
	}
	return (e);
}

static gfarm_error_t
async_channel_service(struct abstract_host *host,
	struct peer *peer, gfp_xdr_async_peer_t async,
	channel_protocol_switch_t channel_protocol_switch)
{
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	enum gfp_xdr_msg_type type;
	gfp_xdr_xid_t xid;
	size_t size;
	gfarm_int32_t rv;

	e = gfp_xdr_recv_async_header(conn, 0, &type, &xid, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002777,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	switch (type) {
	case GFP_XDR_TYPE_REQUEST:
		e = async_channel_protocol_switch(host, peer, xid, size,
		    channel_protocol_switch);
		break;
	case GFP_XDR_TYPE_RESULT:
		e = gfp_xdr_callback_async_result(async, peer, xid, size, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002778,
			    "(%s) unknown reply xid:%d size:%d",
			    back_channel_type_name(peer), (int)xid, (int)size);
			e = gfp_xdr_purge(conn, 0, size);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1002779,
				    "(%s) skipping %d bytes: %s",
				    back_channel_type_name(peer), (int)size,
				    gfarm_error_string(e));
		} else if (IS_CONNECTION_ERROR(rv)) {
			e = rv;
		}
		break;
	default:
		gflog_fatal(GFARM_MSG_1002780,
		    "type %d", type);
		/*NOTREACHED*/
		e = GFARM_ERR_PROTOCOL;
		break;
	}
	return (e);
}

void *
gfm_server_channel_main(void *arg,
	channel_protocol_switch_t channel_protocol_switch
#ifdef COMPAT_GFARM_2_3
	    , void (*channel_free)(struct abstract_host *),
	    gfarm_error_t (*sync_channel_service)(struct abstract_host *,
		struct peer *)
#endif
	)
{
	struct peer *peer0 = arg, *peer;
	struct abstract_host *host = peer_get_abstract_host(peer0);
	gfp_xdr_async_peer_t async;
	gfarm_error_t e;
	static const char diag[] = "gfm_server_channel_main";

	e = abstract_host_receiver_lock(host, &peer,
	    back_channel_type_name(peer0));
	if (e != GFARM_ERR_NO_ERROR) { /* already disconnected */
		gflog_notice(GFARM_MSG_1002781,
		    "channel(%s): aborted: %s",
		    abstract_host_get_name(host), gfarm_error_string(e));
#ifdef COMPAT_GFARM_2_3
		channel_free(host);
#endif
		peer_invoked(peer0);
		return (NULL);
	}
	/*
	 * the following ensures that the bach_channel connection is
	 * not switched to another one.
	 */
	if (peer != peer0) {
		gflog_notice(GFARM_MSG_1002782,
		    "%s(%s): aborted: unexpected peer switch",
		    back_channel_type_name(peer), abstract_host_get_name(host));
#ifdef COMPAT_GFARM_2_3
		channel_free(host);
#endif
		abstract_host_receiver_unlock(host, peer);
		peer_invoked(peer0);
		return (NULL);
	}

	/* now, host_receiver_lock() is protecting this peer */
	peer_invoked(peer);

	async = peer_get_async(peer);

	do {
		if (peer_had_protocol_error(peer)) {
			/* abstract_host_disconnect*() must be already called */
#ifdef COMPAT_GFARM_2_3
			channel_free(host);
#endif
			abstract_host_receiver_unlock(host, peer);
			gflog_debug(GFARM_MSG_1002783,
			    "%s(%s): host_disconnect was called",
			    back_channel_type_name(peer),
			    abstract_host_get_name(host));
			return (NULL);
		}
#ifdef COMPAT_GFARM_2_3
		if (async != NULL)
#endif
			e = async_channel_service(host, peer, async,
			    channel_protocol_switch);
#ifdef COMPAT_GFARM_2_3
		else
			e = sync_channel_service(host, peer);
#endif
		if (IS_CONNECTION_ERROR(e)) {
			if (e == GFARM_ERR_UNEXPECTED_EOF) {
				gflog_notice(GFARM_MSG_1002784,
				    "%s(%s): disconnected",
				    back_channel_type_name(peer),
				    abstract_host_get_name(host));
			} else {
				gflog_error(GFARM_MSG_1002785,
				    "%s(%s): "
				    "request error, reset: %s",
				     back_channel_type_name(peer),
				     abstract_host_get_name(host),
				     gfarm_error_string(e));
			}
#ifdef COMPAT_GFARM_2_3
			channel_free(host);
#endif
			abstract_host_disconnect_request(host, peer, diag);
			abstract_host_receiver_unlock(host, peer);
			return (NULL);
		}
	} while (gfp_xdr_recv_is_ready(peer_get_conn(peer)));

	/*
	 * NOTE:
	 * We should use do...while loop for the above gfp_xdr_recv_is_ready()
	 * case, instead of thrpool_add_job().
	 * See the comment in protocol_main() for detail.
	 */

	peer_watch_access(peer);

	abstract_host_receiver_unlock(host, peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

void
gfm_server_channel_already_disconnected_message(struct abstract_host *host,
	const char *proto, const char *op, const char *condition)
{
	gflog_debug(GFARM_MSG_1002786,
	    "channel(%s) %s %s: already disconnected: %s",
	    abstract_host_get_name(host), proto, op, condition);
}

void
gfm_server_channel_disconnect_request(struct abstract_host *host,
	struct peer *peer, const char *proto, const char *op,
	const char *condition)
{
	static const char diag[] = "gfm_server_channel_disconnect_request";

	gflog_notice(GFARM_MSG_1002787,
	    "%s(%s) %s %s: disconnecting: %s",
	    back_channel_type_name(peer), abstract_host_get_name(host),
	    proto, op, condition);
	abstract_host_disconnect_request(host, peer, diag);
}

/*
 * synchronous mode of back_channel is only used before gfarm-2.4.0
 */
gfarm_error_t
gfm_client_channel_vsend_request(struct abstract_host *host,
	struct peer *peer0, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *), void *closure,
#ifdef COMPAT_GFARM_2_3
	void (*host_set_callback)(struct abstract_host *, struct peer *,
	    gfarm_int32_t (*)(void *, void *, size_t),
	    void (*)(void *, void *), void *),
#endif
	long timeout_microsec, gfarm_int32_t command,
	const char *format, va_list * app)
{
	gfarm_error_t e;
	struct peer *peer;
	gfp_xdr_async_peer_t async;
	struct gfp_xdr *server;

	if (debug_mode)
		gflog_info(GFARM_MSG_1002788,
		    "%s: <%s> channel sending request(%d)",
		    abstract_host_get_name(host), diag, command);

	e = abstract_host_sender_trylock(host, &peer, diag, timeout_microsec);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_DEVICE_BUSY) {
			gflog_debug(GFARM_MSG_1002789,
			    "%s(%s) channel (command %d) request: "
			    "sending busy", abstract_host_get_name(host),
			    diag, command);
			abstract_host_peer_busy(host, diag);
		} else /* host_disconnect_request() is already called */
			gfm_server_channel_already_disconnected_message(host,
			    diag, "request", "sending busy");
		return (e);
	}
	/* if (peer0 == NULL), the caller doesn't care the connection */
	if (peer0 != NULL && peer != peer0) {
		abstract_host_sender_unlock(host, peer,
		    back_channel_type_name(peer0));
		gflog_debug(GFARM_MSG_1002790,
		    "(%s) %s (command %d) request: "
		    "%s was reconnected",
		    abstract_host_get_name(host), diag,
		    command, peer_get_service_name(peer0));
		return (GFARM_ERR_CONNECTION_ABORTED);
	}
	abstract_host_peer_unbusy(host, diag);
	async = peer_get_async(peer);
	server = peer_get_conn(peer);

	if (async != NULL) { /* is asynchronous mode? */
		e = gfp_xdr_vsend_async_request(server,
		    async, result_callback, disconnect_callback, closure,
		    command, format, app);
#ifdef COMPAT_GFARM_2_3
	} else { /*  synchronous mode */
		host_set_callback(host, peer,
		    result_callback, disconnect_callback, closure);
		e = gfp_xdr_vrpc_request(server,
		    command, &format, app);
		if (*format != '\0') {
			gflog_fatal(GFARM_MSG_1002791,
			    "gfs_client_send_request(%d): "
			    "invalid format character: %c(%x)",
			    command, *format, *format);
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = gfp_xdr_flush(server);
#endif
	}

	if (e != GFARM_ERR_NO_ERROR) { /* must be IS_CONNECTION_ERROR(e) */
		gfm_server_channel_disconnect_request(host, peer,
		    diag, "request", gfarm_error_string(e));
		abstract_host_sender_unlock(host, peer, diag);
		return (e);
	}

	if (async != NULL) /* is asynchronous mode? */
		abstract_host_sender_unlock(host, peer, diag);
	return (GFARM_ERR_NO_ERROR);
}

/* abstract_host_receiver_lock() must be already called here by
 * channel_main() */
gfarm_error_t
gfm_server_channel_vget_request(struct peer *peer, size_t size,
	const char *diag, const char *format, va_list *app)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;

	if (debug_mode)
		gflog_info(GFARM_MSG_1002792,
		    "%s: <%s> %s start receiving",
		    peer_get_hostname(peer), diag,
		    back_channel_type_name(peer));
	e = gfp_xdr_vrecv_request_parameters(client, 0, &size, format, app);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002793,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

/* XXX FIXME: currently called by threads in back_channel_recv_thread_pool or
 *            gfmdc_recv_thread_pool */
gfarm_error_t
gfm_server_channel_vput_reply(struct abstract_host *host,
	struct peer *peer0, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, va_list *app)
{
	gfarm_error_t e;
	struct peer *peer;
	struct gfp_xdr *client;

	if (debug_mode)
		gflog_info(GFARM_MSG_1002794,
		    "%s: <%s> sending reply: %d",
		    abstract_host_get_name(host), diag, (int)errcode);

	/*
	 * Since this is a reply, the peer is probably living,
	 * thus, not using peer_sender_trylock() is mostly ok.
	 */
	if ((e = abstract_host_sender_lock(host, &peer, diag))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if (peer != peer0) {
		abstract_host_sender_unlock(host, peer, diag);
		return (GFARM_ERR_CONNECTION_ABORTED);
	}
	client = peer_get_conn(peer);
	e = gfp_xdr_vsend_async_result(client, xid, errcode, format, app);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);

	abstract_host_sender_unlock(host, peer, diag);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002795,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

/* abstract_host_receiver_lock() must be already called here by
 * channel_main() */
gfarm_error_t
gfm_client_channel_vrecv_result(struct peer *peer,
	struct abstract_host *host, size_t size, const char *diag,
	const char **formatp, gfarm_error_t *errcodep, va_list *app)
{
	gfarm_error_t e;
	gfp_xdr_async_peer_t async = peer_get_async(peer);
	gfarm_int32_t errcode;
	struct gfp_xdr *conn = peer_get_conn(peer);

	if (debug_mode)
		gflog_info(GFARM_MSG_1002796,
		    "%s: <%s> %s receiving reply", abstract_host_get_name(host),
		    diag, back_channel_type_name(peer));

	if (async != NULL) { /* is async mode? */
		e = gfp_xdr_vrpc_result_sized(conn, 0,
		    &size, &errcode, formatp, app);
	} else { /*  synchronous mode */
		e = gfp_xdr_vrpc_result(conn, 0, 1, &errcode, formatp, app);
		abstract_host_sender_unlock(host, peer,
		    back_channel_type_name(peer));
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002797,
		    "%s(%s) RPC result: %s", back_channel_type_name(peer),
		    abstract_host_get_name(host), gfarm_error_string(e));
	} else if (async != NULL && size != 0) {
		gflog_error(GFARM_MSG_1002798,
		    "%s(%s) RPC result: protocol residual %d",
		    back_channel_type_name(peer), abstract_host_get_name(host),
		    (int)size);
		if ((e = gfp_xdr_purge(conn, 0, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1002799,
			    "%s(%s) RPC result: skipping: %s",
			    back_channel_type_name(peer),
			    abstract_host_get_name(host),
			    gfarm_error_string(e));
		e = GFARM_ERR_PROTOCOL;
	} else { /* e == GFARM_ERR_NO_ERROR */
		*errcodep = errcode;
	}
	return (e);
}

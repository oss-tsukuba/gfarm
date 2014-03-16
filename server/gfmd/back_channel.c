/*
 * $Id$
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "gfutil.h"
#include "thrsubr.h"

#include "context.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "config.h"

#include "gfmd.h"
#include "peer_watcher.h"
#include "peer.h"
#include "local_peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "thrpool.h"
#include "callout.h"
#include "abstract_host.h"
#include "host.h"
#include "mdhost.h"
#include "netsendq.h"
#include "netsendq_impl.h"
#include "inode.h"
#include "dead_file_copy.h"
#include "file_replication.h"
#include "gfmd_channel.h"
#include "relay.h"
#include "thrstatewait.h"

#include "back_channel.h"

static struct peer_watcher *back_channel_recv_watcher;

static const char BACK_CHANNEL_DIAG[] = "back_channel";

/*
 * responsibility to call host_disconnect_request():
 *
 * back_channel_main() is the handler of back_channel_recv_watcher.
 *
 * gfs_client_send_request() (and other leaf functions) is responsible
 * for threads in back_channel_send_manager thread_pool.
 *
 * gfm_async_server_put_reply() should be responsible
 * for threads in back_channel_send_manager thread_pool, too.
 */

static void
gfs_client_status_disconnect_or_message(struct host *host,
	struct peer *peer, const char *proto, const char *op,
	const char *condition)
{
	if (peer != NULL) { /* to make the race condition harmless */
		async_server_disconnect_request(
		    host_to_abstract_host(host), peer,
		    proto, op, condition);
	} else {
		async_server_already_disconnected_message(
		    host_to_abstract_host(host),
		    proto, op, condition);
	}
}

/* host_receiver_lock() must be already called here by back_channel_main() */
gfarm_error_t
gfm_async_server_get_request(struct peer *peer, size_t size,
	const char *diag, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = async_server_vget_request(peer, size, diag, format, &ap);
	va_end(ap);

	return (e);
}

gfarm_error_t
gfm_async_server_put_reply(struct host *host,
	struct peer *peer0, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, ...)
{
	gfarm_error_t e;
	va_list ap;
	struct peer *peer;

	if (peer0 == NULL)
		peer = host_get_peer(host);  /* increment refcount */
	else
		peer = peer0;

	va_start(ap, format);
	if (peer == NULL || peer_get_parent(peer) == NULL) {
		e = async_server_vput_reply(host_to_abstract_host(host),
		    peer, xid, diag, errcode, format, &ap);
	} else {
		e = gfmdc_server_vput_remote_gfs_rpc_reply(
		    host_to_abstract_host(host), peer, xid, diag, errcode,
		    format, &ap);
	}
	va_end(ap);

	if (peer0 == NULL)
		host_put_peer(host, peer);  /* decrement refcount */

	return (e);
}

gfarm_error_t
gfs_client_send_request(struct host *host,
	struct peer *peer0, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;
	struct peer *peer;

	if (peer0 == NULL)
		peer = host_get_peer(host);  /* increment refcount */
	else
		peer = peer0;

	va_start(ap, format);
	if (peer == NULL || peer_get_parent(peer) == NULL) {
		e = async_client_vsend_request(
		    host_to_abstract_host(host), peer, diag,
			result_callback, disconnect_callback, closure,
#ifdef COMPAT_GFARM_2_3
			host_set_callback,
#endif
			command, format, &ap);
	} else {
		e = gfmdc_master_client_remote_gfs_rpc(
		    host_to_abstract_host(host), peer, diag, result_callback,
		    disconnect_callback, closure, command, format, &ap);
	}
	va_end(ap);

	if (peer0 == NULL)
		host_put_peer(host, peer);  /* decrement refcount */

	return (e);
}

gfarm_error_t
gfs_client_recv_result_and_error(struct peer *peer, struct host *host,
	size_t size, gfarm_error_t *errcodep,
	const char *diag, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = async_client_vrecv_result(
	    peer, host_to_abstract_host(host), size, diag,
	    &format, errcodep, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfs_client_recv_result(struct peer *peer, struct host *host,
       size_t size, const char *diag, const char *format, ...)
{
	gfarm_error_t e, errcode;
	va_list ap;

	va_start(ap, format);
	e = async_client_vrecv_result(
	    peer, host_to_abstract_host(host), size, diag,
	    &format, &errcode, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/*
	 * We just use gfarm_error_t as the errcode,
	 * Note that GFARM_ERR_NO_ERROR == 0.
	 */
	return (errcode);
}

struct gfs_client_status_entry {
	struct netsendq_entry qentry; /* must be first member */
};

static gfarm_int32_t
gfs_client_status_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct gfs_client_status_entry *qe = arg;
	struct host *host = abstract_host_to_host(qe->qentry.abhost);
	struct host_status st;
	static const char diag[] = "GFS_PROTO_STATUS";

	e = gfs_client_recv_result(peer, host,
	    size, diag, "fffll",
	    &st.loadavg_1min, &st.loadavg_5min, &st.loadavg_15min,
	    &st.disk_used, &st.disk_avail);
	netsendq_remove_entry(abstract_host_get_sendq(qe->qentry.abhost),
	    &qe->qentry, e);

	if (e == GFARM_ERR_NO_ERROR) {
		host_status_update(host, &st);
	} else {
		/* this gfsd is not working correctly, thus, disconnect it */
		gfs_client_status_disconnect_or_message(host, peer,
		    diag, "result", gfarm_error_string(e));
	}
	return (e);
}

/* both giant_lock and peer_table_lock are held before calling this function */
static void
gfs_client_status_free(void *p, void *arg)
{
	struct peer *peer = p;
	struct gfs_client_status_entry *qe = arg;
	struct host *host = abstract_host_to_host(qe->qentry.abhost);
	static const char diag[] = "GFS_PROTO_STATUS";

	gfs_client_status_disconnect_or_message(host, peer,
	    diag, "connection aborted", gfarm_error_string(qe->qentry.result));
	netsendq_remove_entry(abstract_host_get_sendq(qe->qentry.abhost),
	    &qe->qentry, GFARM_ERR_CONNECTION_ABORTED);
}

static void *
gfs_client_status_request(void *arg)
{
	gfarm_error_t e;
	struct gfs_client_status_entry *qe = arg;
	struct host *host = abstract_host_to_host(qe->qentry.abhost);
	struct peer *peer = host_get_peer(host); /* increment refcount */
	static const char diag[] = "GFS_PROTO_STATUS";

	e = gfs_client_send_request(host, peer, diag,
	    gfs_client_status_result, gfs_client_status_free, qe,
	    GFS_PROTO_STATUS, "");
	netsendq_entry_was_sent(abstract_host_get_sendq(qe->qentry.abhost),
	    &qe->qentry);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_info(GFARM_MSG_1001986,
		    "gfs_client_status_request: %s",
		    gfarm_error_string(e));
		/* accessing `qe' is only allowed if e != GFARM_ERR_NO_ERROR */
		qe->qentry.result = e;
		gfs_client_status_free(peer, qe);
	}

	host_put_peer(host, peer); /* decrement refcount */

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static void
gfs_client_status_finalize(struct netsendq_entry *qentryp)
{
	netsendq_entry_destroy(qentryp);
	free(qentryp);
}

struct netsendq_type gfs_proto_status_queue = {
	gfs_client_status_request,
	gfs_client_status_finalize,
	1,
	NETSENDQ_FLAG_PRIOR_ONE_SHOT,
	NETSENDQ_TYPE_GFS_PROTO_STATUS
};

static void
gfs_client_status_schedule(struct host *host, int first_attempt)
{
	gfarm_error_t e;
	struct gfs_client_status_entry *qe;
	enum { stop_callout, do_next, do_retry } callout_next = do_next;
	const char diag[] = "GFS_PROTO_STATUS";

	GFARM_MALLOC(qe);
	if (qe == NULL) {
		callout_next = do_retry;
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s: no memory for queue entry",
		    host_name(host), diag);
	} else {
		netsendq_entry_init(&qe->qentry, &gfs_proto_status_queue);
		qe->qentry.abhost = host_to_abstract_host(host);
		e = netsendq_add_entry(host_sendq(host), &qe->qentry,
		    NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING);
		if (e == GFARM_ERR_NO_ERROR) {
			/* OK */
		} else if (first_attempt && e == GFARM_ERR_DEVICE_BUSY) {
			/*
			 * if this is first attempt just after
			 * the back channel connection is made,
			 * it's possible previous callout remains
			 * with the following scenario:
			 * 1. gfs_client_status_callout() thread begins to run
			 * 2. host_unset_peer() calls callout_stop()
			 * 3. netsendq_host_becomes_down() clears readyq
			 * 4. the gfs_client_status_callout() thread in 1
			 *    adds an entry to workq and readyq
			 */
			gflog_info(GFARM_MSG_UNFIXED,
			    "%s: %s queueing conflict", host_name(host), diag);
		} else {
			/* increment refcount */
			struct peer *peer = host_get_peer(host);

			callout_next = stop_callout;
			gflog_info(GFARM_MSG_UNFIXED,
			    "%s: %s queueing: %s",
			    host_name(host), diag, gfarm_error_string(e));
			/* `qe' is be freed by gfs_client_status_finalize() */
			if (peer == NULL) {
				gflog_info(GFARM_MSG_UNFIXED,
				    "%s: %s: already disconnected",
				    host_name(host), diag);
			} else {
				gfs_client_status_disconnect_or_message(host,
				    peer, diag, "queueing",
				    gfarm_error_string(e));
				 /* decrement refcount */
				host_put_peer(host, peer);
			}
		}
	}
	switch (callout_next) {
	case stop_callout:
		/* do nothing */
		break;
	case do_next:
		callout_schedule(host_status_callout(host),
		    gfarm_metadb_heartbeat_interval * 1000000);
		break;
	case do_retry:
		callout_schedule(host_status_callout(host),
		    gfarm_metadb_heartbeat_interval * 1000000 / 10);
		break;
	}
}

static void *
gfs_client_status_callout(void *arg)
{
	gfs_client_status_schedule(arg, 0);
	return (NULL);
}

#ifdef not_def_REPLY_QUEUE

struct gfm_async_server_reply_to_gfsd_entry {
	struct netsendq_entry qentry; /* must be first member */

	struct peer *peer;
	gfp_xdr_xid_t xid;
	gfarm_int32_t errcode;
	const char *diag;
};

static void *
gfm_async_server_reply_to_gfsd(void *arg)
{
	gfarm_error_t e;
	struct gfm_async_server_reply_to_gfsd_entry *qe = arg;
	struct host *host = abstract_host_to_host(qe->qentry.abhost);
	struct netsendq *qhost = abstract_host_get_sendq(qe->qentry.abhost);
	const char *diag = qe->diag;

	e = gfm_async_server_put_reply(host, qe->peer, qe->xid, diag,
	    qe->errcode, "");
	netsendq_entry_was_sent(qhost, &qe->qentry);
	netsendq_remove_entry(qhost, &qe->qentry, e);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: %s reply: %s",
		    host_name(host), diag, gfarm_error_string(e));
	return (NULL);
}

static void
gfm_async_server_reply_to_gfsd_finalize(struct netsendq_entry *qentryp)
{
	netsendq_entry_destroy(qentryp);
	free(qentryp);
}

struct netsendq_type gfm_async_server_reply_to_gfsd_queue = {
	gfm_async_server_reply_to_gfsd,
	gfm_async_server_reply_to_gfsd_finalize,
	0 /* will be initialized by gfm_proto_reply_to_gfsd_window */,
	0,
	NETSENDQ_TYPE_GFM_PROTO_REPLY_TO_GFSD
};

/* FIXME: should support return values other than gfarm_error_t too */
void
gfm_async_server_reply_to_gfsd_schedule(struct host *host,
	struct peer *peer, gfp_xdr_xid_t xid,
	gfarm_error_t errcode, int flags, const char *diag)
{
	gfarm_error_t e;
	struct gfm_async_server_reply_to_gfsd_entry *qe;

	GFARM_MALLOC(qe);
	if (qe == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s: no memory for queue entry",
		    host_name(host), diag);
	} else {
		netsendq_entry_init(&qe->qentry,
		    &gfm_async_server_reply_to_gfsd_queue);
		qe->qentry.abhost = host_to_abstract_host(host);
		qe->peer = peer;
		qe->xid = xid;
		qe->errcode = errcode;
		qe->diag = diag;
		e = netsendq_add_entry(host_sendq(host), &qe->qentry, flags);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "%s: %s queueing: %s",
			    host_name(host), diag, gfarm_error_string(e));
			if ((flags & NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING)
			    == 0)
				free(qe);
		}
	}
}

#endif /* not_def_REPLY_QUEUE */

/*
 * Back channel protocol switch for master gfmd.
 */
static gfarm_error_t
async_back_channel_protocol_switch_master(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	struct host *host = abstract_host_to_host(h);
	gfarm_error_t e;

	switch (request) {
	case GFM_PROTO_REPLICATION_RESULT:
		e = gfm_async_server_replication_result(host, peer, xid, size);
		break;
	default:
		*unknown_request = 1;
		e = GFARM_ERR_PROTOCOL;
		break;
	}
	return (e);
}

/*
 * Closure for async_back_channel_protocol_switch_slave().
 */
struct protocol_switch_slave_closure {
	struct abstract_host *abhost;
	gfarm_int64_t private_peer_id;
	gfp_xdr_xid_t xid;
	size_t size;
	void *data;
};

/*
 * Create an object of 'struct protocol_switch_slave_closure'.
 */
static struct protocol_switch_slave_closure *
protocol_switch_slave_closure_alloc(struct abstract_host *abhost,
	gfarm_int64_t private_peer_id, gfp_xdr_xid_t xid, size_t size,
	void *data)
{
	static struct protocol_switch_slave_closure *closure;

	GFARM_MALLOC(closure);
	if (closure == NULL)
		return (NULL);
	closure->abhost          = abhost;
	closure->private_peer_id = private_peer_id;
	closure->xid             = xid;
	closure->size            = size;
	closure->data            = malloc(size);
	if (closure->data == NULL) {
		free(closure);
		return (NULL);
	}
	if (data != NULL)
		memcpy(closure->data, data, size);

	return (closure);
}

/*
 * Dispose an object of 'struct protocol_switch_slave_closure'.
 */
static void
protocol_switch_slave_closure_free(
	struct protocol_switch_slave_closure *closure)
{
	free(closure->data);
	free(closure);
}

/*
 * 'result_callback' handler for
 * async_back_channel_protocol_switch_slave().
 */
static gfarm_error_t
async_back_channel_protocol_switch_slave_result(gfarm_error_t errcode,
    void *arg, size_t size, void *data)
{
	gfarm_error_t e = errcode;
	struct peer *peer;
	struct protocol_switch_slave_closure *closure = arg;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (relay reply to gfsd)";

	/* increment refcount */
	peer = abstract_host_get_peer_with_id(closure->abhost,
	    closure->private_peer_id, diag);
	if (peer == NULL) {
		if (e == GFARM_ERR_NO_ERROR)
			e = GFARM_ERR_CONNECTION_ABORTED;
	} else if (errcode == GFARM_ERR_NO_ERROR) {
		e = async_server_put_reply(closure->abhost, peer,
		    closure->xid, diag, errcode, "r", size, data);
	} else {
		(void) async_server_put_reply(closure->abhost, peer,
		    closure->xid, diag, errcode, "");
		e = errcode;
	}

	/* decrement refcount */
	if (peer != NULL)
		abstract_host_put_peer(closure->abhost, peer);
	protocol_switch_slave_closure_free(closure);
	return (e);
}

/*
 * 'disconnect_callback' handler for
 * async_back_channel_protocol_switch_slave().
 */
static void
async_back_channel_protocol_switch_slave_disconnect(gfarm_error_t errcode,
	void *arg)
{
	(void) async_back_channel_protocol_switch_slave_result(errcode, arg,
	    0, "");
}

/*
 * Back channel protocol switch for slave gfmd.  It forwards a GFS protocol
 * request from gfsd it a master gmfd and relays its reply in the opposite
 * direction.
 */
static gfarm_error_t
async_back_channel_protocol_switch_slave(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct host *host = abstract_host_to_host(h);
	struct gfp_xdr *conn = peer_get_conn(peer);
	struct protocol_switch_slave_closure *closure = NULL;
	size_t data_size;
	int eof;
	static const char diag[] = "async_back_channel_protocol_switch_slave";

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> back_channel start receiving request(%d)",
		    peer_get_hostname(peer), diag, (int)request);

	do {
		/*
		 * We dispose 'closure' in
		 * async_back_channel_protocol_switch_slave_result().
		 */
		closure = protocol_switch_slave_closure_alloc(h,
		    peer_get_private_peer_id(peer), xid, size, NULL);
		if (closure == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		}

		data_size = size;
		e = gfp_xdr_recv(conn, 1, &eof, "r", data_size, &data_size,
		    closure->data);
		if (e != GFARM_ERR_NO_ERROR) {
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		} else if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
			break;
		} else if (data_size != 0) {
			e = GFARM_ERR_PROTOCOL;
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: <%s> protocol redidual %u",
			    peer_get_hostname(peer), diag, (int)data_size);
		}

		e = gfmdc_slave_client_remote_gfs_rpc(peer, closure,
		    async_back_channel_protocol_switch_slave_result,
		    async_back_channel_protocol_switch_slave_disconnect,
		    request, size, closure->data);
	} while (0);

	if (e != GFARM_ERR_NO_ERROR) {
		if (!eof) {
			(void) gfm_async_server_put_reply(host, peer, xid,
			    diag, e, "");
		}
		if (closure != NULL)
			protocol_switch_slave_closure_free(closure);
	}
	return (e);
}

gfarm_error_t
async_back_channel_protocol_switch(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	gfarm_error_t e;

	if (mdhost_self_is_master()) {
		e = async_back_channel_protocol_switch_master(h, peer, request,
		    xid, size, unknown_request);
	} else {
		e = async_back_channel_protocol_switch_slave(h, peer, request,
		    xid, size, unknown_request);
	}

	return (e);
}

#ifdef COMPAT_GFARM_2_3
static gfarm_error_t
sync_back_channel_service(struct abstract_host *h, struct peer *peer)
{
	struct host *host = abstract_host_to_host(h);
	gfarm_int32_t (*result_callback)(void *, void *, size_t);
	void *arg;

	if (!host_get_result_callback(host, peer, &result_callback, &arg)) {
		gflog_error(GFARM_MSG_1002285,
		    "extra data from back channel");
		return (GFARM_ERR_PROTOCOL);
	}
	/*
	 * the 3rd argument of (*callback)() (i.e. 0) is dummy,
	 * but it must be a value except GFP_XDR_ASYNC_SIZE_FREED.
	 */
	return ((*result_callback)(peer, arg, 0));
}

/*
 * it's ok to call this with an async peer,
 * because host_get_disconnect_callback() returns FALSE in that case.
 */
static void
sync_back_channel_free(struct abstract_host *h)
{
	struct host *host = abstract_host_to_host(h);
	void (*disconnect_callback)(void *, void *);
	struct peer *peer;
	void *arg;

	if (host_get_disconnect_callback(host,
	    &disconnect_callback, &peer, &arg)) {
		(*disconnect_callback)(peer, arg);
	}
}
#endif

static void *
back_channel_main(void *arg)
{
	struct local_peer *local_peer = arg;

	return (async_server_main(local_peer,
		async_back_channel_protocol_switch
#ifdef COMPAT_GFARM_2_3
		, sync_back_channel_free,
		sync_back_channel_service
#endif
		));
}

static gfarm_error_t
gfm_server_switch_back_channel_common(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client,
	int version, const char *diag, struct relayed_request *relay)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2;
	struct host *host;
	gfp_xdr_async_peer_t async = NULL;
	struct local_peer *local_peer = NULL;
	int is_direct_connection;
	int i = 0;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	host = NULL;
#endif
	is_direct_connection = (peer_get_parent(peer) == NULL);

	giant_lock();

	if (from_client) {
		gflog_debug(GFARM_MSG_1001995,
		    "Operation not permitted: from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001996,
		    "Operation not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (is_direct_connection &&
	    (e = gfp_xdr_async_peer_new(&async)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002288,
		    "%s: gfp_xdr_async_peer_new(): %s",
		    diag, gfarm_error_string(e));
	}
	giant_unlock();

	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay,
	    diag, &e,  "i", &i/*XXX FIXME*/);
	if (e2 != GFARM_ERR_NO_ERROR)
		return (e2);
	if (debug_mode)
		gflog_debug(GFARM_MSG_1000404, "gfp_xdr_flush");
	e2 = gfp_xdr_flush(peer_get_conn(peer));
	if (e2 != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000405,
		    "%s: protocol flush: %s",
		    diag, gfarm_error_string(e2));
		return (e2);
	} else if (e != GFARM_ERR_NO_ERROR)
		return (e2);

	if (is_direct_connection) {
		local_peer = peer_to_local_peer(peer);
		local_peer_set_async(local_peer, async); /* XXXRELAY */
		local_peer_set_readable_watcher(local_peer,
		    back_channel_recv_watcher);
	}

	if (host_is_up(host)) /* throw away old connetion */ {
		gflog_warning(GFARM_MSG_1002440,
		    "back_channel(%s): switching to new connection",
		    host_name(host));
		host_disconnect_request(host, NULL);
	}

	giant_lock();
	peer_set_peer_type(peer, peer_type_back_channel);
	abstract_host_set_peer(host_to_abstract_host(host), peer, version);
	giant_unlock();

	if (is_direct_connection) {
		local_peer_watch_readable(local_peer);
		gfarm_thr_statewait_signal(
		    local_peer_get_statewait(local_peer), e2, diag);
	}

	callout_setfunc(host_status_callout(host),
	    NULL /* or, use back_channel_send_manager thread pool? */,
	    gfs_client_status_callout, host);
	gfs_client_status_schedule(host, 1);
	gflog_info(GFARM_MSG_UNFIXED,
	    "back_channel(%s): started", host_name(host));

	return (e2);
}

#ifdef COMPAT_GFARM_2_3

gfarm_error_t
gfm_server_switch_back_channel(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_SWITCH_BACK_CHANNEL";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SWITCH_BACK_CHANNEL, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, xid, sizep,
	    from_client, GFS_PROTOCOL_VERSION_V2_3, diag, relay);

	return (e);
}

#endif /* defined(COMPAT_GFARM_2_3) */

gfarm_error_t
gfm_server_switch_async_back_channel(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t version;
	gfarm_int64_t gfsd_cookie;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL, "il", &version, &gfsd_cookie);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, xid, sizep,
	    from_client, version, diag, relay);

	return (e);
}

/*
 * Wrapped closure for gfm_server_relay_gfs_rpc().
 */
struct gfs_client_relay_closure {
	struct abstract_host *abhost;
	gfarm_int64_t private_peer_id;
	size_t size;
	void *data;
	void *closure;
	gfarm_int32_t (*result_callback)(gfarm_error_t, void *, size_t,
	    void *);
	void (*disconnect_callback)(gfarm_error_t, void *);
};

/*
 * Create an object of 'struct gfs_client_relay_closure'.
 */
static struct gfs_client_relay_closure *
gfs_client_relay_closure_alloc(struct abstract_host *abhost,
	gfarm_int64_t private_peer_id, size_t size, void *data, void *closure,
	gfarm_int32_t (*result_callback)(gfarm_error_t, void *, size_t,
	    void *),
	void (*disconnect_callback)(gfarm_error_t, void *))
{
	struct gfs_client_relay_closure *wclosure;

	GFARM_MALLOC(wclosure);
	if (wclosure == NULL)
		return (NULL);
	wclosure->data = malloc(size);
	if (wclosure->data == NULL) {
		free(wclosure);
		return (NULL);
	}

	wclosure->abhost              = abhost;
	wclosure->private_peer_id     = private_peer_id;
	wclosure->size                = size;
	if (data != NULL)
		memcpy(wclosure->data, data, size);
	wclosure->closure             = closure;
	wclosure->result_callback     = result_callback;
	wclosure->disconnect_callback = disconnect_callback;

	return (wclosure);
}

/*
 * Dispose an object of 'struct gfs_client_relay_closure'.
 */
static void
gfs_client_relay_closure_free(struct gfs_client_relay_closure *wclosure)
{
	free(wclosure->data);
	free(wclosure);
}

/*
 * 'result_callback' handler for gfs_client_relay().
 */
static gfarm_int32_t
gfs_client_relay_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e, e2;
	struct peer *peer = p;
	struct gfp_xdr *conn = peer_get_conn(peer);
	struct gfs_client_relay_closure *wclosure = arg;
	void *data = NULL;
	size_t data_size;
	int eof;
	static const char diag[] = "gfs_client_relay_result";

	do {
		data = malloc(size);
		if (data == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		}
		data_size = size;
		e = gfp_xdr_recv(conn, 1, &eof, "r", data_size, &data_size,
		    data);
		if (e != GFARM_ERR_NO_ERROR) {
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		} else if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
		} else if (data_size != 0) {
			e = GFARM_ERR_PROTOCOL;
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: <%s> protocol redidual %u",
			    peer_get_hostname(peer), diag, (int)data_size);
		}

	} while (0);

	e2 = wclosure->result_callback(e, wclosure->closure, size, data);
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;

	free(data);
	gfs_client_relay_closure_free(wclosure);
	return (e);
}

/*
 * 'disconnect_callback' handler for gfs_client_relay().
 */
static void
gfs_client_relay_disconnect(void *p, void *arg)
{
	struct gfs_client_relay_closure *wclosure = arg;

	wclosure->disconnect_callback(GFARM_ERR_UNEXPECTED_EOF,
	    wclosure->closure);
	gfs_client_relay_closure_free(wclosure);
}

static void *
gfs_client_relay_thread(void *arg)
{
	struct gfs_client_relay_closure *wclosure = arg;
	struct peer *peer;
	static const char diag[] = "gfs_client_relay";

	/* increment refcount */
	peer = abstract_host_get_peer_with_id(wclosure->abhost,
	    wclosure->private_peer_id, diag);
	if (peer == NULL)
		return (NULL);

	(void) async_client_send_raw_request(wclosure->abhost, peer, diag,
	    gfs_client_relay_result, gfs_client_relay_disconnect, wclosure,
	    wclosure->size, wclosure->data);

	/* decrement refcount */
	abstract_host_put_peer(wclosure->abhost, peer);

	return (NULL);
}

/*
 * Forward a GFS protocol request received from a master gfmd to gfsd.
 * This function is used by slave gfmd only.
 */
gfarm_error_t
gfs_client_relay(struct abstract_host *abhost, struct peer *peer,
	size_t size, void *data, void *closure,
	gfarm_int32_t (*result_callback)(gfarm_error_t, void *, size_t,
	    void *),
	void (*disconnect_callback)(gfarm_error_t, void *))
{
	struct gfs_client_relay_closure *wclosure;

	wclosure = gfs_client_relay_closure_alloc(abhost,
	    peer_get_private_peer_id(peer), size, data, closure,
	    result_callback, disconnect_callback);
	if (wclosure == NULL)
		return (GFARM_ERR_NO_MEMORY);
	thrpool_add_job(mdhost_send_manager_get_thrpool(),
	    gfs_client_relay_thread, wclosure);

	return (GFARM_ERR_NO_ERROR);
}

void
back_channel_init(void)
{
#ifdef not_def_REPLY_QUEUE
	gfm_async_server_reply_to_gfsd_queue.window_size =
	    gfm_proto_reply_to_gfsd_window;
#endif
	file_replication_init();

	back_channel_recv_watcher = peer_watcher_alloc(
	    /* XXX FIXME: use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    back_channel_main, "receiving from filesystem nodes");

}

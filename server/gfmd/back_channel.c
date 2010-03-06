/*
 * $Id$
 */

#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>

#include <gfarm/gflog.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "config.h"

#include "peer.h"
#include "subr.h"
#include "thrsubr.h"
#include "thrpool.h"
#include "callout.h"
#include "host.h"
#include "inode.h"
#include "dead_file_copy.h"

#include "back_channel.h"

struct thread_pool *back_channel_recv_thread_pool;
struct thread_pool *back_channel_send_thread_pool;

/*
 * responsibility to call host_disconnect():
 *
 * back_channel_main() is responsible for threads in
 * back_channel_recv_thread_pool.
 *
 * gfs_client_send_request() (and other leaf functions) is responsible
 * for threads in back_channel_send_thread_pool.
 *
 * gfm_async_server_put_reply() should be responsible for threads in
 * back_channel_send_thread_pool too, but currently it's not because
 * it's called by threads in back_channel_recv_thread_pool. XXX FIXME
 */

/* host_receiver_lock() must be already called here by back_channel_main() */
static gfarm_error_t
gfm_async_server_get_request(struct peer *peer, size_t size,
	const char *diag, const char *format, ...)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	va_list ap;
	gfarm_error_t e;

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> back_channel start receiving",
		    host_name(peer_get_host(peer)), diag);

	va_start(ap, format);
	e = gfp_xdr_vrecv_request_parameters(client, 0, &size, format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1001980,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

/* XXX FIXME: currently called by threads in back_channel_recv_thread_pool */
static gfarm_error_t
gfm_async_server_put_reply(struct host *host,
	struct peer *peer0, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, ...)
{
	gfarm_error_t e;
	struct peer *peer;
	struct gfp_xdr *client;
	va_list ap;

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> back_channel sending reply: %d",
		    host_name(host), diag, (int)errcode);

	/*
	 * Since this is a reply, the peer is probably living,
	 * thus, not using peer_sender_trylock() is mostly ok.
	 */
	if ((e = host_sender_lock(host, &peer)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (peer != peer0)
		return (GFARM_ERR_CONNECTION_ABORTED);
	client = peer_get_conn(peer);

	va_start(ap, format);
	e = gfp_xdr_vsend_async_result(client, xid, errcode, format, &ap);
	va_end(ap);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);

	host_sender_unlock(host);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1001981,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}


/*
 * synchronous mode of backchannel is only used before gfarm-2.4.0
 */
static gfarm_error_t
gfs_client_send_request(struct host *host, const char *diag,
	gfarm_int32_t (*callback)(void *, size_t), void *closure,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	struct peer *peer;
	gfp_xdr_async_peer_t async;
	struct gfp_xdr *server;
	va_list ap;

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> back_channel sending request(%d)",
		    host_name(host), diag, command);

	e = host_sender_trylock(host, &peer);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_DEVICE_BUSY) {
			host_peer_busy(host);
		} /* otherwise host_disconnect_request() is already called */
		return (e);
	}
	host_peer_unbusy(host);
	async = peer_get_async(peer);
	server = peer_get_conn(peer);

	va_start(ap, format);
	if (async != NULL) { /* is asynchronous mode? */
		e = gfp_xdr_vsend_async_request(server,
		    async, callback, closure,
		    command, format, &ap);
	} else { /*  synchronous mode */
		e = gfp_xdr_vrpc_request(server,
		    command, &format, &ap);
		if (*format != '\0') {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "gfs_client_send_request(%d): "
			    "invalid format character: %c(%x)",
			    command, *format, *format);
		}
		host_set_callback(host, callback, closure);
	}
	va_end(ap);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(server);

	if (e != GFARM_ERR_NO_ERROR) { /* must be IS_CONNECTION_ERROR(e) */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "back_channel(%s) command %d: disconnecting: %s",
		    host_name(host), command, gfarm_error_string(e));
		host_disconnect_request(host);
		host_sender_unlock(host);
		return (e);
	}

	if (async != NULL) /* is asynchronous mode? */
		host_sender_unlock(host);
	return (GFARM_ERR_NO_ERROR);
}

/* host_receiver_lock() must be already called here by back_channel_main() */
static gfarm_error_t
gfs_client_recv_result(struct host *host, size_t size, const char *diag,
	const char *format, ...)
{
	gfarm_error_t e;
	struct peer *peer = host_peer(host);
	gfp_xdr_async_peer_t async = peer_get_async(peer);
	gfarm_int32_t errcode;
	struct gfp_xdr *conn = peer_get_conn(peer);
	va_list ap;

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> back_channel receiving reply",
		    host_name(host), diag);

	va_start(ap, format);
	if (async != NULL) { /* is async mode? */
		e = gfp_xdr_vrpc_result_sized(conn, 0,
		    &size, &errcode, &format, &ap);
	} else { /*  synchronous mode */
		e = gfp_xdr_vrpc_result(conn, 0, &errcode, &format, &ap);
		host_sender_unlock(host);
	}
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "back_channel(%s) RPC result: %s",
		    host_name(host), gfarm_error_string(e));
	} else if (async != NULL && size != 0) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "back_channel(%s) RPC result: protocol residual %d",
		    host_name(host), (int)size);
		if ((e = gfp_xdr_purge(conn, 0, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1001985,
			    "back_channel(%s) RPC result: skipping: %s",
			    host_name(host), gfarm_error_string(e));
		e = GFARM_ERR_PROTOCOL;
	} else {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		e = errcode;
	}
	return (e);
}

static gfarm_int32_t
gfs_client_status_result(void *arg, size_t size)
{
	gfarm_error_t e;
	struct host *host = arg;
	struct host_status st;
	static const char diag[] = "GFS_PROTO_STATUS";

	e = gfs_client_recv_result(host, size, diag, "fffll",
	    &st.loadavg_1min, &st.loadavg_5min, &st.loadavg_15min,
	    &st.disk_used, &st.disk_avail);
	if (e == GFARM_ERR_NO_ERROR) {
		host_status_update(host, &st);
	} else {
		host_status_disable(host);
	}
	return (e);
}

/* this function is called via callout */
static void *
gfs_client_status_request(void *arg)
{
	gfarm_error_t e;
	struct host *host = arg;
	static const char diag[] = "GFS_PROTO_STATUS";

	if (host_status_reply_is_waiting(host)) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "back_channel(%s) disconnected: no status",
		    host_name(host));
		host_disconnect_request(host);
		return (NULL);
	}

	/*
	 * schedule here instead of the end of gfs_client_status_result(),
	 * because gfs_client_send_request() may block, and we should
	 * detect it at next call of gfs_client_status_request().
	 */
	callout_schedule(host_status_callout(host),
	    gfarm_metadb_heartbeat_interval * 1000000);

	e = gfs_client_send_request(host, diag,
	    gfs_client_status_result, host, GFS_PROTO_STATUS, "");
	if (e == GFARM_ERR_DEVICE_BUSY) {
		if (!host_status_callout_retry(host))
			return (NULL);
		gflog_error(GFARM_MSG_UNFIXED,
		    "back_channel(%s) disconnected: status rpc unresponsive",
		    host_name(host));
		host_disconnect_request(host);
		return (NULL);
	}
	if (e == GFARM_ERR_NO_ERROR) {
		host_status_reply_waiting(host);
	} else {
		gflog_error(GFARM_MSG_1001986,
		    "gfs_client_status_request: %s",
		    gfarm_error_string(e));
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static gfarm_int32_t
gfs_client_fhremove_result(void *arg, size_t size)
{
	struct dead_file_copy *dfc = arg;
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_recv_result(host, size, diag, "");
	removal_finishedq_enqueue(dfc, e);
	return (e);
}

static void *
gfs_client_fhremove_request(void *closure)
{
	struct dead_file_copy *dfc = closure;
	gfarm_ino_t ino = dead_file_copy_get_ino(dfc);
	gfarm_int64_t gen = dead_file_copy_get_gen(dfc);
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_send_request(host, diag,
	    gfs_client_fhremove_result, dfc,
	    GFS_PROTO_FHREMOVE, "ll", ino, gen);
	if (e == GFARM_ERR_NO_ERROR) {
		return (NULL);
	} else if (e == GFARM_ERR_DEVICE_BUSY) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "GFS_PROTO_FHREMOVE(%lld, %lld, %s): "
		    "busy, waiting for some time",
		    (long long)dead_file_copy_get_ino(dfc),
		    (long long)dead_file_copy_get_gen(dfc),
		    host_name(dead_file_copy_get_host(dfc)));
		host_busyq_enqueue(dfc);
	} else {
		removal_finishedq_enqueue(dfc, e);
	}
		    
	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static void *
remover(void *closure)
{
	struct dead_file_copy *dfc;

	for (;;) {
		dfc = removal_pendingq_dequeue();
		if (host_is_up(dead_file_copy_get_host(dfc)))
			thrpool_add_job(back_channel_send_thread_pool,
			    gfs_client_fhremove_request, dfc);
		else /* hopes it will up shortly */
			host_busyq_enqueue(dfc);
	}

	/*NOTREACHED*/
	return (NULL);
}

/*
 * GFS_PROTO_REPLICATION_REQUEST didn't exist before gfarm-2.4.0
 */

static gfarm_int32_t
gfs_client_replication_request_result(void *arg, size_t size)
{
	struct file_replicating *fr = arg;
	gfarm_int64_t handle;
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";

	e = gfs_client_recv_result(fr->dst, size, diag, "l", &handle);
	/* XXX FIXME: deadlock */
	giant_lock();
	if (e == GFARM_ERR_NO_ERROR)
		file_replicating_set_handle(fr, handle);
	else
		file_replicating_free(fr);
	giant_unlock();
	return (e);
}

struct gfs_client_replication_request_arg {
	char *srchost;
	int srcport;
	struct host *dst;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	struct file_replicating *fr;
};

static void *
gfs_client_replication_request_request(void *closure)
{
	struct gfs_client_replication_request_arg *arg = closure;
	char *srchost = arg->srchost;
	gfarm_int32_t srcport = arg->srcport;
	struct host *dst = arg->dst;
	gfarm_ino_t ino = arg->ino;
	gfarm_int64_t gen = arg->gen;
	struct file_replicating *fr = arg->fr;
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";

	free(arg);

	e = gfs_client_send_request(dst, diag,
	    gfs_client_replication_request_result, fr,
	    GFS_PROTO_REPLICATION_REQUEST, "sill",
	    srchost, srcport, ino, gen);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_lock(); /* XXX FIXME: deadlock */
		e = host_replicated(dst, ino, gen, -1, 0, e, -1);
		giant_unlock();
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

gfarm_error_t
async_back_channel_replication_request(char *srchost, int srcport,
	struct host *dst, gfarm_ino_t ino, gfarm_int64_t gen,
	struct file_replicating *fr)
{
	struct gfs_client_replication_request_arg *arg;

	/* XXX FIXME: check host_is_up(dst) */

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_error(GFARM_MSG_1001988,
		    "async_back_channel_replication_request: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->srchost = srchost;
	arg->srcport = srcport;
	arg->dst = dst;
	arg->ino = ino;
	arg->gen = gen;
	arg->fr = fr;
	thrpool_add_job(back_channel_send_thread_pool,
	    gfs_client_replication_request_request, arg);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * SLEEPS: shoundn't
 *	but gfm_async_server_put_reply is calling peer_sender_lock() XXX FIXME
 */
static gfarm_error_t
gfm_async_server_replication_result(struct host *host,
	struct peer *peer, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_int32_t src_errcode, dst_errcode;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	gfarm_int64_t handle;
	gfarm_off_t filesize;
	static const char diag[] = "GFM_PROTO_REPLICATION_RESULT";

	/*
	 * The reason why this protocol returns not only handle
	 * but also ino and gen is because it's possible that
	 * this request may arrive earlier than the result of
	 * GFS_PROTO_REPLICATION_REQUEST due to race condition.
	 */
	if ((e = gfm_async_server_get_request(peer, size, diag, "llliil",
	    &ino, &gen, &handle, &src_errcode, &dst_errcode, &filesize))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	giant_lock(); /* XXX FIXME: deadlock */
	e = host_replicated(host, ino, gen, handle,
	    src_errcode, dst_errcode, filesize);
	giant_unlock();

	/*
	 * XXX FIXME
	 * There is a slight possibility of deadlock in the following call,
	 * because currently this is called in the context
	 * of back_channel_recv_thread_pool,
	 * although it unlikely blocks, since this is a reply.
	 */
	return (gfm_async_server_put_reply(host, peer, xid, diag, e, ""));
}

static gfarm_error_t
async_back_channel_protocol_switch(struct host *host, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_int32_t request;

	e = gfp_xdr_recv_request_command(client, 0, &size, &request);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	switch (request) {
	case GFM_PROTO_REPLICATION_RESULT:
		e = gfm_async_server_replication_result(host, peer, xid, size);
		break;
	default:
		gflog_error(GFARM_MSG_1001989,
		    "(back channel) unknown request %d "
		    "(xid:%d size:%d), reset",
		    (int)request, (int)xid, (int)size);
		e = gfp_xdr_purge(client, 0, size);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		break;
	}
	return (e);
}

static gfarm_error_t
async_back_channel_service(struct host *host,
	struct peer *peer, gfp_xdr_async_peer_t async)
{
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	enum gfp_xdr_msg_type type;
	gfp_xdr_xid_t xid;
	size_t size;
	gfarm_int32_t rv;

	e = gfp_xdr_recv_async_header(conn, 0, &type, &xid, &size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	switch (type) {
	case GFP_XDR_TYPE_REQUEST:
		e = async_back_channel_protocol_switch(host, peer, xid, size);
		break;
	case GFP_XDR_TYPE_RESULT:
		e = gfp_xdr_callback_async_result(async, xid, size, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1001990,
			    "(back channel) unknown reply "
			    "xid:%d size:%d", (int)xid, (int)size);
			e = gfp_xdr_purge(conn, 0, size);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1001991,
				    "skipping %d bytes: %s",
				    (int)size, gfarm_error_string(e));
		} else if (IS_CONNECTION_ERROR(rv)) {
			e = rv;
		}
		break;
	default:
		gflog_fatal(GFARM_MSG_1001992,
		    "back_channel_service: type %d", type);
		/*NOTREACHED*/
		e = GFARM_ERR_PROTOCOL;
		break;
	}
	return (e);
}

#ifdef COMPAT_GFARM_2_3
static gfarm_error_t
sync_back_channel_service(struct host *host, struct peer *peer)
{
	gfarm_int32_t (*callback)(void *, size_t);
	void *arg;

	if (!host_get_callback(host, &callback, &arg)) {
		gflog_error(GFARM_MSG_UNFIXED, "extra data from back channel");
		return (GFARM_ERR_PROTOCOL);
	}
	return ((*callback)(arg, -1));
}
#endif

static void *
back_channel_main(void *arg)
{
	struct peer *peer0 = arg, *peer;
	struct host *host = peer_get_host(peer0);;
	gfp_xdr_async_peer_t async;
	gfarm_error_t e;

	e = host_receiver_lock(host, &peer);
	if (e != GFARM_ERR_NO_ERROR) /* already disconnected */
		return (NULL);
	/*
	 * the following ensures that the bach_channel connection is
	 * not switched to another one.
	 */
	if (peer != peer0)
		return (NULL);
	async = peer_get_async(peer);

	do {
		if (peer_had_protocol_error(peer)) {
			host_disconnect_request(host);
			return (NULL);
		}
#ifdef COMPAT_GFARM_2_3
		if (async != NULL) {
#endif
			e = async_back_channel_service(host, peer, async);
#ifdef COMPAT_GFARM_2_3
		} else {
			e = sync_back_channel_service(host, peer);
		}
#endif
		if (e == GFARM_ERR_UNEXPECTED_EOF) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "back_channel(%s): disconnected", host_name(host));
		} else if (IS_CONNECTION_ERROR(e)) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "back_channel(%s): request error, reset: %s",
			     host_name(host), gfarm_error_string(e));
		}
		if (IS_CONNECTION_ERROR(e)) {
			host_disconnect_request(host);
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

	host_receiver_unlock(host);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static gfarm_error_t
gfm_server_switch_back_channel_common(struct peer *peer, int from_client,
	int version, const char *diag)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2;
	struct host *host;
	gfp_xdr_async_peer_t async = NULL;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	host = NULL;
#endif

	giant_lock();
	if (from_client) {
		gflog_debug(GFARM_MSG_1001995,
			"Operation not permitted: from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001996,
			"Operation not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (version >= GFS_PROTOCOL_VERSION_V2_4 &&
	    (e = gfp_xdr_async_peer_new(&async)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: gfp_xdr_async_peer_new(): %s",
		    diag, gfarm_error_string(e));
	} else {		
		if (host_is_up(host)) /* throw away old connetion */
			host_disconnect(host);

		peer_set_async(peer, async);
		host_peer_set(host, peer, version);
	}
	giant_unlock();
	if (version < GFS_PROTOCOL_VERSION_V2_4)
		e2 = gfm_server_put_reply(peer, diag, e, "");
	else
		e2 = gfm_server_put_reply(peer, diag, e, "i", 0 /*XXX FIXME*/);
	if (e2 != GFARM_ERR_NO_ERROR)
		return (e2);

	if (debug_mode)
		gflog_debug(GFARM_MSG_1000404, "gfp_xdr_flush");
	e2 = gfp_xdr_flush(peer_get_conn(peer));
	if (e2 != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000405,
		    "%s: protocol flush: %s",
		    diag, gfarm_error_string(e));
	else if (e == GFARM_ERR_NO_ERROR) {
		peer_set_protocol_handler(peer,
		    back_channel_recv_thread_pool,
		    back_channel_main);
		peer_watch_access(peer);

		callout_setfunc(host_status_callout(host),
		    back_channel_send_thread_pool,
		    gfs_client_status_request, host);
		thrpool_add_job(
		    back_channel_send_thread_pool,
		    gfs_client_status_request, host);
	}

	return (e2);
}

#ifdef COMPAT_GFARM_2_3

gfarm_error_t
gfm_server_switch_back_channel(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_SWITCH_BACK_CHANNEL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, from_client,
	    GFS_PROTOCOL_VERSION_V2_3, diag);

	return (e);
}

#endif /* defined(COMPAT_GFARM_2_3) */

gfarm_error_t
gfm_server_switch_async_back_channel(struct peer *peer, int from_client,
	int skip)
{
	gfarm_error_t e;
	gfarm_int32_t version;
	gfarm_int64_t gfsd_cookie;
	static const char diag[] = "GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL";

	e = gfm_server_get_request(peer, diag, "il", &version, &gfsd_cookie);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, from_client,
	    version, diag);

	return (e);
}

void
back_channel_init(void)
{
	gfarm_error_t e;

	/* XXX FIXME: use different config parameter */
	back_channel_recv_thread_pool = thrpool_new(
	    gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "receiving from filesystem nodes");
	back_channel_send_thread_pool = thrpool_new(
	    gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "sending to filesystem nodes");
	if (back_channel_recv_thread_pool == NULL ||
	    back_channel_send_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1001998,
		    "filesystem node thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);

	e = create_detached_thread(remover, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "create_detached_thread(remover): %s",
		    gfarm_error_string(e));

}

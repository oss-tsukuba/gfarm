/*
 * $Id$
 */

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
#include "thrpool.h"
#include "callout.h"
#include "host.h"
#include "inode.h"

#include "back_channel.h"

struct thread_pool *gfsd_thread_pool;

/* end of the back channel session */
static void
back_channel_disconnect(struct peer *peer)
{
	struct host *host = peer_get_host(peer);

	
	giant_lock();
	host_peer_unset(host);
	/*
	 * NOTE: this shouldn't need db_begin()/db_end()
	 * at least for now,
	 * because only externalized descriptor needs the calls.
	 */
	peer_free(peer);
	giant_unlock();
}

gfarm_error_t
gfm_async_server_get_request(struct peer *peer, size_t size,
	const char *diag, const char *format, ...)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrecv_request_parameters(client, 0, &size, format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfm_async_server_put_reply(struct peer *peer, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t e, char *format, ...)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	va_list ap;

	va_start(ap, format);
	e = gfp_xdr_vsend_async_result(client, xid, e, format, &ap);
	va_end(ap);

	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(client);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "async server %s receiving parameter: %s",
		    diag, gfarm_error_string(e));
	return (e);
}


static gfarm_error_t
gfs_client_rpc_back_channel(struct peer *peer, const char *diag, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;
	struct gfp_xdr *conn;

	if (peer == NULL || (conn = peer_get_conn(peer)) == NULL)
		return (GFARM_ERR_SOCKET_IS_NOT_CONNECTED);

	va_start(ap, format);
	e = gfp_xdr_vrpc(conn, 0, command, &errcode, &format, &ap);
	va_end(ap);
	if (IS_CONNECTION_ERROR(e)) {
		/* back channel is disconnected */
		gflog_warning(GFARM_MSG_1000400,
		    "back channel disconnected: %s",
			      gfarm_error_string(e));
		giant_lock();
		host_peer_unset(peer_get_host(peer));
		giant_unlock();
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000401,
		    "%s: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_fhremove(struct peer *peer, gfarm_ino_t ino, gfarm_uint64_t gen)
{
	return (gfs_client_rpc_back_channel(
			peer, "fhremove", GFS_PROTO_FHREMOVE,
			"ll/", ino, gen));
}

gfarm_error_t
gfs_client_status(struct peer *peer,
	double *loadavg_1min, double *loadavg_5min, double *loadavg_15min,
	gfarm_off_t *disk_used, gfarm_off_t *disk_avail)
{
	return (gfs_client_rpc_back_channel(
			peer, "status", GFS_PROTO_STATUS, "/fffll",
			loadavg_1min, loadavg_5min, loadavg_15min,
			disk_used, disk_avail));
}

gfarm_error_t
gfs_async_client_send_request(struct peer *peer,
	gfarm_int32_t (*callback)(void *, size_t), void *closure,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfp_xdr_vsend_async_request(peer_get_conn(peer),
	    peer_get_async(peer), callback, closure, command, format, &ap);
	va_end(ap);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(peer_get_conn(peer));
	return (e);
}

gfarm_error_t
gfs_async_client_recv_result(struct peer *peer, size_t size,
	const char *format, ...)
{
	gfarm_error_t e;
	gfarm_int32_t errcode;
	struct gfp_xdr *conn = peer_get_conn(peer);
	va_list ap;

	va_start(ap, format);
	e = gfp_xdr_vrpc_result_sized(conn, 0, &size, &errcode, &format, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "async gfs_client rpc result: %s", gfarm_error_string(e));
	} else if (size != 0) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "async gfs_client rpc result: protocol residual %d",
		    (int)size);
		if ((e = gfp_xdr_purge(conn, 0, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_UNFIXED,
			    "async gfs_client rpc result: skipping: %s",
			    gfarm_error_string(e));
		e = GFARM_ERR_PROTOCOL;
	} else {
		e = errcode;
	}
	return (e);
}

gfarm_int32_t gfs_async_client_status_result(void *, size_t);

/* this function is called via callout */
void *
gfs_async_client_status_request(void *arg)
{
	gfarm_error_t e;
	struct peer *peer = arg;
	struct host *h = peer_get_host(peer);

	if (host_peer_unset_pending(h)) { /* avoid race condition of callout */
		callout_ack(host_status_callout(h));
		giant_lock();
		host_peer_unset(h);
		giant_unlock();
		return (NULL);
	}
	peer_io_lock(peer);
	e = gfs_async_client_send_request(peer,
	    gfs_async_client_status_result, peer, GFS_PROTO_STATUS, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfs_async_client_status_request: %s",
		    gfarm_error_string(e));
		peer_record_protocol_error(peer);
	}
	peer_io_unlock(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

/* peer_io_lock() should be held before calling this */
gfarm_int32_t
gfs_async_client_status_result(void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = arg;
	struct host *h = peer_get_host(peer);
	struct host_status st;

	e = gfs_async_client_recv_result(peer, size, "fffll",
	    &st.loadavg_1min, &st.loadavg_5min, &st.loadavg_15min,
	    &st.disk_used, &st.disk_avail);
	if (e == GFARM_ERR_NO_ERROR) {
		host_status_update(h, &st);
	} else {
		host_status_disable(h);
	}
	callout_reset(host_status_callout(h),
	    gfarm_metadb_heartbeat_interval * 1000000,
	    gfsd_thread_pool, gfs_async_client_status_request, peer);
	return (e);
}

/* peer_io_lock() should be held before calling this */
gfarm_int32_t
gfs_async_client_replication_request_result(void *arg, size_t size)
{
	struct file_replicating *fr = arg;
	struct peer *peer = host_peer(fr->dst);
	gfarm_int32_t handle;
	gfarm_error_t e;

	e = gfs_async_client_recv_result(peer, size, "l", &handle);
	giant_lock();
	if (e == GFARM_ERR_NO_ERROR)
		file_replicating_set_handle(fr, handle);
	else
		file_replicating_free(fr);
	giant_unlock();
	return (e);
}

struct gfs_async_client_status_request_arg {
	char *srchost;
	int srcport;
	struct host *dst;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	struct file_replicating *fr;
};

void *
gfs_async_client_replication_request_request(void *closure)
{
	struct gfs_async_client_status_request_arg *arg = closure;
	char *srchost = arg->srchost;
	gfarm_int32_t srcport = arg->srcport;
	struct host *dst = arg->dst;
	gfarm_ino_t ino = arg->ino;
	gfarm_int64_t gen = arg->gen;
	struct file_replicating *fr = arg->fr;
	struct peer *peer = host_peer(dst);
	gfarm_error_t e;

	free(arg);

	if (peer == NULL) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "replication from %s to %s is canceled since %s is down", 
		    srchost, host_name(dst), host_name(dst));
		free(srchost);
		return (NULL);
	}

	peer_io_lock(peer);
	e = gfs_async_client_send_request(peer,
	    gfs_async_client_replication_request_result, fr,
	    GFS_PROTO_REPLICATION_REQUEST, "sill",
	    srchost, srcport, ino, gen);
	peer_io_unlock(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

gfarm_error_t
async_back_channel_replication_request(char *srchost, int srcport,
	struct host *dst, gfarm_ino_t ino, gfarm_int64_t gen,
	struct file_replicating *fr)
{
	struct gfs_async_client_status_request_arg *arg;

	GFARM_MALLOC(arg);
	if (arg == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "async_back_channel_replication_request: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}
	arg->srchost = srchost;
	arg->srcport = srcport;
	arg->dst = dst;
	arg->ino = ino;
	arg->gen = gen;
	arg->fr = fr;
	thrpool_add_job(gfsd_thread_pool,
	    gfs_async_client_replication_request_request, arg);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_async_server_replication_result(struct peer *peer,
	gfp_xdr_xid_t xid, size_t size, struct host *h)
{
	gfarm_error_t e;
	gfarm_int32_t errcode;
	gfarm_int64_t handle;
	gfarm_off_t filesize;
	const char diag[] = "replication_result";

	if ((e = gfm_async_server_get_request(peer, size, diag,
	    "ill", &errcode, &handle, &filesize)) != GFARM_ERR_NO_ERROR)
		return (e);

	giant_lock();
	e = host_replicated(h, errcode, handle, filesize);
	giant_unlock();

	return (gfm_async_server_put_reply(peer, xid, diag, e, ""));
}

gfarm_error_t
async_back_channel_protocol_switch(struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	struct gfp_xdr *conn = peer_get_conn(peer);
	struct host *h = peer_get_host(peer);
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_int32_t request;

	e = gfp_xdr_recv_request_command(conn, 0, &size, &request);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	switch (request) {
	case GFM_PROTO_REPLICATION_RESULT:
		e = gfm_async_server_replication_result(peer, xid, size, h);
		break;
	default:
		gflog_error(GFARM_MSG_UNFIXED,
		    "(back channel) unknown request %d "
		    "(xid:%d size:%d), reset",
		    (int)request, (int)xid, (int)size);
		e = gfp_xdr_purge(conn, 0, size);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		break;
	}
	return (e);
}

gfarm_error_t
async_back_channel_service(struct peer *peer)
{
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	enum gfp_xdr_msg_type type;
	gfp_xdr_xid_t xid;
	size_t size;
	gfarm_int32_t rv;

	e = gfp_xdr_recv_async_header(conn, 0, &type, &xid, &size);
	if (e == GFARM_ERR_NO_ERROR) {
		if (type == GFP_XDR_TYPE_REQUEST) {
			e = async_back_channel_protocol_switch(peer,
			    xid, size);
			if (e == GFARM_ERR_NO_ERROR)
				return (e);
		} else if (type == GFP_XDR_TYPE_RESULT) {
			e = gfp_xdr_callback_async_result(peer_get_async(peer),
			    xid, size, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNFIXED,
				    "(back channel) unknown reply "
				    "xid:%d size:%d", (int)xid, (int)size);
				e = gfp_xdr_purge(conn, 0, size);
				if (e != GFARM_ERR_NO_ERROR)
					gflog_error(GFARM_MSG_UNFIXED,
					    "skipping %d bytes: %s",
					    (int)size, gfarm_error_string(e));
			} else if (IS_CONNECTION_ERROR(rv)) {
				e = rv;
			}
			return (e);
		} else {
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "async_back_channel_service: type %d", type);
		}
	}
	if (e == GFARM_ERR_UNEXPECTED_EOF) {
		gflog_error(GFARM_MSG_UNFIXED, "back channel disconnected");
	} else {
		gflog_error(GFARM_MSG_UNFIXED,
		    "back channel request error, reset: %s",
		    gfarm_error_string(e));
	}
	return (e);
}

void *
async_back_channel_main(void *arg)
{
	struct peer *peer = arg;
	gfarm_error_t e;

	peer_io_lock(peer);
	do {
		if (peer_had_protocol_error(peer)) {
			peer_io_unlock(peer);
			back_channel_disconnect(peer);
			return (NULL);
		}
		if ((e = async_back_channel_service(peer))
		    != GFARM_ERR_NO_ERROR &&
		    IS_CONNECTION_ERROR(e)) {
			peer_io_unlock(peer);
			back_channel_disconnect(peer);
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
	peer_io_unlock(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

#ifdef COMPAT_GFARM_2_3

void *
remover(void *arg)
{
	struct peer *peer = arg;
	struct host *host = peer_get_host(peer);
	gfarm_error_t e;
	struct timeval now;
	struct timespec timeout;

	gflog_notice(GFARM_MSG_1000402, "heartbeat interval: %d sec",
	    gfarm_metadb_heartbeat_interval);
	while (1) {
		e = host_update_status(host);
		if (peer_had_protocol_error(peer) ||
		    e != GFARM_ERR_NO_ERROR)
			break;

		/* timeout: 3 min */
		gettimeofday(&now, NULL);
		timeout.tv_sec = now.tv_sec + gfarm_metadb_heartbeat_interval;
		timeout.tv_nsec = now.tv_usec * 1000;

		e = host_remove_replica(host, &timeout);
		if (peer_had_protocol_error(peer))
			break;
		if (e == GFARM_ERR_OPERATION_TIMED_OUT ||
		    e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
			continue;
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	gflog_warning(GFARM_MSG_1000403,
	    "remover: %s", peer_had_protocol_error(peer) ?
		"protocol error" : gfarm_error_string(e));
	back_channel_disconnect(peer);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

#endif /* defined(COMPAT_GFARM_2_3) */

gfarm_error_t
gfm_server_switch_back_channel_common(struct peer *peer, int from_client,
	int version, int *okp, const char *msg)
{
	gfarm_error_t e, e2;
	struct host *h;
	struct callout *callout;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	h = NULL;
#endif
	*okp = 0;

	giant_lock();
	if (from_client)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((h = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else {
		callout = NULL;
		e = GFARM_ERR_NO_ERROR;
		if (version >= GFS_PROTOCOL_VERSION_V2_4) { /* async protocol */
			callout = callout_new();
			if (callout == NULL)
				e = GFARM_ERR_NO_MEMORY;
		}
		if (e == GFARM_ERR_NO_ERROR) {
			host_peer_disconnect(h);
			host_peer_set(h, peer, version, callout);
		}
	}
	giant_unlock();
	e2 = gfm_server_put_reply(peer, msg, e, "");
	peer_io_unlock(peer);
	if (e2 == GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_debug(GFARM_MSG_1000404, "gfp_xdr_flush");
		e2 = gfp_xdr_flush(peer_get_conn(peer));
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_warning(GFARM_MSG_1000405,
			    "%s: protocol flush: %s",
			    msg, gfarm_error_string(e));
		else if (e == GFARM_ERR_NO_ERROR)
			*okp = 1;
	}

	return (e2);
}

#ifdef COMPAT_GFARM_2_3

gfarm_error_t
gfm_server_switch_back_channel(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	int ok;
	const char msg[] = "switch_back_channel";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, from_client,
	    GFS_PROTOCOL_VERSION_V2_3, &ok, msg);

	/* XXX FIXME - make sure there is at most one running remover thread */
	if (e == GFARM_ERR_NO_ERROR && ok)
		e = create_detached_thread(remover, peer);

	return (e);
}

#endif /* defined(COMPAT_GFARM_2_3) */

gfarm_error_t
gfm_server_switch_async_back_channel(struct peer *peer, int from_client,
	int skip)
{
	gfarm_error_t e;
	gfarm_int32_t version;
	int ok;
	const char msg[] = "switch_async_back_channel";

	e = gfm_server_get_request(peer, msg, "i", &version);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_switch_back_channel_common(peer, from_client,
	    version, &ok, msg);

	if (e == GFARM_ERR_NO_ERROR && ok) {
		if ((e = peer_set_async(peer)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "peer_set_async: %s", gfarm_error_string(e));
			back_channel_disconnect(peer);
		} else {
			thrpool_add_job(gfsd_thread_pool,
			    gfs_async_client_status_request, peer);
			peer_set_protocol_handler(peer, gfsd_thread_pool,
			    async_back_channel_main);
		}
	}

	return (e);
}

void
back_channel_init(void)
{
	/* XXX FIXME: use different config parameter */
	gfsd_thread_pool = thrpool_new(gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "filesystem nodes");
	if (gfsd_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "filesystem node thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);
}

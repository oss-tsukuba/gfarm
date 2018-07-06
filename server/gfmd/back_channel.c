/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <gfarm/gflog.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gflog_reduced.h"
#include "thrsubr.h"

#include "context.h"
#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "config.h"

#include "gfmd.h"
#include "peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "thrpool.h"
#include "callout.h"
#include "abstract_host.h"
#include "host.h"
#include "inode.h"
#include "dead_file_copy.h"

#include "back_channel.h"

static struct thread_pool *back_channel_send_thread_pool;
static struct thread_pool *proto_status_send_thread_pool;

static struct peer_watcher *back_channel_recv_watcher;

/* SF.net #429: use short timeout, to make GFS_PROTO_STATUS success */
#define GFS_PROTO_REPLICATION_REQUEST_TIMEOUT	1000000  /*  1.0 sec. */
#define GFS_PROTO_FHREMOVE_TIMEOUT		100000   /*  0.1 sec. */

/*
 * responsibility to call host_disconnect_request():
 *
 * back_channel_main() is the handler of back_channel_recv_watcher.
 *
 * gfm_client_channel_send_request() (and other leaf functions) is responsible
 * for threads in back_channel_send_thread_pool.
 *
 * gfm_async_server_put_reply() should be responsible for threads in
 * back_channel_send_thread_pool too, but currently it's not because
 * it's called by threads for back_channel_recv_watcher. XXX FIXME
 */

static void
gfs_client_status_disconnect_or_message(struct host *host,
	struct peer *peer, const char *proto, const char *op,
	const char *condition)
{
	if (peer != NULL) { /* to make the race condition harmless */
		gfm_server_channel_disconnect_request(
		    host_to_abstract_host(host), peer,
		    proto, op, condition);
	} else {
		gfm_server_channel_already_disconnected_message(
		    host_to_abstract_host(host),
		    proto, op, condition);
	}
}

/* host_receiver_lock() must be already called here by back_channel_main() */
static gfarm_error_t
gfm_async_server_get_request(struct peer *peer, size_t size,
	const char *diag, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_server_channel_vget_request(peer, size, diag, format, &ap);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfm_async_server_put_reply(struct host *host,
	struct peer *peer, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_server_channel_vput_reply(
	    host_to_abstract_host(host), peer, xid, diag,
	    errcode, format, &ap);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfs_client_recv_result_and_error(struct peer *peer, struct host *host,
	size_t size, gfarm_error_t *errcodep,
	const char *diag, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vrecv_result(
	    peer, host_to_abstract_host(host), size, diag,
	    &format, errcodep, &ap);
	va_end(ap);
	return (e);
}

static gfarm_error_t
gfs_client_recv_result(struct peer *peer, struct host *host,
       size_t size, const char *diag, const char *format, ...)
{
	gfarm_error_t e, errcode;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vrecv_result(
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

static gfarm_int32_t
gfs_client_status_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct host *host = arg;
	struct host_status st;
	static const char diag[] = "GFS_PROTO_STATUS";

	e = gfs_client_recv_result(peer, host,
	    size, diag, "fffll",
	    &st.loadavg_1min, &st.loadavg_5min, &st.loadavg_15min,
	    &st.disk_used, &st.disk_avail);
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
#if 0
	struct peer *peer = p;
	struct host *host = arg;
#endif
}

static gfarm_error_t
gfs_client_send_request(struct host *host,
	struct peer *peer0, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure, long timeout_microsec,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vsend_request(
	    host_to_abstract_host(host), peer0, diag,
	    result_callback, disconnect_callback, closure,
#ifdef COMPAT_GFARM_2_3
	    host_set_callback,
#endif
	    timeout_microsec, command, format, &ap);
	va_end(ap);
	return (e);
}

/* this function is called via callout */
static void *
gfs_client_status_request(void *arg)
{
	gfarm_error_t e;
	struct host *host = arg;
	struct peer *peer = host_get_peer(host); /* increment refcount */
	static const char diag[] = "GFS_PROTO_STATUS";

	if (host_status_reply_is_waiting(host)) {
		gfs_client_status_disconnect_or_message(host, peer,
		    diag, "request", "no status");
		host_put_peer(host, peer); /* decrement refcount */
		return (NULL);
	}

	/*
	 * schedule here instead of the end of gfs_client_status_result(),
	 * because gfs_client_send_request() may block, and we should
	 * detect it at next call of gfs_client_status_request().
	 */
	callout_schedule(host_status_callout(host),
	    gfarm_metadb_heartbeat_interval * 1000000);

	host_status_reply_waiting_set(host);
	e = gfs_client_send_request(host, NULL, diag,
	    gfs_client_status_result, gfs_client_status_free, host,
	    GFM_CLIENT_CHANNEL_TIMEOUT_INFINITY, GFS_PROTO_STATUS, "");
	if (e != GFARM_ERR_NO_ERROR) {
		host_status_reply_waiting_reset(host);
		gflog_error(GFARM_MSG_1001986,
		    "gfs_client_status_request: %s",
		    gfarm_error_string(e));
	}

	host_put_peer(host, peer); /* decrement refcount */

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static gfarm_int32_t
gfs_client_fhremove_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct dead_file_copy *dfc = arg;
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_recv_result(peer, host, size, diag, "");
	removal_finishedq_enqueue(dfc, e);
	return (e);
}

/* both giant_lock and peer_table_lock are held before calling this function */
static void
gfs_client_fhremove_free(void *p, void *arg)
{
#if 0
	struct peer *peer = p;
#endif
	struct dead_file_copy *dfc = arg;

	removal_finishedq_enqueue(dfc, GFARM_ERR_CONNECTION_ABORTED);
}

#define SAME_WARNING_TRIGGER	10	/* check reduced mode */
#define SAME_WARNING_THRESHOLD	30	/* more than this -> reduced mode */
#define SAME_WARNING_DURATION	600	/* seconds to measure the limit */
#define	SAME_WARNING_INTERVAL	60	/* seconds: interval of reduced log */

struct gflog_reduced_state busy_state = GFLOG_REDUCED_STATE_INITIALIZER(
	SAME_WARNING_TRIGGER,
	SAME_WARNING_THRESHOLD,
	SAME_WARNING_DURATION,
	SAME_WARNING_INTERVAL);

static void *
gfs_client_fhremove_request(void *closure)
{
	struct dead_file_copy *dfc = closure;
	gfarm_ino_t ino = dead_file_copy_get_ino(dfc);
	gfarm_int64_t gen = dead_file_copy_get_gen(dfc);
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_send_request(host, NULL, diag,
	    gfs_client_fhremove_result, gfs_client_fhremove_free, dfc,
	    GFS_PROTO_FHREMOVE_TIMEOUT, GFS_PROTO_FHREMOVE, "ll", ino, gen);
	if (e == GFARM_ERR_NO_ERROR) {
		return (NULL);
	} else if (e == GFARM_ERR_DEVICE_BUSY) {
		gflog_reduced_info(GFARM_MSG_1002284, &busy_state,
		    "%s(%lld, %lld, %s): "
		    "busy, waiting for some time", diag,
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
			thrpool_add_job_low_priority(
			    back_channel_send_thread_pool,
			    gfs_client_fhremove_request, dfc);
		else /* make this dfcstate_deferred */
			removal_finishedq_enqueue(dfc,
			    GFARM_ERR_NO_ROUTE_TO_HOST);
	}

	/*NOTREACHED*/
	return (NULL);
}

/*
 * GFS_PROTO_REPLICATION_REQUEST didn't exist before gfarm-2.4.0
 */
static gfarm_int32_t
gfs_client_replication_request_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct file_replicating *fr = arg;
	gfarm_int64_t handle;
	struct host *dst;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	gfarm_error_t e, errcode, e2;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST result";

	/* XXX FIXME, src_err and dst_err should be passed separately */
	e = gfs_client_recv_result_and_error(peer, fr->dst, size, &errcode,
	    diag, "l", &handle);
	/* XXX FIXME: deadlock */
	giant_lock();
	if (e == GFARM_ERR_NO_ERROR && errcode == GFARM_ERR_NO_ERROR)
		file_replicating_set_handle(fr, handle);
	else {
		dst = fr->dst;
		ino = inode_get_number(fr->inode);
		gen = fr->igen;
		if (e != GFARM_ERR_NO_ERROR)
			e2 = peer_replicated(peer, dst, ino, gen,
			    GFS_PROTO_REPLICATION_HANDLE_INVALID,
			    GFARM_ERR_NO_ERROR, e, -1,
			    0, NULL, 0, NULL, 0);
		else if (IS_CONNECTION_ERROR(errcode))
			e2 = peer_replicated(peer, dst, ino, gen,
			    GFS_PROTO_REPLICATION_HANDLE_INVALID,
			    errcode, GFARM_ERR_NO_ERROR, -1,
			    0, NULL, 0, NULL, 0);
		else
			e2 = peer_replicated(peer, dst, ino, gen,
			    GFS_PROTO_REPLICATION_HANDLE_INVALID,
			    GFARM_ERR_NO_ERROR, errcode, -1,
			    0, NULL, 0, NULL, 0);
		gflog_debug(GFARM_MSG_1002359,
		    "%s: (%s, %lld:%lld): aborted: %s - (%s, %s)",
		    diag, host_name(dst), (long long)ino, (long long)gen,
		    gfarm_error_string(errcode),
		    gfarm_error_string(e), gfarm_error_string(e2));
	}
	giant_unlock();
	return (e);
}

/*
 * GFS_PROTO_REPLICATION_CKSUM_REQUEST didn't exist before gfarm-2.6.0
 */
static gfarm_int32_t
gfs_client_replication_cksum_request_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct file_replicating *fr = arg;
	gfarm_int64_t handle = GFS_PROTO_REPLICATION_HANDLE_INVALID;
	struct host *dst;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	gfarm_error_t e, e2;
	gfarm_int32_t src_errcode = GFARM_ERR_NO_ERROR;
	gfarm_int32_t dst_errcode = GFARM_ERR_NO_ERROR;
	static const char diag[] =
	    "GFS_PROTO_REPLICATION_CKSUM_REQUEST result";

	e = gfs_client_recv_result_and_error(peer, fr->dst, size, &dst_errcode,
	    diag, "li", &handle, &src_errcode);
	/* XXX FIXME: deadlock */
	giant_lock();
	if (e == GFARM_ERR_NO_ERROR && dst_errcode == GFARM_ERR_NO_ERROR)
		file_replicating_set_handle(fr, handle);
	if (e != GFARM_ERR_NO_ERROR ||
	    dst_errcode != GFARM_ERR_NO_ERROR ||
	    src_errcode != GFARM_ERR_NO_ERROR) {
		dst = fr->dst;
		ino = inode_get_number(fr->inode);
		gen = fr->igen;
		if (e != GFARM_ERR_NO_ERROR) {
			dst_errcode = e;
			src_errcode = GFARM_ERR_NO_ERROR;
		}
		e2 = peer_replicated(peer, dst, ino, gen,
		    handle, src_errcode, dst_errcode, -1,
		    1, NULL, 0, NULL, 0);
		gflog_debug(GFARM_MSG_1004036,
		    "%s: (%s, %lld:%lld): aborted: %s/%s - (%s, %s)",
		    diag, host_name(dst), (long long)ino, (long long)gen,
		    gfarm_error_string(src_errcode),
		    gfarm_error_string(dst_errcode),
		    gfarm_error_string(e), gfarm_error_string(e2));
	}
	giant_unlock();
	return (e);
}

/* both giant_lock and peer_table_lock are held before calling this function */
static void
gfs_client_replication_request_free(void *p, void *arg)
{
	struct peer *peer = p;
	struct file_replicating *fr = arg;
	struct host *dst = fr->dst;
	gfarm_ino_t ino = inode_get_number(fr->inode);
	gfarm_int64_t gen = fr->igen;
	gfarm_error_t e;
	int cksum_protocol =
	    (file_replicating_get_cksum_request_flags(fr) &
	     GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_ENABLED) != 0;
	const char *diag = cksum_protocol ?
	    "GFS_PROTO_REPLICATION_CKSUM_REQUEST free" :
	    "GFS_PROTO_REPLICATION_REQUEST free";

	e = peer_replicated(peer, dst, ino, gen,
	    GFS_PROTO_REPLICATION_HANDLE_INVALID,
	    GFARM_ERR_NO_ERROR, GFARM_ERR_CONNECTION_ABORTED, -1,
	    cksum_protocol, NULL, 0, NULL, 0);
	gflog_debug(GFARM_MSG_1002360,
	    "%s: (%s, %lld:%lld): connection aborted: %s",
	    diag, host_name(dst), (long long)ino, (long long)gen,
	    gfarm_error_string(e));
}

struct gfs_client_replication_request_arg {
	char *srchost;
	int srcport;
	struct host *dst;
	gfarm_ino_t ino;
	gfarm_int64_t gen;

	struct file_replicating *fr;

	/* only used by GFS_PROTO_REPLICATION_CKSUM_REQUEST */
	gfarm_int64_t filesize;
	char *cksum_type;
	size_t cksum_len;
	char *cksum;
	gfarm_int32_t cksum_request_flags;
};

static void *
gfs_client_replication_request_request(void *closure)
{
	struct gfs_client_replication_request_arg *arg = closure;
	struct peer *peer = file_replicating_get_peer(arg->fr);
	gfarm_error_t e, e2;
	int cksum_protocol = (arg->cksum_request_flags &
	    GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_ENABLED) != 0;
	const char *diag = cksum_protocol ?
	    "GFS_PROTO_REPLICATION_CKSUM_REQUEST request" :
	    "GFS_PROTO_REPLICATION_REQUEST request";

	if (cksum_protocol) {
		e = gfs_client_send_request(arg->dst, peer, diag,
		    gfs_client_replication_cksum_request_result,
		    gfs_client_replication_request_free, arg->fr,
		    GFS_PROTO_REPLICATION_REQUEST_TIMEOUT,
		    GFS_PROTO_REPLICATION_CKSUM_REQUEST, "silllsbi",
		    arg->srchost, arg->srcport, arg->ino, arg->gen,
		    arg->filesize, arg->cksum_type, arg->cksum_len, arg->cksum,
		    arg->cksum_request_flags &
		    ~GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_MASK);
		free(arg->cksum_type);
		free(arg->cksum);
	} else {
		e = gfs_client_send_request(arg->dst, peer, diag,
		    gfs_client_replication_request_result,
		    gfs_client_replication_request_free, arg->fr,
		    GFS_PROTO_REPLICATION_REQUEST_TIMEOUT,
		    GFS_PROTO_REPLICATION_REQUEST, "sill",
		    arg->srchost, arg->srcport, arg->ino, arg->gen);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		giant_lock(); /* XXX FIXME: deadlock */
		e2 = peer_replicated(peer, arg->dst, arg->ino, arg->gen,
		    GFS_PROTO_REPLICATION_HANDLE_INVALID,
		    GFARM_ERR_NO_ERROR, e, -1,
		    cksum_protocol, NULL, 0, NULL, 0);
		giant_unlock();
		gflog_debug(GFARM_MSG_1002361,
		    "%s: %s->(%s, %lld:%lld): aborted: %s (%s)", diag,
		    arg->srchost, host_name(arg->dst),
		    (long long)arg->ino, (long long)arg->gen,
		    gfarm_error_string(e), gfarm_error_string(e2));
	}
	free(arg);

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

gfarm_error_t
async_back_channel_replication_cksum_request(char *srchost, int srcport,
	struct host *dst, gfarm_ino_t ino, gfarm_int64_t gen,
	gfarm_int64_t filesize,
	char *cksum_type, size_t cksum_len, char *cksum,
	gfarm_int32_t cksum_request_flags,
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
	arg->filesize = filesize;
	arg->cksum_type = cksum_type;
	arg->cksum_len = cksum_len;
	arg->cksum = cksum;
	arg->cksum_request_flags = cksum_request_flags;
	assert(
	    (cksum_type != NULL &&
	     (cksum_request_flags &
	      GFS_PROTO_REPLICATION_CKSUM_REQFLAG_INTERNAL_ENABLED) != 0) ||
	    (cksum_type == NULL &&
	     cksum_len == 0 && cksum == NULL && cksum_request_flags == 0));
	thrpool_add_job(back_channel_send_thread_pool,
	    gfs_client_replication_request_request, arg);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
async_back_channel_replication_request(char *srchost, int srcport,
	struct host *dst, gfarm_ino_t ino, gfarm_int64_t gen,
	struct file_replicating *fr)
{
	return (async_back_channel_replication_cksum_request(srchost, srcport,
	    dst, ino, gen, -1, NULL, 0, NULL, 0, fr));
}

/*
 * SLEEPS: shoundn't
 *	but gfm_async_server_put_reply is calling peer_sender_lock() XXX FIXME
 */
static gfarm_error_t
gfm_async_server_replication_result(struct host *host,
	struct peer *peer, gfp_xdr_xid_t xid, size_t size, int cksum_protocol)
{
	gfarm_error_t e, e2;
	gfarm_int32_t src_errcode, dst_errcode;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	gfarm_int64_t handle;
	gfarm_int64_t trace_seq_num; /* for gfarm_file_trace */
	gfarm_off_t filesize;
	char *cksum_type = NULL;
	size_t cksum_len = 0;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	gfarm_int32_t cksum_result_flags = 0;
	const char *diag = cksum_protocol ?
	    "GFM_PROTO_REPLICATION_CKSUM_RESULT" :
	    "GFM_PROTO_REPLICATION_RESULT";

	/*
	 * The reason why these protocols return not only handle
	 * but also ino and gen is because it's possible that
	 * this request may arrive in the peer layer earlier
	 * than the result of GFS_PROTO_REPLICATION_REQUEST
	 * due to race condition of gfmd thread scheduling.
	 */
	if (cksum_protocol)
		e = gfm_async_server_get_request(peer, size, diag, "llliilsbi",
		    &ino, &gen, &handle,
		    &src_errcode, &dst_errcode, &filesize,
		    &cksum_type, sizeof(cksum), &cksum_len, cksum,
		    &cksum_result_flags);
	else
		e = gfm_async_server_get_request(peer, size, diag, "llliil",
		    &ino, &gen, &handle,
		    &src_errcode, &dst_errcode, &filesize);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	giant_lock(); /* XXX FIXME: deadlock */
	e = peer_replicated(peer, host, ino, gen, handle,
	    src_errcode, dst_errcode, filesize,
	    cksum_protocol, cksum_type, cksum_len, cksum, cksum_result_flags);
	free(cksum_type);
	trace_seq_num = trace_log_get_sequence_number(),
	giant_unlock();

	/*
	 * XXX FIXME
	 * There is a slight possibility of deadlock in the following call,
	 * because currently this is called in the context
	 * of threads for back_channel_recv_watcher,
	 * although it unlikely blocks, since this is a reply.
	 */
	e2 = gfm_async_server_put_reply(host, peer, xid, diag, e, "");

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR)
		gflog_trace(GFARM_MSG_1003320,
		    "%lld/////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    gfarm_host_get_self_name(), gfmd_port,
		    host_name(host), (long long int)ino, (long long int)gen);

	return (e2);
}

static gfarm_error_t
async_back_channel_protocol_switch(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	struct host *host = abstract_host_to_host(h);
	gfarm_error_t e;

	switch (request) {
	case GFM_PROTO_REPLICATION_RESULT:
		e = gfm_async_server_replication_result(host, peer, xid,
		    size, 0);
		break;
	case GFM_PROTO_REPLICATION_CKSUM_RESULT:
		e = gfm_async_server_replication_result(host, peer, xid,
		    size, 1);
		break;
	default:
		*unknown_request = 1;
		e = GFARM_ERR_PROTOCOL;
		break;
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
	return (gfm_server_channel_main(arg,
		async_back_channel_protocol_switch,
#ifdef COMPAT_GFARM_2_3
		sync_back_channel_free,
		sync_back_channel_service
#endif
		));
}

/* giant_lock is held before calling this function */
static void
back_channel_async_peer_free(struct peer *peer, gfp_xdr_async_peer_t async)
{
	gfp_xdr_async_peer_free(async, peer);
}

static gfarm_error_t
gfm_server_switch_back_channel_common(struct peer *peer, int from_client,
	int is_async, int version, const char *diag)
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
	} else if (is_async ? (
	    version <  GFS_PROTOCOL_VERSION_V2_4 ||
	    version >  GFS_PROTOCOL_VERSION_V2_6) :
	    version != GFS_PROTOCOL_VERSION_V2_3) {
		e = GFARM_ERR_PROTOCOL_NOT_SUPPORTED;
		gflog_info(GFARM_MSG_1004037,
		    "%s: %s@%s: unsupported protocol version %d",
		    diag, peer_get_username(peer), peer_get_hostname(peer),
		    version);
	} else if (is_async &&
	    (e = gfp_xdr_async_peer_new(&async)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002288,
		    "%s: gfp_xdr_async_peer_new(): %s",
		    diag, gfarm_error_string(e));
	}
	giant_unlock();
	if (!is_async)
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
		    diag, gfarm_error_string(e2));
	else if (e == GFARM_ERR_NO_ERROR) {
		if (host_is_up(host)) /* throw away old connetion */ {
			gflog_warning(GFARM_MSG_1002440,
			    "back_channel(%s): switching to new connection",
			    host_name(host));
			host_disconnect_request(host, NULL);
		}

		/*
		 * SF.net #1019
		 * need giant_lock during transition to back channel
		 */
		giant_lock();
		peer_set_async(peer, async);
		peer_set_watcher(peer, back_channel_recv_watcher);
		abstract_host_set_peer(host_to_abstract_host(host),
		    peer, version);

		(void)gfarm_sockbuf_apply_limit(peer_get_fd(peer),
		    SO_SNDBUF, gfarm_metadb_server_back_channel_sndbuf_limit,
		    "metadb_server_back_channel_sndbuf_limit");
		peer_watch_access(peer);
		giant_unlock();

		callout_setfunc(host_status_callout(host),
		    proto_status_send_thread_pool,
		    gfs_client_status_request, host);
		thrpool_add_job(
		    proto_status_send_thread_pool,
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
	    0, GFS_PROTOCOL_VERSION_V2_3, diag);

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
	    1, version, diag);

	return (e);
}

struct watcher *
back_channel_watcher(void)
{
	return (peer_watcher_get_watcher(back_channel_recv_watcher));
}

struct thread_pool *
back_channel_recv_thrpool(void)
{
	return (peer_watcher_get_thrpool(back_channel_recv_watcher));
}

void
back_channel_init(void)
{
	gfarm_error_t e;

	peer_set_free_async(back_channel_async_peer_free);

	back_channel_recv_watcher = peer_watcher_alloc(
	    /* XXX FIXME: use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    back_channel_main, "receiving from filesystem nodes");

	back_channel_send_thread_pool = thrpool_new(
	    /* XXX FIXME: use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    "sending to filesystem nodes");
	if (back_channel_send_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1001998,
		    "filesystem node thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);
	thrpool_set_jobq_low_priority_limit(back_channel_send_thread_pool,
	    gfarm_metadb_remover_queue_length);

	proto_status_send_thread_pool = thrpool_new(
	    /* XXX FIXME: use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    "sending heartbeats to filesystem nodes");
	if (proto_status_send_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1003498,
		    "GFS_PROTO_STATUS thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);

	e = create_detached_thread(remover, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1002289,
		    "create_detached_thread(remover): %s",
		    gfarm_error_string(e));

}

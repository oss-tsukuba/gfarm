#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "thrsubr.h"

#include "context.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "gfp_xdr.h" /* host.h currently needs this */

#include "subr.h"
#include "abstract_host.h"
#include "host.h"
#include "netsendq.h"
#include "netsendq_impl.h"
#include "back_channel.h"
#include "file_replication.h"
#include "inode.h"
#include "gfmd.h" /* gfmd_port */
#include "thrstatewait.h"

struct dead_file_copy;
struct file_replication {

	struct netsendq_entry qentry; /* must be first member */

	/*
	 * resources which are protected by the giant_lock
	 */

	/*
	 * ongoing replications on same inode,
	 * linked to struct inode_activity::u.f.rstate
	 */
	GFARM_HCIRCLEQ_ENTRY(file_replication) replications;

	struct inode *inode;
	gfarm_uint64_t igen; /* generation when replication started */

	struct host *src;

	/*
	 * old generation which should be removed just after
	 * the completion of the replication,
	 * or, NULL
	 */
	struct dead_file_copy *cleanup;

	int queued;

	gfarm_error_t src_errcode; /* qentry.result is dst_errcode */
	gfarm_int64_t handle; /* pid of destination side worker */
	gfarm_off_t filesize;
	struct gfarm_thr_statewait *statewait;
};

struct inode_replication_state {
	GFARM_HCIRCLEQ_HEAD(file_replication) same_inode_list;
};

static int outstanding_file_replications = 0;

struct host *
file_replication_get_dst(struct file_replication *fr)
{
	return (abstract_host_to_host(fr->qentry.abhost));
}

struct inode *
file_replication_get_inode(struct file_replication *fr)
{
	return (fr->inode);
}

gfarm_uint64_t
file_replication_get_gen(struct file_replication *fr)
{
	return (fr->igen);
}

struct dead_file_copy *
file_replication_get_dead_file_copy(struct file_replication *fr)
{
	return (fr->cleanup);
}

gfarm_int64_t
file_replication_get_handle(struct file_replication *fr)
{
	return (fr->handle);
}

void
file_replication_set_handle(struct file_replication *fr, gfarm_int64_t handle)
{
	fr->handle = handle;
}

gfarm_error_t
file_replication_new(struct inode *inode, gfarm_uint64_t gen,
	struct host *src, struct host *dst,
	struct dead_file_copy *deferred_cleanup,
	struct inode_replication_state **rstatep,
	struct file_replication **frp)
{
	struct file_replication *fr;
	struct inode_replication_state *irs = *rstatep;

	if (outstanding_file_replications >=
	    gfarm_outstanding_file_replication_limit)
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);

	GFARM_MALLOC(fr);
	if (fr == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (irs == NULL) {
		GFARM_MALLOC(irs);
		if (irs == NULL) {
			free(fr);
			return (GFARM_ERR_NO_MEMORY);
		}
		GFARM_HCIRCLEQ_INIT(irs->same_inode_list, replications);
		*rstatep = irs;
	}
	GFARM_HCIRCLEQ_INSERT_TAIL(irs->same_inode_list, fr, replications);

	netsendq_entry_init(&fr->qentry, &gfs_proto_replication_request_queue);
	fr->qentry.abhost = host_to_abstract_host(dst);

	fr->inode = inode;
	fr->igen = gen;
	fr->src = src;
	fr->cleanup = deferred_cleanup;
	fr->queued = 0;
	fr->handle = -1;
	fr->filesize = -1;
	fr->statewait = NULL;

	++outstanding_file_replications;

	*frp = fr;
	return (GFARM_ERR_NO_ERROR);
}

void
file_replication_free(struct file_replication *fr,
	struct inode_replication_state **rstatep)
{
	struct inode_replication_state *irs = *rstatep;

	--outstanding_file_replications;

	GFARM_HCIRCLEQ_REMOVE(fr, replications);
	if (GFARM_HCIRCLEQ_EMPTY(irs->same_inode_list, replications)) {
		/* all done */
		free(irs);
		*rstatep = NULL;
	}
		
	netsendq_entry_destroy(&fr->qentry);
	free(fr);
}

int
file_replication_is_busy(struct host *dst)
{
	return (netsendq_window_is_full(
	    abstract_host_get_sendq(host_to_abstract_host(dst)),
	    &gfs_proto_replication_request_queue));
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: XXX
 */
void
file_replication_start(struct inode_replication_state *rstate,
	gfarm_uint64_t gen)
{
	struct file_replication *fr;

	GFARM_HCIRCLEQ_FOREACH(fr, rstate->same_inode_list, replications) {
		if (fr->igen <= gen && !fr->queued) {
			fr->queued = 1;

			/*
			 * we don't have to check the result, because of
			 * NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING
			 */
			(void)netsendq_add_entry(
			    abstract_host_get_sendq(fr->qentry.abhost),
			    &fr->qentry,
			    NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING);
		}
	}
}

void
file_replication_close_check(struct inode_replication_state **irsp)
{
	struct inode_replication_state *rstate = *irsp;
	struct file_replication *fr, *tmp;

	GFARM_HCIRCLEQ_FOREACH_SAFE(fr, rstate->same_inode_list, replications,
	    tmp) {
		/* XXXQ is there any other condition to check? */
		if (!fr->queued && !abstract_host_is_up(fr->qentry.abhost)) {
			(void)inode_replicated(fr,
			    GFARM_ERR_NO_ERROR, GFARM_ERR_CONNECTION_ABORTED,
			    0);
		}
	}
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: XXX
 */
static struct file_replication *
file_replication_lookup(struct host *dst, gfarm_ino_t ino, gfarm_int64_t gen,
	gfarm_int64_t handle)
{
	struct abstract_host *abhost = host_to_abstract_host(dst);
	struct inode *inode;
	struct inode_replication_state *rstate;
	struct file_replication *fr;

	inode = inode_lookup(ino);
	if (inode == NULL)
		return (NULL);
	rstate = inode_get_replication_state(inode);

	if (rstate != NULL) {
		GFARM_HCIRCLEQ_FOREACH(fr,
		    rstate->same_inode_list, replications) {
			if (fr->qentry.abhost == abhost &&
			    fr->igen == gen &&
			    fr->handle == handle) {
				return (fr);
			}
		}
	}
	return (NULL);
}

static void
handle_file_replication_result(struct netsendq_entry *qentryp)
{
	gfarm_error_t e;
	struct file_replication *fr = (struct file_replication *)qentryp;
	struct host *dst;
	struct inode *inode;
	gfarm_error_t dst_error, src_error;
	struct gfarm_thr_statewait *statewait;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST";

	giant_lock();

	dst = abstract_host_to_host(fr->qentry.abhost);
	inode = fr->inode;
	dst_error = fr->qentry.result;
	src_error = fr->src_errcode;
	statewait = fr->statewait;
	if (dst_error == GFARM_ERR_NO_ERROR &&
	    src_error == GFARM_ERR_NO_ERROR) {
		e = inode_replicated(fr, src_error, dst_error, fr->filesize);
	} else {
		e = inode_replicated(fr, src_error, dst_error, -1);
		gflog_debug(GFARM_MSG_1002359,
		    "%s: (%s, %lld:%lld): aborted: (%s, %s) - %s",
		    diag, host_name(dst),
		    (long long)inode_get_number(inode),
		    (long long)file_replication_get_gen(fr),
		    gfarm_error_string(src_error),
		    gfarm_error_string(dst_error),
		    gfarm_error_string(e));
	}

	giant_unlock();

	if (statewait != NULL)
		gfarm_thr_statewait_signal(statewait, e, diag);
}

static void
file_replication_finishedq_enqueue(
	struct file_replication *fr,
	gfarm_error_t src_error, gfarm_error_t dst_error)
{
	fr->src_errcode = src_error;
	netsendq_remove_entry(abstract_host_get_sendq(fr->qentry.abhost),
	    &fr->qentry, dst_error);
}

static void
file_replication_finishedq_enqueue_success(
	struct file_replication *fr,
	gfarm_error_t src_error, gfarm_error_t dst_error,
	gfarm_off_t filesize, struct gfarm_thr_statewait *statewait)
{
	fr->filesize = filesize;
	fr->statewait = statewait;
	file_replication_finishedq_enqueue(fr, src_error, dst_error);
}

gfarm_error_t
gfm_async_server_replication_result(struct host *dst,
	struct peer *peer, gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, e2;
	gfarm_int32_t src_errcode, dst_errcode;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	gfarm_int64_t handle;
	gfarm_off_t filesize;
	struct file_replication *fr;
	struct gfarm_thr_statewait statewait;
	int do_statewait = 0;
	gfarm_int64_t trace_seq_num; /* for gfarm_file_trace */
	static const char diag[] = "GFM_PROTO_REPLICATION_RESULT";

	/*
	 * The reason why this protocol returns not only handle
	 * but also ino and gen is because it's possible that
	 * this request may arrive earlier than the result of
	 * GFS_PROTO_REPLICATION_REQUEST due to race condition.
	 */
	e = gfm_async_server_get_request(peer, size, diag, "llliil",
	    &ino, &gen, &handle, &src_errcode, &dst_errcode, &filesize);

	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * couldn't call file_replication_finishedq_enqueue() here,
		 * because `fr' is unknown.
		 */
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: dst:%s: %s",
		    diag, host_name(dst), gfarm_error_string(e));
		return (e);
	}

	giant_lock(); /* XXXQ FIXME: potential deadlock */

	if ((fr = file_replication_lookup(dst, ino, gen, handle)) != NULL) {
		do_statewait = 1;
		gfarm_thr_statewait_initialize(&statewait, diag);
		file_replication_finishedq_enqueue_success(fr,
		    src_errcode, dst_errcode, filesize, &statewait);
	} else {
		/*
		 * couldn't call file_replication_finishedq_enqueue() here,
		 * because `fr' is unknown.
		 */
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: orphan replication (%s, %lld:%lld): "
		    "handle=%lld s=%d d=%d size:%lld: "
		    "maybe the connection had a problem?",
		    diag, host_name(dst), (long long)ino, (long long)gen,
		    (long long)handle, src_errcode, dst_errcode,
		    (long long)filesize);
	}

	trace_seq_num = trace_log_get_sequence_number(),

	giant_unlock();

	if (gfarm_ctxp->file_trace)
		gflog_trace(GFARM_MSG_1003320,
		    "%lld/////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    gfarm_host_get_self_name(), gfmd_port,
		    host_name(dst), (long long int)ino, (long long int)gen);

	/*
	 * XXX FIXME
	 * There is a slight possibility of deadlock in the following call,
	 * because currently this is called in the context
	 * of threads for back_channel_recv_watcher,
	 */
	if (do_statewait) {
		e = gfarm_thr_statewait_wait(&statewait, diag);
		gfarm_thr_statewait_terminate(&statewait, diag);
	}

	e2 = gfm_async_server_put_reply(dst, peer, xid, diag, e, "");

	return (e2);
}

/*
 * GFS_PROTO_REPLICATION_REQUEST didn't exist before gfarm-2.4.0
 */

static gfarm_int32_t
gfs_client_replication_request_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct file_replication *fr = arg;
	gfarm_int64_t handle;
	struct host *dst = file_replication_get_dst(fr);
	gfarm_error_t e, errcode;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST result";

	/* XXX FIXME, src_err and dst_err should be passed separately */
	e = gfs_client_recv_result_and_error(peer, dst, size, &errcode,
	    diag, "l", &handle);
	if (e == GFARM_ERR_NO_ERROR) {
		fr->handle = handle;
		/* this will be handled by GFM_PROTO_REPLICATION_RESULT */
	} else {
		file_replication_finishedq_enqueue(fr, GFARM_ERR_NO_ERROR, e);
	}
	return (e);
}

static void
gfs_client_replication_request_free(void *p, void *arg)
{
#if 0
	struct peer *peer = p;
#endif
	struct file_replication *fr = arg;
	struct abstract_host *dst = fr->qentry.abhost;
	gfarm_ino_t ino = inode_get_number(fr->inode);
	gfarm_int64_t gen = fr->igen;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST free";

	file_replication_finishedq_enqueue(fr,
	    GFARM_ERR_NO_ERROR, GFARM_ERR_CONNECTION_ABORTED);
	gflog_debug(GFARM_MSG_UNFIXED,
	    "%s: (%s, %lld:%lld): connection aborted",
	    diag, abstract_host_get_name(dst), (long long)ino, (long long)gen);
}

static void *
gfs_client_replication_request_request(void *closure)
{
	struct file_replication *fr = closure;
	struct host *dst = abstract_host_to_host(fr->qentry.abhost);
	gfarm_ino_t ino = inode_get_number(fr->inode);
	gfarm_int64_t gen = fr->igen;
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_REPLICATION_REQUEST request";

	e = gfs_client_send_request(dst, NULL, diag,
	    gfs_client_replication_request_result,
	    gfs_client_replication_request_free,
	    fr,
	    GFS_PROTO_REPLICATION_REQUEST, "sill",
	    host_name(fr->src), host_port(fr->src), ino, gen);
	netsendq_entry_was_sent(abstract_host_get_sendq(fr->qentry.abhost),
	    &fr->qentry);

	if (e != GFARM_ERR_NO_ERROR) {
		/* accessing `fr' is only allowed if e != GFARM_ERR_NO_ERROR */
		file_replication_finishedq_enqueue(fr, GFARM_ERR_NO_ERROR, e);
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s->(%s, %lld:%lld): aborted: %s", diag,
		    host_name(fr->src), host_name(dst),
		    (long long)inode_get_number(fr->inode),
		    (long long)fr->igen,
		    gfarm_error_string(e));
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

struct netsendq_type gfs_proto_replication_request_queue = {
	gfs_client_replication_request_request,
	handle_file_replication_result,
	0 /* will be initialized by gfs_proto_replication_request_window */,
	0,
	NETSENDQ_TYPE_GFS_PROTO_REPLICATION_REQUEST
};


void
file_replication_init(void)
{
	gfs_proto_replication_request_queue.window_size =
	    gfs_proto_replication_request_window;
}

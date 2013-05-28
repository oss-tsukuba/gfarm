/*
 * $Id$
 */

#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <pwd.h>

#include <gfarm/gflog.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "nanosec.h"
#include "thrsubr.h"

#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "gfm_client.h"
#include "config.h"

#include "peer_watcher.h"
#include "peer.h"
#include "local_peer.h"
#include "remote_peer.h"
#include "subr.h"
#include "rpcsubr.h"
#include "thrpool.h"
#include "callout.h"
#include "abstract_host.h"
#include "host.h"
#include "mdhost.h"
#include "inode.h"
#include "journal_file.h"
#include "db_journal.h"
#include "relay.h"
#include "gfmd_channel.h"
#include "back_channel.h"
#include "gfmd.h" /* protocol_service(), gfmd_terminate() */
#include "thrstatewait.h"

struct gfmdc_journal_sync_info {
	gfarm_uint64_t seqnum;
	int nrecv_threads;
	gfarm_error_t file_sync_error;
	pthread_mutex_t sync_mutex;
	pthread_cond_t sync_end_cond;
	pthread_mutex_t async_mutex;
	pthread_cond_t async_wait_cond;
};

static struct peer_watcher *gfmdc_recv_watcher;
static struct thread_pool *gfmdc_send_thread_pool;
static struct thread_pool *journal_sync_thread_pool;
static struct gfmdc_journal_sync_info journal_sync_info;

static const char SYNC_MUTEX_DIAG[]	= "jorunal_sync_info.sync_mutex";
static const char SYNC_END_COND_DIAG[]	= "jorunal_sync_info.sync_end_cond";
static const char SEND_MUTEX_DIAG[]	= "send_closure.mutex";
static const char SEND_END_COND_DIAG[]	= "send_closure.end_cond";
static const char ASYNC_MUTEX_DIAG[]	= "journal_sync_info.async_mutex";
static const char ASYNC_WAIT_COND_DIAG[] = "journal_sync_info.async_wait_cond";
static const char REQUEST_WAIT_INFO_MUTEX[] = "request_sync_info.mutex";
static const char REQUEST_WAIT_INFO_COND[] = "request_sync_info.cond";
static const char PEER_RECORD_MUTEX_DIAG[] = "gfmdc_peer_record.mutex";

#define GFMDC_CONNECT_INTERVAL	30
#define GFMDC_REMOTE_PEER_ALLOC_MAX_RETRY_COUNT	2

/*
 * gmfdc_journal_send_closure
 */

struct gfmdc_journal_send_closure {
	struct mdhost *host;
	void *data;

	/* for synchrnous slave only */
	int end;
	pthread_mutex_t send_mutex;
	pthread_cond_t send_end_cond;
};

static gfarm_error_t
 gfmdc_journal_syncsend_alloc(const char *diag,
	struct gfmdc_journal_send_closure **cp)
{
	int err;
	struct gfmdc_journal_send_closure *c;

	GFARM_MALLOC(c);
	if (c == NULL) {
		gflog_error(GFARM_MSG_1003652,
		    "%s: no memory for journal send closure", diag);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((err = pthread_mutex_init(&c->send_mutex, NULL)) != 0) {
		gflog_error(GFARM_MSG_1003653,
		    "%s: pthread_mutex_init(%s): %s",
		    diag, SEND_MUTEX_DIAG, strerror(err));
		free(c);
		return (gfarm_errno_to_error(err));
	}
	if ((err = pthread_cond_init(&c->send_end_cond, NULL)) != 0) {
		gflog_error(GFARM_MSG_1003654,
		    "%s: pthread_cond_init(%s): %s",
		    diag, SEND_END_COND_DIAG, strerror(err));
		gfarm_mutex_destroy(&c->send_mutex, diag, SEND_MUTEX_DIAG);
		free(c);
		return (gfarm_errno_to_error(err));
	}
	c->host = NULL;
	c->data = NULL;
	c->end = 0;

	*cp = c;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfmdc_journal_syncsend_free(struct gfmdc_journal_send_closure *c)
{
	static const char diag[] = "gfmdc_journal_syncsend_free";

	assert(c->data == NULL);
	gfarm_mutex_destroy(&c->send_mutex, diag, SEND_MUTEX_DIAG);
	gfarm_cond_destroy(&c->send_end_cond, diag, SEND_END_COND_DIAG);
	free(c);
}

static void
gfmdc_journal_send_closure_reset(struct gfmdc_journal_send_closure *c,
	struct mdhost *mh)
{
	c->host = mh;
	c->data = NULL;
}

static void
gfmdc_journal_syncsend_completed(struct gfmdc_journal_send_closure *c)
{
	static const char diag[] = "gfmdc_journal_syncsend_completed";

	free(c->data);
	c->data = NULL;
	gfarm_mutex_lock(&c->send_mutex, diag, SEND_MUTEX_DIAG);
	c->end = 1;
	gfarm_mutex_unlock(&c->send_mutex, diag, SEND_MUTEX_DIAG);
	gfarm_cond_signal(&c->send_end_cond, diag, SEND_END_COND_DIAG);
}

static struct gfmdc_journal_send_closure *
gfmdc_journal_asyncsend_alloc(struct mdhost *mh)
{
	struct gfmdc_journal_send_closure *c;

	GFARM_MALLOC(c);
	if (c != NULL)
		gfmdc_journal_send_closure_reset(c, mh);
	return (c);
}

static void
gfmdc_journal_asyncsend_free(struct gfmdc_journal_send_closure *c)
{
	free(c->data);
	free(c);
}

/*
 * gfmdc_peer_record
 */

/* per peer record for gfmd channel */
struct gfmdc_peer_record {
	pthread_mutex_t mutex;
	struct journal_file_reader *jreader;
	gfarm_uint64_t last_fetch_seqnum;
	int is_received_seqnum, is_in_first_sync;

	/* only used by synchronous slave */
	struct gfmdc_journal_send_closure *journal_send_closure;
};

static gfarm_error_t
gfmdc_peer_record_alloc(const char *diag,
	struct gfmdc_peer_record **gfmdc_peerp)
{
	struct gfmdc_peer_record *gfmdc_peer;
	int err;
	gfarm_error_t e;

	GFARM_MALLOC(gfmdc_peer);
	if (gfmdc_peer == NULL) {
		gflog_error(GFARM_MSG_1003655,
		    "%s: no memory for per peer record", diag);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((err = pthread_mutex_init(&gfmdc_peer->mutex, NULL)) != 0) {
		gflog_error(GFARM_MSG_1003656,
		    "%s: pthread_mutex_init(%s): %s",
		    diag, PEER_RECORD_MUTEX_DIAG, strerror(err));
		free(gfmdc_peer);
		return (gfarm_errno_to_error(err));
	}

	/*
	 * although gfmdc_peer->journal_send_closure is only used
	 * by a synchronous slave, we always initialize this,
	 * because an asynchronous slave may be changed to a synchronous slave.
	 */
	if ((e = gfmdc_journal_syncsend_alloc(diag,
	    &gfmdc_peer->journal_send_closure)) != GFARM_ERR_NO_ERROR) {
		gfarm_mutex_destroy(&gfmdc_peer->mutex,
		    diag, PEER_RECORD_MUTEX_DIAG);
		free(gfmdc_peer);
		return (e);
	}

	gfmdc_peer->jreader = NULL;
	gfmdc_peer->last_fetch_seqnum = 0;
	gfmdc_peer->is_received_seqnum = 0;
	gfmdc_peer->is_in_first_sync = 0;

	*gfmdc_peerp = gfmdc_peer;
	return (GFARM_ERR_NO_ERROR);
}

/* called from peer_free() */
void
gfmdc_peer_record_free(struct gfmdc_peer_record *gfmdc_peer, const char *diag)
{
	if (gfmdc_peer->jreader != NULL)
		journal_file_reader_close(gfmdc_peer->jreader);
	gfmdc_journal_syncsend_free(gfmdc_peer->journal_send_closure);
	gfarm_mutex_destroy(&gfmdc_peer->mutex, diag, PEER_RECORD_MUTEX_DIAG);
	free(gfmdc_peer);
}

static void
gfmdc_peer_mutex_lock(struct gfmdc_peer_record *gfmdc_peer, const char *diag)
{
	gfarm_mutex_lock(&gfmdc_peer->mutex, diag, PEER_RECORD_MUTEX_DIAG);
}

static void
gfmdc_peer_mutex_unlock(struct gfmdc_peer_record *gfmdc_peer, const char *diag)
{
	gfarm_mutex_unlock(&gfmdc_peer->mutex, diag, PEER_RECORD_MUTEX_DIAG);
}

struct journal_file_reader *
gfmdc_peer_get_journal_file_reader(struct gfmdc_peer_record *gfmdc_peer)
{
	struct journal_file_reader *reader;
	static const char diag[] = "gfmdc_peer_get_journal_file_reader";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	reader = gfmdc_peer->jreader;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
	return (reader);
}

void
gfmdc_peer_set_journal_file_reader(struct gfmdc_peer_record *gfmdc_peer,
	struct journal_file_reader *reader)
{
	static const char diag[] = "gfmdc_peer_set_journal_file_reader";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	gfmdc_peer->jreader = reader;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
}

int
gfmdc_peer_journal_file_reader_is_expired(struct gfmdc_peer_record *gfmdc_peer)
{
	return (gfmdc_peer->jreader != NULL ?
	    journal_file_reader_is_expired(gfmdc_peer->jreader) : 0);
}

gfarm_uint64_t
gfmdc_peer_get_last_fetch_seqnum(struct gfmdc_peer_record *gfmdc_peer)
{
	gfarm_uint64_t r;
	static const char diag[] = "gfmdc_peer_get_last_fetch_seqnum";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	r = gfmdc_peer->last_fetch_seqnum;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
	return (r);
}

void
gfmdc_peer_set_last_fetch_seqnum(struct gfmdc_peer_record *gfmdc_peer,
	gfarm_uint64_t seqnum)
{
	static const char diag[] = "gfmdc_peer_set_last_fetch_seqnum";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	gfmdc_peer->last_fetch_seqnum = seqnum;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
}

int
gfmdc_peer_is_received_seqnum(struct gfmdc_peer_record *gfmdc_peer)
{
	int r;
	static const char diag[] = "gfmdc_peer_is_received_seqnum";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	r = gfmdc_peer->is_received_seqnum;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
	return (r);
}

void
gfmdc_peer_set_is_received_seqnum(struct gfmdc_peer_record *gfmdc_peer,
	int flag)
{
	static const char diag[] = "gfmdc_peer_set_is_received_seqnum";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	gfmdc_peer->is_received_seqnum = flag;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
}

int
gfmdc_peer_is_in_first_sync(struct gfmdc_peer_record *gfmdc_peer)
{
	int r;
	static const char diag[] = "gfmdc_peer_is_in_first_sync";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	r = gfmdc_peer->is_in_first_sync;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
	return (r);
}

void
gfmdc_peer_set_is_in_first_sync(struct gfmdc_peer_record *gfmdc_peer, int flag)
{
	static const char diag[] = "gfmdc_peer_set_is_in_first_sync";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	gfmdc_peer->is_in_first_sync = flag;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
}

struct gfmdc_journal_send_closure *
gfmdc_peer_get_journal_send_closure(struct gfmdc_peer_record *gfmdc_peer)
{
	/* mutex_lock is not necessary, because this is constant */
	return (gfmdc_peer->journal_send_closure);
}

/*
 * gfmd channel
 */

static gfarm_error_t
gfmdc_server_get_request(struct peer *peer, size_t size,
	const char *diag, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = async_server_vget_request(peer, size, diag, format, &ap);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfmdc_server_put_reply(struct mdhost *mh,
	struct peer *peer, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = async_server_vput_reply(
	    mdhost_to_abstract_host(mh), peer, xid,
	    diag, errcode, format, &ap);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfmdc_client_recv_result(struct peer *peer, struct mdhost *mh,
	size_t size, const char *diag, const char *format, ...)
{
	gfarm_error_t e, errcode;
	va_list ap;

	va_start(ap, format);
	e = async_client_vrecv_result(peer,
	    mdhost_to_abstract_host(mh), size, diag,
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

struct request_sync_info {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *);
	void *closure;
	int end;
	gfarm_error_t error;
	const char *diag;
};

static void
gfmdc_client_send_request_sync_notify(struct request_sync_info *ri,
	gfarm_error_t e)
{
	ri->error = e;
	gfarm_mutex_lock(&ri->mutex, ri->diag, REQUEST_WAIT_INFO_MUTEX);
	ri->end = 1;
	gfarm_cond_signal(&ri->cond, ri->diag, REQUEST_WAIT_INFO_COND);
	gfarm_mutex_unlock(&ri->mutex, ri->diag, REQUEST_WAIT_INFO_MUTEX);
}

static gfarm_error_t
gfmdc_client_vsend_request_sync_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *mh = peer_get_mdhost(peer);
	struct request_sync_info *ri = arg;

	if ((e = ri->result_op(mh, peer, size, GFARM_ERR_NO_ERROR,
	    ri->closure, ri->diag)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	gfmdc_client_send_request_sync_notify(ri, e);
	return (e);
}

static void
gfmdc_client_vsend_request_sync_disconnect(void *p, void *arg)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *mh = peer_get_mdhost(peer);
	struct request_sync_info *ri = arg;

	/* FIXME better to set the reason of disconnection */
	e = GFARM_ERR_CONNECTION_ABORTED;
	gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
	    mh != NULL ? mdhost_get_name(mh) : "", gfarm_error_string(e));
	(void)ri->result_op(mh, peer, 0, e, ri->closure, ri->diag);
	gfmdc_client_send_request_sync_notify(ri, e);
}

static gfarm_error_t
gfmdc_client_vsend_request_sync(struct mdhost *mh, struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, va_list *app)
{
	gfarm_error_t e;
	struct request_sync_info ri;

	gfarm_mutex_init(&ri.mutex, diag, REQUEST_WAIT_INFO_MUTEX);
	gfarm_cond_init(&ri.cond, diag, REQUEST_WAIT_INFO_COND);
	ri.result_op = result_op;
	ri.closure = closure;
	ri.end = 0;
	ri.error = GFARM_ERR_NO_ERROR;
	ri.diag = diag;

	e = async_client_vsend_request(mdhost_to_abstract_host(mh),
	    peer, diag,
	    gfmdc_client_vsend_request_sync_result,
	    gfmdc_client_vsend_request_sync_disconnect, &ri,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    command, format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
		goto end;
	}

	gfarm_mutex_lock(&ri.mutex, diag, REQUEST_WAIT_INFO_MUTEX);
	while (!ri.end) {
		gfarm_cond_wait(&ri.cond, &ri.mutex, diag,
		    REQUEST_WAIT_INFO_COND);
	}
	gfarm_mutex_unlock(&ri.mutex, diag, REQUEST_WAIT_INFO_MUTEX);
	e = ri.error;
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
end:
	gfarm_mutex_destroy(&ri.mutex, diag, REQUEST_WAIT_INFO_MUTEX);
	gfarm_cond_destroy(&ri.cond, diag, REQUEST_WAIT_INFO_COND);
	return (e);
}

static gfarm_error_t
gfmdc_slave_vsend_request_sync(struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, va_list *app)
{
	return (gfmdc_client_vsend_request_sync(mdhost_lookup_master(),
	    peer, result_op, closure, diag, command, format, app));
}

static gfarm_error_t
gfmdc_slave_send_request_sync(struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfmdc_slave_vsend_request_sync(peer, result_op, closure, diag,
	    command, format, &ap);
	va_end(ap);
	return (e);
}

struct request_async_info {
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *);
	void *closure;
	const char *diag;
};

static gfarm_error_t
gfmdc_client_vsend_request_async_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *mh = peer_get_mdhost(peer);
	struct request_async_info *ri = arg;

	e = ri->result_op(mh, peer, size, GFARM_ERR_NO_ERROR, ri->closure,
	    ri->diag);
	free(ri);
	return (e);
}

static void
gfmdc_client_vsend_request_async_disconnect(void *p, void *arg)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *mh = peer_get_mdhost(peer);
	struct request_async_info *ri = arg;

	/* FIXME better to set the reason of disconnection */
	e = GFARM_ERR_CONNECTION_ABORTED;
	gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
	    mh != NULL ? mdhost_get_name(mh) : "", gfarm_error_string(e));
	(void)ri->result_op(mh, peer, 0, e, ri->closure, ri->diag);
	free(ri);
}

static gfarm_error_t
gfmdc_client_vsend_request_async(struct mdhost *mh, struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, va_list *app)
{
	gfarm_error_t e;
	struct request_async_info *ri;

	GFARM_MALLOC(ri);
	if (ri == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	ri->result_op = result_op;
	ri->closure = closure;
	ri->diag = diag;

	e = async_client_vsend_request(mdhost_to_abstract_host(mh),
	    peer, diag,
	    gfmdc_client_vsend_request_async_result,
	    gfmdc_client_vsend_request_async_disconnect, ri,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    command, format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfmdc_client_send_request_async(struct mdhost *mh, struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfmdc_client_vsend_request_async(mh, peer, result_op, closure,
	    diag, command, format, &ap);
	va_end(ap);
	return (e);
}

static gfarm_error_t
gfmdc_slave_vsend_request_async(
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, va_list *app)
{
	return (gfmdc_client_vsend_request_async(mdhost_lookup_master(),
	    NULL, result_op, closure, diag, command, format, app));
}

static gfarm_error_t
gfmdc_slave_send_request_async(
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	void *closure, const char *diag, gfarm_int32_t command,
	const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfmdc_slave_vsend_request_async(result_op, closure, diag,
	    command, format, &ap);
	va_end(ap);
	return (e);
}

static void
gfmdc_journal_recv_end_signal(const char *diag)
{
	gfarm_mutex_lock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	--journal_sync_info.nrecv_threads;
	gfarm_mutex_unlock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	gfarm_cond_signal(&journal_sync_info.sync_end_cond, diag,
	    SYNC_END_COND_DIAG);
}

static gfarm_error_t
gfmdc_client_journal_send_result_common(struct peer *peer, size_t size,
	struct gfmdc_journal_send_closure *c, const char *diag)
{
	gfarm_error_t e;

	if ((e = gfmdc_client_recv_result(peer, c->host, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002976,
		    "%s : %s", mdhost_get_name(c->host),
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_syncsend_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	struct gfmdc_journal_send_closure *c = closure;

	if (e == GFARM_ERR_NO_ERROR)
		e = gfmdc_client_journal_send_result_common(peer, size, c,
		    diag);
	gfmdc_journal_syncsend_completed(c);
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_asyncsend_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	struct gfmdc_journal_send_closure *c = closure;

	if (e == GFARM_ERR_NO_ERROR)
		e = gfmdc_client_journal_send_result_common(peer, size, c,
		    diag);
	gfmdc_journal_asyncsend_free(c);
	return (e);
}

static int
gfmdc_wait_journal_syncsend(struct gfmdc_journal_send_closure *c)
{
	int r, in_time = 1;
	struct timespec ts;
	static const char diag[] = "gfmdc_wait_journal_syncsend";

	gfarm_gettime(&ts);
	ts.tv_sec += gfarm_get_journal_sync_slave_timeout();

	gfarm_mutex_lock(&c->send_mutex, diag, SEND_MUTEX_DIAG);
	while (!c->end && in_time)
		in_time = gfarm_cond_timedwait(&c->send_end_cond,
		    &c->send_mutex, &ts, diag, SEND_END_COND_DIAG);
	if ((r = c->end) == 0)
		c->end = 1;
	gfarm_mutex_unlock(&c->send_mutex, diag, SEND_MUTEX_DIAG);
	return (r);
}

static gfarm_error_t
gfmdc_client_journal_send(struct peer *peer,
	gfarm_error_t (*result_op)(struct mdhost *, struct peer *, size_t,
		gfarm_error_t, void *, const char *),
	struct gfmdc_journal_send_closure *c, gfarm_uint64_t *to_snp)
{
	gfarm_error_t e;
	int data_len, no_rec;
	char *data;
	gfarm_uint64_t min_seqnum, from_sn, to_sn, lf_sn;
	struct journal_file_reader *reader;
	struct mdhost *mh = c->host;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	static const char diag[] = "GFM_PROTO_JOURNAL_SEND";

	lf_sn = gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer);
	min_seqnum = lf_sn == 0 ? 0 : lf_sn + 1;
	reader = gfmdc_peer_get_journal_file_reader(gfmdc_peer);
	assert(reader);
	e = db_journal_fetch(reader, min_seqnum, &data, &data_len, &from_sn,
	    &to_sn, &no_rec, mdhost_get_name(mh));
	if (e != GFARM_ERR_NO_ERROR) {
		mdhost_set_seqnum_state_by_error(mh, e);
		gflog_notice(GFARM_MSG_1002977,
		    "reading journal to send to %s: %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
		return (e);
	} else if (no_rec) {
		mdhost_set_seqnum_ok(mh);
		*to_snp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	mdhost_set_seqnum_ok(mh);
	gfmdc_peer_set_last_fetch_seqnum(gfmdc_peer, to_sn);
	c->data = data;

	if ((e = gfmdc_client_send_request_async(mh, peer, result_op,
	    c, diag, GFM_PROTO_JOURNAL_SEND, "llb", from_sn, to_sn,
	    (size_t)data_len, data)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002978,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
		free(data);
		c->data = NULL;
		return (e);
	}
	*to_snp = to_sn;

	return (e);
}

static gfarm_error_t
gfmdc_client_journal_syncsend(struct peer *peer,
	struct gfmdc_journal_send_closure *c, gfarm_uint64_t *to_snp)
{
	gfarm_error_t e;

	c->end = 0;
	e = gfmdc_client_journal_send(peer,
	    gfmdc_client_journal_syncsend_result, c, to_snp);
	if (e == GFARM_ERR_NO_ERROR && *to_snp > 0 &&
	    !gfmdc_wait_journal_syncsend(c))
		return (GFARM_ERR_OPERATION_TIMED_OUT);
	assert(c->data == NULL);
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_asyncsend(struct peer *peer,
	struct gfmdc_journal_send_closure *c, gfarm_uint64_t *to_snp)
{
	return (gfmdc_client_journal_send(peer,
	    gfmdc_client_journal_asyncsend_result, c, to_snp));
}

static gfarm_error_t
gfmdc_server_journal_send(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, er;
	gfarm_uint64_t from_sn, to_sn;
	unsigned char *recs = NULL;
	size_t recs_len;
	static const char diag[] = "GFM_PROTO_JOURNAL_SEND";

	if ((er = gfmdc_server_get_request(peer, size, diag, "llB",
	    &from_sn, &to_sn, &recs_len, &recs))
	    == GFARM_ERR_NO_ERROR)
		er = db_journal_recvq_enter(from_sn, to_sn, recs_len, recs);
#ifdef DEBUG_JOURNAL
	if (er == GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002979,
		    "from %s : recv journal %llu to %llu",
		    mdhost_get_name(mh), (unsigned long long)from_sn,
		    (unsigned long long)to_sn);
#endif
	e = gfmdc_server_put_reply(mh, peer, xid, diag, er, "");
	return (e);
}

static void* gfmdc_journal_first_sync_thread(void *);

static gfarm_error_t
gfmdc_server_journal_ready_to_recv(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	int inited = 0;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	struct journal_file_reader *reader;
	static const char diag[] = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	if ((e = gfmdc_server_get_request(peer, size, diag, "l", &seqnum))
	    == GFARM_ERR_NO_ERROR) {
		gfmdc_peer_set_last_fetch_seqnum(gfmdc_peer, seqnum);
		gfmdc_peer_set_is_received_seqnum(gfmdc_peer, 1);
#ifdef DEBUG_JOURNAL
		gflog_debug(GFARM_MSG_1002980,
		    "%s : last_fetch_seqnum=%llu",
		    mdhost_get_name(mh), (unsigned long long)seqnum);
#endif
		reader = gfmdc_peer_get_journal_file_reader(gfmdc_peer);
		if ((e = db_journal_reader_reopen_if_needed(&reader,
		    gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer), &inited))
		    == GFARM_ERR_NO_ERROR) {
			mdhost_set_seqnum_ok(mh);
		} else {
			mdhost_set_seqnum_state_by_error(mh, e);
			gflog_error(GFARM_MSG_1002981,
			    "gfmd_channel(%s) : %s",
			    mdhost_get_name(mh),
			    gfarm_error_string(e));
		}
		/*
		 * if reader is already expired,
		 * db_journal_reader_reopen_if_needed() returns EXPIRED and
		 * sets inited to 1.
		 * if no error occurrs, it also sets inited to 1.
		 */
		if (inited)
			gfmdc_peer_set_journal_file_reader(gfmdc_peer, reader);
	}
	e = gfmdc_server_put_reply(mh, peer, xid, diag, e, "l",
	    db_journal_get_current_seqnum());
	if (mdhost_is_sync_replication(mh)) {
		peer_add_ref(peer); /* increment refcount */
		thrpool_add_job(journal_sync_thread_pool,
		    gfmdc_journal_first_sync_thread, peer);
	}
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_ready_to_recv_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	gfarm_uint64_t seqnum;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if ((e = gfmdc_client_recv_result(peer, mh, size, diag, "l", &seqnum))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002982, "%s : %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	else
		e = slave_add_initial_db_update_info(seqnum, diag);
	return (e);
}

gfarm_error_t
gfmdc_client_journal_ready_to_recv(struct mdhost *mh, struct peer *peer)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	static const char diag[] = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	giant_lock();
	seqnum = db_journal_get_current_seqnum();
	giant_unlock();

	if ((e = gfmdc_slave_send_request_sync(peer,
	    gfmdc_client_journal_ready_to_recv_result, NULL, diag,
	    GFM_PROTO_JOURNAL_READY_TO_RECV, "l", seqnum))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002983,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfmdc_client_remote_peer_alloc_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfmdc_client_recv_result(peer, mh, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfmdc_client_remote_peer_alloc(struct peer *peer)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_ALLOC";
	int port;
	/* FIXME get from peer */
	gfarm_int32_t proto_family = GFARM_PROTO_FAMILY_IPV4;
	gfarm_int32_t proto_transport = GFARM_PROTO_TRANSPORT_IP_TCP;

	if ((e = peer_get_port(peer, &port)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}

	if ((e = gfmdc_slave_send_request_sync(NULL,
	    gfmdc_client_remote_peer_alloc_result, NULL, diag,
	    GFM_PROTO_REMOTE_PEER_ALLOC, "lissiii",
	    peer_get_private_peer_id(peer), peer_get_auth_id_type(peer),
	    peer_get_username(peer), peer_get_hostname(peer),
	    proto_family, proto_transport, port)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfmdc_server_remote_peer_alloc(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_int64_t remote_peer_id;
	gfarm_int32_t auth_id_type, proto_family, proto_transport, port;
	char *user, *host;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_ALLOC";

	if ((e = gfmdc_server_get_request(peer, size, diag, "lissiii",
	    &remote_peer_id, &auth_id_type, &user, &host,
	    &proto_family, &proto_transport, &port)) ==
	    GFARM_ERR_NO_ERROR) {
		/* FIXME We don't pass auth_method now */
		e = remote_peer_alloc(peer, remote_peer_id,
		    auth_id_type, user, host, GFARM_AUTH_METHOD_NONE,
		    proto_family, proto_transport, port);
	}
	e = gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
	return (e);
}

/* 
 * Perform a GFM_PROTO_REMOTE_PEER_ALLOC request for 'peer' if needed.
 *
 * This function requests GFM_PROTO_REMOTE_PEER_ALLOC again if the slave
 * gfmd has reconnected to a master gfmd.
 *
 * Upon successful, this function locks a peer to the current master
 * gfmd and returns its peer as '*mhpeerp'.  The caller of this function
 * must call gfmdc_ensure_remote_peer_end() to unlock the peer.
 */
gfarm_error_t
gfmdc_ensure_remote_peer(struct peer *peer, struct peer **mhpeerp)
{
	gfarm_error_t e;
	struct local_peer *lpeer = peer_to_local_peer(peer);
	struct abstract_host *mah;
	struct peer *my_mhpeer = NULL;
	gfarm_int64_t master_id;
	int retry_count = 0;
	const char diag[] = "gfmdc_ensure_remote_peer";

	for (;;) {
		mah = mdhost_to_abstract_host(mdhost_lookup_master());
		e = abstract_host_sender_lock(mah, NULL, &my_mhpeer, diag);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s (abstract_host_sender_lock): %s",
			    diag, gfarm_error_string(e));
			return (e);
		}
		master_id = peer_get_private_peer_id(my_mhpeer);
		if (local_peer_get_remote_peer_allocated(lpeer))
			break;
		abstract_host_sender_unlock(mah, my_mhpeer, diag);

		if (retry_count >= GFMDC_REMOTE_PEER_ALLOC_MAX_RETRY_COUNT) {
			gflog_error(GFARM_MSG_UNFIXED, "%s: retry overtime",
			    diag);
			return (e);
		}

		e = gfmdc_client_remote_peer_alloc(peer);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s (gfmdc_client_remote_peer_alloc): %s",
			    diag, gfarm_error_string(e));
			return (e);
		}
		local_peer_set_remote_peer_allocated(lpeer, master_id);
		retry_count++;
	}

	*mhpeerp = my_mhpeer;
	return (GFARM_ERR_NO_ERROR);
}

void
gfmdc_ensure_remote_peer_end(struct peer *mhpeer)
{
	struct abstract_host *mah;
	const char diag[] = "gfmdc_ensure_remote_peer_end";

	mah = peer_get_abstract_host(mhpeer);
	abstract_host_sender_unlock(mah, mhpeer, diag);
}

static gfarm_error_t
gfmdc_client_remote_peer_free_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfmdc_client_recv_result(peer, mh, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfmdc_client_remote_peer_free(gfarm_uint64_t peer_id)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_FREE";

	if (mdhost_self_is_master() || !mdhost_is_up(mdhost_lookup_master()))
		return (GFARM_ERR_NO_ERROR);

	if ((e = gfmdc_slave_send_request_async(
	    gfmdc_client_remote_peer_free_result, NULL, diag,
	    GFM_PROTO_REMOTE_PEER_FREE, "l", peer_id))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfmdc_server_remote_peer_free(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_int64_t remote_peer_id;
	struct remote_peer *remote_peer;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_FREE";

	do {
		e = gfmdc_server_get_request(peer, size, diag, "l",
		    &remote_peer_id);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		remote_peer = local_peer_lookup_remote(
		    peer_to_local_peer(peer), remote_peer_id);
		if (remote_peer == NULL) {
			e = GFARM_ERR_INVALID_REMOTE_PEER;
			break;
		}

		assert(peer_get_parent(remote_peer_to_peer(remote_peer)) !=
		    NULL);

		/* XXXRELAY this takes giant lock */
		giant_lock();
		remote_peer_set_received_remote_peer_free(remote_peer);
		peer_free(remote_peer_to_peer(remote_peer));
		giant_unlock();
	} while (0);
	e = gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
	return (e);
}

static gfarm_error_t
gfmdc_client_remote_peer_disconnect_result(struct mdhost *mh,
	struct peer *peer, size_t size, gfarm_error_t e, void *closure,
	const char *diag)
{
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = gfmdc_client_recv_result(peer, mh, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfmdc_client_remote_peer_disconnect(struct mdhost *mh, struct peer *peer,
    gfarm_int64_t remote_peer_id)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_DISCONNECT";

	if ((e = gfmdc_client_send_request_async(mh, peer,
	    gfmdc_client_remote_peer_disconnect_result, NULL, diag,
	    GFM_PROTO_REMOTE_PEER_DISCONNECT, "l", remote_peer_id))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfmdc_server_remote_peer_disconnect(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, e2;
	gfarm_int64_t peer_id;
	struct peer *gfs_peer;
	static const char diag[] = "GFM_PROTO_REMOTE_PEER_DISCONNECT";

	do {
		if ((e = gfmdc_server_get_request(peer, size, diag, "l",
		    &peer_id)) != GFARM_ERR_NO_ERROR)
			break;
		gfs_peer = local_peer_to_peer(local_peer_lookup(peer_id));
		if (gfs_peer == NULL)
			break;  /* 'gfsd_peer' has already been disposed. */
		if (peer_get_peer_type(gfs_peer) != peer_type_back_channel) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "received peer-disconnect reqeust with an invalid "
			    "peer id: %lld", (long long)peer_id);
			e = GFARM_ERR_INVALID_REMOTE_PEER;
			break;
		}
		local_peer_set_received_remote_peer_disconnect(
		    peer_to_local_peer(gfs_peer));
		host_disconnect_request(peer_get_host(gfs_peer), gfs_peer);
	} while (0);

	e2 = gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
	if (e == GFARM_ERR_NO_ERROR)
		e = e2;
	return (e);
}

struct server_remote_rpc_closure {
	struct peer *rpeer;
	gfp_xdr_xid_t xid;
	size_t size;
};

static void *
gfmdc_server_remote_rpc_thread(void *arg)
{
	struct server_remote_rpc_closure *cp = arg;

	protocol_service(cp->rpeer, cp->xid, &cp->size); 
	return (NULL);
}

static gfarm_error_t
gfmdc_server_remote_rpc(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_int64_t remote_peer_id;
	struct remote_peer *remote_peer;
	struct peer *rpeer;
	struct gfarm_thr_statewait *statewait;
	struct server_remote_rpc_closure c;
	int eof;
	static const char diag[] = "GFM_PROTO_REMOTE_RPC";

	if (debug_mode) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> start remote rpc receiving from %s",
		    peer_get_hostname(peer), diag,
		    peer_get_service_name(peer));
	}

	if ((e = gfp_xdr_recv_sized(peer_get_conn(peer), 0, 1, &size, &eof,
	    "l", &remote_peer_id)) != GFARM_ERR_NO_ERROR) {
		/* XXXRELAY fix rpc residual */
		e = gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
	} else if (eof) {
		e = gfmdc_server_put_reply(mh, peer, xid, diag,
		    GFARM_ERR_UNEXPECTED_EOF, "");
	} else if ((remote_peer = local_peer_lookup_remote(
	    peer_to_local_peer(peer), remote_peer_id)) == NULL) {
		/* XXXRELAY fix rpc residual */
		e = gfmdc_server_put_reply(mh, peer, xid, diag,
		    GFARM_ERR_INVALID_REMOTE_PEER, "");
	} else {
		rpeer = remote_peer_to_peer(remote_peer);
		remote_peer_clear_db_update_info(remote_peer);
		statewait = remote_peer_get_statewait(remote_peer);
		gfarm_thr_statewait_reset(statewait, diag);

		/*
		 * If the remote RPC updates metadata and the server
		 * is a master gfmd with a sync-slave, the following
		 * indirect fmdc_server_remote_rpc_thread() call also
		 * send journal-update notification to the sync-slave.
		 *
		 * To receive a response of the journal-update request,
		 * it have to call gfmdc_server_remote_rpc_thread()
		 * from another thread in the thread pool and wait for
		 * completion of the fmdc_server_remote_rpc_thread()
		 * call.
		 *
		 * For more information, see the ticket #287 and the
		 * changeset r6632.
		 */
		c.rpeer = rpeer;
		c.xid = xid;
		c.size = size;
		thrpool_add_job(journal_sync_thread_pool,
		    gfmdc_server_remote_rpc_thread, &c);
		e = gfarm_thr_statewait_wait(statewait, diag);

		/*
		 * decrement refcount of 'rpeer' (== 'remote_peer'), since
		 * it has been incremented by local_peer_lookup_remote().
		 */
		peer_del_ref(rpeer);
	}
	return (e);
}

/*
 * Closure for gfmdc_master_server_remote_gfs_rpc().
 */
struct master_server_remote_gfs_rpc_closure {
	struct abstract_host *abhost;
	gfarm_int64_t private_peer_id;
	gfarm_int64_t remote_peer_id;
	gfp_xdr_xid_t xid;
	size_t size;
	struct gfarm_thr_statewait *statewait;
};

/*
 * Create an object of 'struct master_server_remote_gfs_rpc_closure'.
 */
static struct master_server_remote_gfs_rpc_closure *
master_server_remote_gfs_rpc_closure_alloc(struct abstract_host *abhost,
	gfarm_int64_t private_peer_id, gfarm_int64_t remote_peer_id,
	gfp_xdr_xid_t xid, size_t size, struct gfarm_thr_statewait *statewait)
{
	struct master_server_remote_gfs_rpc_closure *closure;

	GFARM_MALLOC(closure);
	if (closure == NULL)
		return (NULL);
	closure->abhost          = abhost;
	closure->private_peer_id = private_peer_id;
	closure->remote_peer_id  = remote_peer_id;
	closure->xid             = xid;
	closure->size            = size;
	closure->statewait       = statewait;

	return (closure);
}

/*
 * Dispose an object of 'struct master_server_remote_gfs_rpc_closure'.
 */
static void
master_server_remote_gfs_rpc_closure_free(
	struct master_server_remote_gfs_rpc_closure *closure)
{
	free(closure);
}

/*
 * Internal function of gfmdc_master_server_remote_gfs_rpc(), invoked via
 * a thread pool.
 */
static void *
gfmdc_master_server_remote_gfs_rpc_thread(void *arg)
{
	struct master_server_remote_gfs_rpc_closure *closure = arg;
	struct peer *peer;
	struct remote_peer *remote_peer;
	gfarm_error_t e;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC master (request from slave)";

	/* increment refcount */
	if ((peer = abstract_host_get_peer_with_id(closure->abhost,
	    closure->private_peer_id, diag)) == NULL)
		e = GFARM_ERR_CONNECTION_ABORTED;
	else if ((remote_peer = local_peer_lookup_remote(
	    peer_to_local_peer(peer), closure->remote_peer_id)) == NULL)
		e = GFARM_ERR_INVALID_REMOTE_PEER;
	else {
		e = async_channel_protocol_switch(
		    peer_get_abstract_host(remote_peer_to_peer(remote_peer)),
		    remote_peer_to_peer(remote_peer), closure->xid,
		    closure->size, async_back_channel_protocol_switch);
	}

	/* decrement refcount */
	if (peer != NULL)
		abstract_host_put_peer(closure->abhost, peer);
	gfarm_thr_statewait_signal(closure->statewait, e, diag);
	return (NULL);
}

/*
 * Protocol handler of GFM_PROTO_REMOTE_GFS_RPC for master gfmd.  It receives
 * a GFM_PROTO_REMOTE_GFS_RPC request from a slave gfmd and sends its reply.
 */
static gfarm_error_t
gfmdc_master_server_remote_gfs_rpc(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	gfarm_int64_t remote_peer_id;
	struct remote_peer *remote_peer = NULL;
	struct gfarm_thr_statewait *statewait;
	struct master_server_remote_gfs_rpc_closure *closure;
	int eof;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC master (request from slave)";

	if (debug_mode) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> start remote rpc receiving from %s",
		    peer_get_hostname(peer), diag,
		    peer_get_service_name(peer));
	}

	do {
		e = gfp_xdr_recv_sized(conn, 0, 1, &size, &eof, "l",
		    &remote_peer_id);
		if (e != GFARM_ERR_NO_ERROR) {
			/* XXXRELAY fix rpc residual */
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		}
		if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
			break;
		}
		remote_peer = local_peer_lookup_remote(
		    peer_to_local_peer(peer), remote_peer_id);
		if (remote_peer == NULL) {
			/* XXXRELAY fix rpc residual */
			e = GFARM_ERR_INVALID_REMOTE_PEER;
			break;
		}
		if (remote_peer_get_peer_type(remote_peer) !=
		    peer_type_back_channel) {
			/* XXXRELAY fix rpc residual */
			e = GFARM_ERR_INVALID_REMOTE_PEER;
			break;
		}
	} while (0);

	statewait = remote_peer_get_statewait(remote_peer);
	gfarm_thr_statewait_reset(statewait, diag);
	closure = master_server_remote_gfs_rpc_closure_alloc(
	    mdhost_to_abstract_host(mh), peer_get_private_peer_id(peer),
	    remote_peer_id, xid, size, statewait);
	if (closure == NULL)
		e = GFARM_ERR_NO_MEMORY;

	if (e != GFARM_ERR_NO_ERROR && !eof) {
		(void) gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
		return (e);
	}

	thrpool_add_job(journal_sync_thread_pool,
	    gfmdc_master_server_remote_gfs_rpc_thread, closure);
	e = gfarm_thr_statewait_wait(statewait, diag);
	master_server_remote_gfs_rpc_closure_free(closure);

	return (e);
}

/*
 * Closure for gfmdc_slave_server_remote_gfs_rpc().
 */
struct slave_server_remote_gfs_rpc_closure {
	struct abstract_host *abhost;
	gfarm_int64_t private_peer_id;
	gfp_xdr_xid_t xid;
	size_t size;
	void *data;
	gfarm_error_t errcode;  /* only used in a reply. */
};

/*
 * Create an object of 'struct slave_server_remote_gfs_rpc_closure'.
 */
static struct slave_server_remote_gfs_rpc_closure *
slave_server_remote_gfs_rpc_closure_alloc(struct abstract_host *abhost,
	gfarm_int64_t private_peer_id, gfp_xdr_xid_t xid, size_t size,
	void *data)
{
	struct slave_server_remote_gfs_rpc_closure *closure;

	GFARM_MALLOC(closure);
	if (closure == NULL)
		return (NULL);
	closure->data = malloc(size);
	if (closure->data == NULL) {
		free(closure);
		return (NULL);
	}

	closure->abhost          = abhost;
	closure->private_peer_id = private_peer_id;
	closure->xid             = xid;
	closure->size            = size;
	if (data != NULL)
		memcpy(closure->data, data, size);

	return (closure);
}

/*
 * Dispose an object of 'struct slave_server_remote_gfs_rpc_closure'.
 */
static void
slave_server_remote_gfs_rpc_closure_free(
	struct slave_server_remote_gfs_rpc_closure *closure)
{
	free(closure->data);
	free(closure);
}

/*
 * Resize data buffer in an object of
 * 'struct slave_server_remote_gfs_rpc_closure'.
 */
static int
slave_server_remote_gfs_rpc_closure_resize(
	struct slave_server_remote_gfs_rpc_closure *closure, size_t size)
{
	void *new_data;

	if (size > closure->size) {
		new_data = realloc(closure->data, size);
		if (new_data == NULL)
			return (0);
		closure->data = new_data;
	}

	closure->size = size;
	return (1);
}

/*
 * Internal function of gfmdc_slave_server_remote_gfs_rpc_result(),
 * invoked via a thread pool.
 */
static void *
gfmdc_slave_server_remote_gfs_rpc_result_thread(void *arg)
{
	struct slave_server_remote_gfs_rpc_closure *closure = arg;
	struct peer *peer;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (reply to master)";

	/* increment refcount */
	peer = abstract_host_get_peer_with_id(closure->abhost,
	    closure->private_peer_id, diag);
	if (peer == NULL)
		return (NULL);

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> sending reply: %d (%s)",
		    peer_get_hostname(peer), diag, (int)closure->errcode,
		    gfarm_error_string(closure->errcode));

	if (closure->errcode == GFARM_ERR_NO_ERROR) {
		(void) async_server_put_reply(closure->abhost, peer,
		    closure->xid, diag, closure->errcode, "r",
		    (int) closure->size, closure->data);
	} else {
		(void) async_server_put_reply(closure->abhost, peer,
		    closure->xid, diag, closure->errcode, "");
	}

	/* decrement refcount */
	abstract_host_put_peer(closure->abhost, peer);
	slave_server_remote_gfs_rpc_closure_free(closure);
	return (NULL);
}

/*
 * 'result_callback' handler for gfmdc_slave_server_remote_gfs_rpc().
 */
static gfarm_error_t
gfmdc_slave_server_remote_gfs_rpc_result(gfarm_error_t errcode, void *arg,
    size_t size, void *data)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct slave_server_remote_gfs_rpc_closure *closure = arg;

	if (!slave_server_remote_gfs_rpc_closure_resize(closure, size)) {
		slave_server_remote_gfs_rpc_closure_free(closure);
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(closure->data, data, size);
	closure->errcode = errcode;

	thrpool_add_job(journal_sync_thread_pool,
	    gfmdc_slave_server_remote_gfs_rpc_result_thread, closure);
	return (e);
}

/*
 * 'disconnect_callback' handler for gfmdc_slave_server_remote_gfs_rpc().
 */
static void
gfmdc_slave_server_remote_gfs_rpc_disconnect(gfarm_error_t errcode, void *arg)
{
	assert(errcode != GFARM_ERR_NO_ERROR);
	(void) gfmdc_slave_server_remote_gfs_rpc_result(errcode, arg, 0, NULL);
}

/*
 * Protocol handler of GFM_PROTO_REMOTE_GFS_RPC for slave gfmd.  It forwards
 * a GFS protocol request from a master gfmd to gfsd and relays its reply
 * in the opposite direction.
 */
static gfarm_error_t
gfmdc_slave_server_remote_gfs_rpc(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	struct local_peer *gfs_lpeer;
	struct peer *gfs_peer;
	gfarm_int64_t peer_id;
	size_t data_size;
	int eof;
	struct slave_server_remote_gfs_rpc_closure *closure = NULL;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (request from master)";

	if (debug_mode) {
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s: <%s> start remote rpc receiving from %s",
		    peer_get_hostname(peer), diag,
		    peer_get_service_name(peer));
	}

	do {
		e = gfp_xdr_recv_sized(conn, 0, 1, &size, &eof, "l", &peer_id);
		if (e != GFARM_ERR_NO_ERROR) {
			/* XXXRELAY fix rpc residual */
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		} else if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
			break;
		}

		/*
		 * We dispose 'closure' at
		 * gfmdc_slave_server_remote_gfs_rpc_result_thread().
		 */
		closure = slave_server_remote_gfs_rpc_closure_alloc(
		    mdhost_to_abstract_host(mh),
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
			/* XXXRELAY fix rpc residual */
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

		gfs_lpeer = local_peer_lookup(peer_id);
		if (gfs_lpeer == NULL) {
			e = GFARM_ERR_INVALID_REMOTE_PEER;
			break;
		}
		gfs_peer = local_peer_to_peer(gfs_lpeer);

		/*
		 * Wait until GFM_PROTO_SWITCH_ASYNC_BACK_CHANNEL procedure
		 * on this slave gfmd has been completed.  Note that master
		 * gfmd may finish the procedure earlier than the slave gfmd
		 * and it requests GFS_PROTO_STATUS to the gfsd server
		 * immediately.
		 */
		if (peer_get_peer_type(gfs_peer) ==
		    peer_type_foreground_channel) {
			e = gfarm_thr_statewait_wait(
			    local_peer_get_statewait(gfs_lpeer), diag);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}

		peer_add_ref(gfs_peer);   /* increment refcount */
		e = gfs_client_relay(peer_get_abstract_host(gfs_peer),
		    gfs_peer, size, closure->data, closure,
		    gfmdc_slave_server_remote_gfs_rpc_result,
		    gfmdc_slave_server_remote_gfs_rpc_disconnect);
		peer_del_ref(gfs_peer);   /* decrement refcount */
	} while (0);

	if (e != GFARM_ERR_NO_ERROR) {
		if (!eof) {
			(void) gfmdc_server_put_reply(mh, peer, xid, diag, e,
			    "");
		}
		if (closure != NULL)
			slave_server_remote_gfs_rpc_closure_free(closure);
	}
	return (e);
}

/*
 * Closure for gfmdc_slave_client_remote_gfs_rpc().
 */
struct slave_client_remote_gfs_rpc_closure {
	struct abstract_host *abhost;
	void *closure;
	gfarm_int32_t
		(*result_callback)(gfarm_error_t, void *, size_t, void *);
	void (*disconnect_callback)(gfarm_error_t, void *);
	gfarm_int64_t private_peer_id;
	int request;
	size_t size;
	void *data;
};

/*
 * Create an object of 'struct slave_client_remote_gfs_rpc_closure'.
 */
static struct slave_client_remote_gfs_rpc_closure *
slave_client_remote_gfs_rpc_closure_alloc(struct abstract_host *abhost,
	void *closure,
	gfarm_error_t (*result_callback)(gfarm_error_t, void *, size_t,
	    void *),
	void (*disconnect_callback)(gfarm_error_t, void *),
	gfarm_int64_t private_peer_id, int request, size_t size, void *data)
{
	struct slave_client_remote_gfs_rpc_closure *wclosure;

	GFARM_MALLOC(wclosure);
	if (wclosure == NULL)
		return (NULL);
	wclosure->data = malloc(size);
	if (wclosure->data == NULL) {
		free(wclosure);
		return (NULL);
	}

	wclosure->abhost              = abhost;
	wclosure->closure             = closure;
	wclosure->result_callback     = result_callback;
	wclosure->disconnect_callback = disconnect_callback;
	wclosure->private_peer_id     = private_peer_id;
	wclosure->request             = request;
	wclosure->size                = size;
	if (data != NULL)
		memcpy(wclosure->data, data, size);

	return (wclosure);
}

/*
 * Dispose an object of 'struct slave_client_remote_gfs_rpc_closure'.
 */
static void
slave_client_remote_gfs_rpc_closure_free(
	struct slave_client_remote_gfs_rpc_closure *wclosure)
{
	free(wclosure->data);
	free(wclosure);
}

/*
 * Resize data buffer in an object of
 * 'struct slave_client_remote_gfs_rpc_closure'.
 */
static int
slave_client_remote_gfs_rpc_closure_resize(
	struct slave_client_remote_gfs_rpc_closure *wclosure, size_t size)
{
	void *new_data;

	if (size > wclosure->size) {
		new_data = realloc(wclosure->data, size);
		if (new_data == NULL)
			return (0);
		wclosure->data = new_data;
	}

	wclosure->size = size;
	return (1);
}

/*
 * 'result_callback' handler for gfmdc_slave_client_remote_gfs_rpc().
 */
static gfarm_error_t
gfmdc_slave_client_remote_gfs_rpc_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct gfp_xdr *conn = peer_get_conn(peer);
	struct slave_client_remote_gfs_rpc_closure *wclosure = arg;
	gfarm_int32_t errcode = GFARM_ERR_NO_ERROR;
	size_t data_size;
	int eof;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (reply from master)";

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED, "%s: <%s> receiving reply",
		    peer_get_hostname(peer), diag);

	do {
		if (!slave_client_remote_gfs_rpc_closure_resize(wclosure,
		    size)) {
			e = GFARM_ERR_NO_MEMORY;
			errcode = e;
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		}

		/*
		 * Get an error code of GFM_PROTO_REMOTE_GFS_RPC.
		 */
		e = gfp_xdr_recv_sized(conn, 0, 1, &size, &eof, "i", &errcode);
		if (e != GFARM_ERR_NO_ERROR) {
			errcode = e;
			(void) gfp_xdr_purge(conn, 0, size);
			break;
		} else if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
			errcode = e;
			break;
		}

		/*
		 * Get an error code of a GFS protocol request wrapped by
		 * GFM_PROTO_REMOTE_GFS_RPC.
		 */
		if (errcode == GFARM_ERR_NO_ERROR) {
			e = gfp_xdr_recv_sized(conn, 0, 1, &size, &eof, "i",
			    &errcode);
			if (e != GFARM_ERR_NO_ERROR) {
				errcode = e;
				(void) gfp_xdr_purge(conn, 0, size);
				break;
			} else if (eof) {
				e = GFARM_ERR_UNEXPECTED_EOF;
				errcode = e;
				break;
			}
		}

		data_size = size;
		e = gfp_xdr_recv(conn, 1, &eof, "r", data_size, &data_size,
		    wclosure->data);
		if (e != GFARM_ERR_NO_ERROR) {
			errcode = e;
			(void) gfp_xdr_purge(conn, 0, data_size);
		} else if (eof) {
			e = GFARM_ERR_UNEXPECTED_EOF;
			errcode = e;
		} else if (data_size != 0) {
			e = GFARM_ERR_PROTOCOL;
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: <%s> protocol redidual %u",
			    peer_get_hostname(peer), diag, (int)data_size);
		}
	} while (0);

	if (errcode == GFARM_ERR_NO_ERROR) {
		e = wclosure->result_callback(errcode, wclosure->closure,
		    size, wclosure->data);
	} else {
		e = wclosure->result_callback(errcode, wclosure->closure,
		    0, "");
	}

	slave_client_remote_gfs_rpc_closure_free(wclosure);
	return (e);
}

/*
 * 'disconnect_callback' handler for gfmdc_slave_client_remote_gfs_rpc().
 */
static void
gfmdc_slave_client_remote_gfs_rpc_disconnect(void *p, void *arg)
{
	struct slave_client_remote_gfs_rpc_closure *wclosure = arg;

	(void) wclosure->result_callback(GFARM_ERR_UNEXPECTED_EOF,
	    wclosure->closure, 0, "");
	slave_client_remote_gfs_rpc_closure_free(wclosure);
}

/*
 * Internal function of gfmdc_slave_client_remote_gfs_rpc_thread().
 * It converts '...' to va_list, in order to call
 * async_client_vsend_wrapped_request() which takes two va_list arguments.
 */
static void *
gfmdc_slave_client_remote_gfs_rpc_thread00(
	struct slave_client_remote_gfs_rpc_closure *wclosure, char *format,
	va_list *app, char *wformat, ...)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	va_list wap;
	struct peer *gfs_peer;
	struct peer *mh_peer = NULL;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (request to master)";

	do {
		/* increment refcount */
		gfs_peer = abstract_host_get_peer_with_id(wclosure->abhost,
		    wclosure->private_peer_id, diag);
		if (gfs_peer == NULL) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			break;
		}
		e = gfmdc_ensure_remote_peer(gfs_peer, &mh_peer);
		/* decrement refcount */
		abstract_host_put_peer(wclosure->abhost, gfs_peer);
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if (debug_mode)
			gflog_info(GFARM_MSG_UNFIXED,
			    "%s: <%s> sending requset",
			    peer_get_hostname(mh_peer), diag);

		va_start(wap, wformat);
		(void) async_client_vsend_wrapped_request_unlocked(
		    peer_get_abstract_host(mh_peer), diag,
		    gfmdc_slave_client_remote_gfs_rpc_result,
		    gfmdc_slave_client_remote_gfs_rpc_disconnect, wclosure,
		    wformat, &wap, wclosure->request, format, app, 0);
		va_end(wap);
	} while (0);

	if (mh_peer != NULL)
		gfmdc_ensure_remote_peer_end(mh_peer);
	if (e != GFARM_ERR_NO_ERROR) {
		(void) wclosure->result_callback(e, wclosure->closure, 0, "");
		slave_client_remote_gfs_rpc_closure_free(wclosure);
	}

	return (NULL);
}

/*
 * Internal function of gfmdc_slave_client_remote_gfs_rpc_thread().
 * It converts '...' to va_list, in order to call
 * async_client_vsend_wrapped_request() which takes two va_list arguments.
 */
static void *
gfmdc_slave_client_remote_gfs_rpc_thread0(
	struct slave_client_remote_gfs_rpc_closure *wclosure,
	char *format, ...)
{
	void *result;
	va_list ap;

	va_start(ap, format);
	result = gfmdc_slave_client_remote_gfs_rpc_thread00(wclosure, format,
	    &ap, "il", GFM_PROTO_REMOTE_GFS_RPC, wclosure->private_peer_id);
	va_end(ap);

	return (result);
}

/*
 * Internal function of gfmdc_slave_client_remote_gfs_rpc(), invoked via
 * a thread pool.
 */
static void *
gfmdc_slave_client_remote_gfs_rpc_thread(void *arg)
{
	struct slave_client_remote_gfs_rpc_closure *wclosure = arg;

	return (gfmdc_slave_client_remote_gfs_rpc_thread0(wclosure,
		"r", wclosure->size, wclosure->data));
}

/*
 * Protocol handler of GFM_PROTO_REMOTE_GFS_RPC for slave gfmd.  It forwards
 * a GFS protocol request from gfsd to a master gfmd and relays its reply
 * in the opposite direction.
 */
gfarm_error_t
gfmdc_slave_client_remote_gfs_rpc(struct peer *gfs_peer, void *closure,
	gfarm_error_t (*result_callback)(gfarm_error_t, void *, size_t,
	    void *),
	void (*disconnect_callback)(gfarm_error_t, void *),
	int request, size_t size, void *data)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct slave_client_remote_gfs_rpc_closure *wclosure;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC slave (request to master)";

	/*
	 * We dispose 'wclosure' in
	 * gfmdc_slave_client_remote_gfs_rpc_result() or
	 * gfmdc_slave_client_remote_gfs_rpc_disconnect().
	 */
	wclosure = slave_client_remote_gfs_rpc_closure_alloc(
	    peer_get_abstract_host(gfs_peer), closure, result_callback,
	    disconnect_callback, peer_get_private_peer_id(gfs_peer),
	    request, size, data);
	if (wclosure == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED, "%s: %s", diag,
		    gfarm_error_string(e));
		return (e);
	}

	thrpool_add_job(journal_sync_thread_pool,
	    gfmdc_slave_client_remote_gfs_rpc_thread, wclosure);

	return (e);
}

/*
 * Wrapped closure for gfmdc_master_client_remote_gfs_rpc().
 */
struct master_client_remote_gfs_rpc_closure {
	gfarm_int32_t (*result_callback)(void *, void *, size_t);
	void (*disconnect_callback)(void *, void *);
	void *closure;
};

static struct master_client_remote_gfs_rpc_closure *
master_client_remote_gfs_rpc_closure_alloc(
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure)
{
	struct master_client_remote_gfs_rpc_closure *wclosure;

	GFARM_MALLOC(wclosure);
	if (wclosure == NULL)
		return (NULL);
	wclosure->result_callback     = result_callback;
	wclosure->disconnect_callback = disconnect_callback;
	wclosure->closure             = closure;
	return (wclosure);
}

static void
master_client_remote_gfs_rpc_closure_free(
	struct master_client_remote_gfs_rpc_closure *wclosure)
{
	free(wclosure);
}

/*
 * 'result_callback' handler for gfmdc_master_client_remote_gfs_rpc0().
 */
static gfarm_int32_t
gfmdc_master_client_remote_gfs_rpc_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	gfarm_error_t errcode;
	struct peer *peer = p;
	struct gfp_xdr *conn = peer_get_conn(peer);
	int eof;
	struct master_client_remote_gfs_rpc_closure *wclosure = arg;
	static const char diag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC master (reply from slave)";

	/*
	 * Get an error code of GFM_PROTO_REMOTE_GFS_RPC.
	 */
	if ((e = gfp_xdr_recv_sized(conn, 0, 1, &size, &eof, "i", &errcode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	} else if (eof) {
		e = GFARM_ERR_UNEXPECTED_EOF;
	}

	if (e == GFARM_ERR_NO_ERROR && errcode == GFARM_ERR_NO_ERROR)
		e = (wclosure->result_callback)(p, wclosure->closure, size);
	else
		(wclosure->disconnect_callback)(peer, wclosure->closure);

	master_client_remote_gfs_rpc_closure_free(wclosure);
	return (e);
}

/*
 * 'disconnect_callback' handler for gfmdc_master_client_remote_gfs_rpc0().
 */
static void
gfmdc_master_client_remote_gfs_rpc_disconnect(void *p, void *arg)
{
	struct master_client_remote_gfs_rpc_closure *wclosure = arg;

	(wclosure->disconnect_callback)(p, wclosure->closure);
	master_client_remote_gfs_rpc_closure_free(wclosure);
}

/*
 * Internal function of gfmdc_master_client_remote_gfs_rpc().
 * It converts '...' to va_list, in order to call
 * async_client_vsend_wrapped_request() which takes two va_list arguments.
 */
static gfarm_error_t
gfmdc_master_client_remote_gfs_rpc0(struct abstract_host *ah,
	struct peer *peer, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *), void *closure,
	gfarm_int32_t command, const char *format, va_list *app,
	const char *wformat, ...)
{
	gfarm_error_t e;
	struct master_client_remote_gfs_rpc_closure *wclosure;
	va_list wap;
	static const char wdiag[] =
	    "GFM_PROTO_REMOTE_GFS_RPC master (request to slave)";

	gflog_debug(GFARM_MSG_UNFIXED, "%s: sending request", wdiag);

	/*
	 * We dispose 'wclosure' in
	 * gfmdc_master_client_remote_gfs_rpc_result() or
	 * gfmdc_master_client_remote_gfs_rpc_disconnect().
	 */
	wclosure = master_client_remote_gfs_rpc_closure_alloc(result_callback,
	    disconnect_callback, closure);
	if (wclosure == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no memory", wdiag);
		return (GFARM_ERR_NO_MEMORY);
	}

	va_start(wap, wformat);
	e = async_client_vsend_wrapped_request(ah, peer, diag,
	    gfmdc_master_client_remote_gfs_rpc_result,
	    gfmdc_master_client_remote_gfs_rpc_disconnect, wclosure,
	    wformat, &wap, command, format, app, 0);
	va_end(wap);

	return (e);
}

/*
 * Protocol handler of GFM_PROTO_REMOTE_GFS_RPC for master gfmd.  It sends
 * a GFS protocol request to gfsd via a slave gfmd and receives its reply.
 */
gfarm_error_t
gfmdc_master_client_remote_gfs_rpc(struct abstract_host *ah,
	struct peer *peer, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *), void *closure,
	gfarm_int32_t command, const char *format, va_list *app)
{
	struct remote_peer *rp = peer_to_remote_peer(peer);

	return (gfmdc_master_client_remote_gfs_rpc0(ah, peer, diag,
	    result_callback, disconnect_callback, closure, command,
	    format, app, "il", GFM_PROTO_REMOTE_GFS_RPC,
	    remote_peer_get_remote_peer_id(rp)));
}

/*
 * Internal function of gfmdc_server_vput_remote_gfs_rpc_reply().
 * It converts '...' to va_list, in order to call
 * async_server_vput_wrapped_reply() which takes two va_list arguments.
 */
static gfarm_error_t
gfmdc_server_vput_remote_gfs_rpc_reply0(struct abstract_host *ah,
	struct peer *peer, gfp_xdr_xid_t xid, const char *diag,
	gfarm_error_t errcode, char *format, va_list *app, char *wformat, ...)
{
	va_list wap;
	gfarm_error_t e;
	struct peer *parent_peer;
	struct abstract_host *parent_ah;

	parent_peer = peer_get_parent(peer);
	if (parent_peer == NULL)
		return (GFARM_ERR_INVALID_REMOTE_PEER);
	peer_add_ref(parent_peer);  /* increment refcount */
	parent_ah = peer_get_abstract_host(parent_peer);

	va_start(wap, wformat);
	e = async_server_vput_wrapped_reply(parent_ah, parent_peer, xid, 0,
	    diag, GFARM_ERR_NO_ERROR, wformat, &wap, errcode, format, app);
	va_end(wap);

	peer_del_ref(parent_peer);  /* decrement refcount */
	return (e);
}

/*
 * Send a reply of GFM_PROTO_REMOTE_GFS_RPC request.
 */
gfarm_error_t
gfmdc_server_vput_remote_gfs_rpc_reply(struct abstract_host *ah,
	struct peer *peer, gfp_xdr_xid_t xid, const char *diag,
	gfarm_error_t errcode, char *format, va_list *app)
{
	return (gfmdc_server_vput_remote_gfs_rpc_reply0(ah, peer, xid, diag,
	    errcode, format, app, ""));
}

struct gfmdc_fhopen_closure {
	void *closure;
	gfarm_error_t (*callback)(void *, gfarm_error_t, gfarm_int32_t);
};

static gfarm_error_t
gfmdc_client_fhopen_result(struct mdhost *mh, struct peer *peer,
	size_t size, gfarm_error_t e, void *closure, const char *diag)
{
	struct gfmdc_fhopen_closure *c = closure;
	gfarm_int32_t mode = 0;

	if (e == GFARM_ERR_NO_ERROR) {
		if ((e = gfmdc_client_recv_result(peer, peer_get_mdhost(peer),
		     size, diag, "i", &mode)) != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: RPC error: %s", diag, gfarm_error_string(e));
	}
	(*c->callback)(c->closure, e, mode);
	free(c);
	return (e);
}

gfarm_error_t
gfmdc_client_fhopen_request(struct peer *peer,
	gfarm_ino_t inum, gfarm_int64_t igen, gfarm_uint32_t flag,
	gfarm_error_t (*callback)(void *, gfarm_error_t, gfarm_int32_t),
	void *closure)
{
	gfarm_error_t e;
	struct mdhost *mh = peer_get_mdhost(peer);
	struct gfmdc_fhopen_closure *c;
	static const char diag[] = "GFM_PROTO_FHOPEN";

	GFARM_MALLOC(c);
	if (c == NULL)
		return (GFARM_ERR_NO_MEMORY);
	c->callback = callback;
	c->closure = closure;
	if ((e = gfmdc_client_send_request_async(mh, peer,
	    gfmdc_client_fhopen_result, closure, diag, GFM_PROTO_FHOPEN,
	    "lli", inum, igen, flag)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
		return (e);
	}
	return (e);
}	

static gfarm_error_t
gfmdc_protocol_switch(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	struct mdhost *mh = abstract_host_to_mdhost(h);
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	switch (request) {
	case GFM_PROTO_JOURNAL_READY_TO_RECV:
		/* in master */
		e = gfmdc_server_journal_ready_to_recv(mh, peer, xid, size);
		break;
	case GFM_PROTO_JOURNAL_SEND:
		/* in slave */
		e = gfmdc_server_journal_send(mh, peer, xid, size);
		break;
	case GFM_PROTO_REMOTE_PEER_ALLOC:
		/* in master */
		e = gfmdc_server_remote_peer_alloc(mh, peer, xid, size);
		break;
	case GFM_PROTO_REMOTE_PEER_FREE:
		/* in master */
		e = gfmdc_server_remote_peer_free(mh, peer, xid, size);
		break;
	case GFM_PROTO_REMOTE_RPC:
		/* in master */
		e = gfmdc_server_remote_rpc(mh, peer, xid, size);
		break;
	case GFM_PROTO_REMOTE_GFS_RPC:
		if (mdhost_self_is_master()) {
			/* in master */
			e = gfmdc_master_server_remote_gfs_rpc(mh, peer, xid,
			    size);
		} else {
			/* in slave */
			e = gfmdc_slave_server_remote_gfs_rpc(mh, peer, xid,
			    size);
		}
		break;
	case GFM_PROTO_REMOTE_PEER_DISCONNECT:
		/* in slave */
		e = gfmdc_server_remote_peer_disconnect(mh, peer, xid, size);
		break;
	default:
		*unknown_request = 1;
		e = GFARM_ERR_PROTOCOL;
		break;
	}
	return (e);
}

#ifdef COMPAT_GFARM_2_3
static void
gfmdc_channel_free(struct abstract_host *h)
{
}
#endif

static void *
gfmdc_main(void *arg)
{
	struct local_peer *local_peer = arg;

	return (async_server_main(local_peer,
		gfmdc_protocol_switch
#ifdef COMPAT_GFARM_2_3
		,gfmdc_channel_free,
		NULL
#endif
		));
}

static gfarm_error_t
switch_gfmd_channel(struct peer *peer, int from_client,
	int version, const char *diag)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct mdhost *mh = NULL;
	gfp_xdr_async_peer_t async = NULL;
	struct gfmdc_peer_record *gfmdc_peer = NULL; /* shut up warning by gcc */

	giant_lock();
	assert(peer_get_async(peer) == NULL);
	if ((mh = peer_get_mdhost(peer)) == NULL) {
		gflog_error(GFARM_MSG_1002984,
			"Operation not permitted: peer_get_mdhost() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = gfmdc_peer_record_alloc(diag, &gfmdc_peer)) !=
	    GFARM_ERR_NO_ERROR) {
		/* gflog is called by gfmdc_peer_record_alloc() */
	} else if ((e = gfp_xdr_async_peer_new(&async))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002985,
		    "%s: gfp_xdr_async_peer_new(): %s",
		    diag, gfarm_error_string(e));
		gfmdc_peer_record_free(gfmdc_peer, diag);
	}
	peer_set_peer_type(peer, peer_type_gfmd_channel);
	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR) {
		struct local_peer *local_peer = peer_to_local_peer(peer);

		local_peer_set_async(local_peer, async);
		local_peer_set_readable_watcher(local_peer, gfmdc_recv_watcher);
		peer_set_gfmdc_record(peer, gfmdc_peer);

		if (mdhost_is_up(mh)) { /* throw away old connection */
			gflog_warning(GFARM_MSG_1002986,
			    "gfmd_channel(%s): switching to new connection",
			    mdhost_get_name(mh));
			mdhost_disconnect_request(mh, NULL);
		}
		mdhost_set_peer(mh, peer, version);
		local_peer_watch_readable(local_peer);
	}
	return (e);
}

gfarm_error_t
gfm_server_switch_gfmd_channel(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_error_t er = GFARM_ERR_NO_ERROR;
	gfarm_int32_t version;
	gfarm_int64_t cookie;
	struct relayed_request *relay;
	int i;
	static const char diag[] = "GFM_PROTO_SWITCH_GFMD_CHANNEL";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SWITCH_GFMD_CHANNEL, "il", &version, &cookie);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		if (from_client) {
			gflog_debug(GFARM_MSG_1002987,
			    "Operation not permitted: from_client");
			er = GFARM_ERR_OPERATION_NOT_PERMITTED;
		}
	}
	i = 0;
	if ((e = gfm_server_relay_put_reply(peer, xid, sizep, relay,
	    diag, &er, "i", &i /*XXX FIXME*/)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002988,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfp_xdr_flush(peer_get_conn(peer))) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1002989,
		    "%s: protocol flush: %s",
		    diag, gfarm_error_string(e));
	if (relay == NULL && debug_mode)
		gflog_debug(GFARM_MSG_1002990, "gfp_xdr_flush");
	return (switch_gfmd_channel(peer, from_client, version, diag));
}

static gfarm_error_t
gfmdc_connect(void)
{
	gfarm_error_t e;
	int port;
	const char *hostname;
	gfarm_int32_t gfmd_knows_me;
	struct gfm_connection *gfm_server = NULL;
	struct gfp_xdr *conn;
	struct mdhost *rhost, *master, *self_host;
	struct peer *peer = NULL;
	char *local_user;
	struct passwd *pwd;
	/* XXX FIXME must be configuable */
	unsigned int sleep_interval = 10;
	/* XXX FIXME must be configuable */
	static unsigned int sleep_max_interval = 40;
	static int hack_to_make_cookie_not_work = 0; /* XXX FIXME */
	static const char *service_user = GFMD_USERNAME;
	static const char diag[] = "gfmdc_connect";

	master = mdhost_lookup_master();
	gfarm_set_auth_id_type(GFARM_AUTH_ID_TYPE_METADATA_HOST);
	hostname = mdhost_get_name(master);
	port = mdhost_get_port(master);

	if ((e = gfarm_global_to_local_username_by_url(GFARM_PATH_ROOT,
	    service_user, &local_user)) != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1002991,
		    "no local user for the global `%s' user.",
		    service_user); /* exit */
	}
	if ((pwd = getpwnam(local_user)) == NULL) {
		gflog_fatal(GFARM_MSG_1002992,
		    "user `%s' is necessary, but doesn't exist.",
		    local_user); /* exit */
	}

	for (;;) {
		/*
		 * XXXRELAY FIXME
		 * multicast should be used always,
		 * (otherwise when a slave gfmd is promoted to master,
		 *  other slaves won't connect to the new master, see #361),
		 * but currently that doesn't work with protocol relay. FIXME.
		 */
		int multicast = gfarm_get_metadb_server_slave_listen() ? 0 : 1;

		/* try connecting to multiple destinations */
		e = gfm_client_connect_with_seteuid(hostname, port,
		    service_user, &gfm_server, NULL, pwd, multicast);
		if (e == GFARM_ERR_NO_ERROR)
			break;
		gflog_error(GFARM_MSG_1002993,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		if (sleep_interval < sleep_max_interval)
			gflog_error(GFARM_MSG_1002994,
			    "connecting to the master gfmd failed, "
			    "sleep %d sec: %s", sleep_interval,
			    gfarm_error_string(e));
		sleep(sleep_interval);
		if (mdhost_self_is_master())
			break;
		if (sleep_interval < sleep_max_interval)
			sleep_interval *= 2;
	}
	free(local_user);
	if (mdhost_self_is_master()) {
		if (gfm_server != NULL)
			gfm_client_connection_free(gfm_server);
		return (GFARM_ERR_NO_ERROR);
	}

	rhost = mdhost_lookup_metadb_server(
	    gfm_client_connection_get_real_server(gfm_server));
	assert(rhost != NULL);
	if (master != rhost) {
		mdhost_set_is_master(master, 0);
		mdhost_set_is_master(rhost, 1);
	}
	hostname = mdhost_get_name(rhost);

	/* self_host name is equal to metadb_server_host in gfmd.conf */
	self_host = mdhost_lookup_self();
	if ((e = gfm_client_hostname_set(gfm_server,
	    mdhost_get_name(self_host))) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003426,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}

	if ((e = gfm_client_switch_gfmd_channel(gfm_server,
	    GFM_PROTOCOL_VERSION, (gfarm_int64_t)hack_to_make_cookie_not_work,
	    &gfmd_knows_me))
	    != GFARM_ERR_NO_ERROR) {
		if (gfm_client_is_connection_error(e)) {
			gflog_error(GFARM_MSG_1003427,
			    "gfmd_channel(%s) : %s",
			    hostname, gfarm_error_string(e));
		} else {
			gflog_error(GFARM_MSG_1003428,
			    "gfmd_channel(%s) : authorization denied. "
			    "set metadb_server_host properly. : %s",
			    hostname, gfarm_error_string(e));
		}
		return (e);
	}
	/* NOTE: gfm_client_connection_convert_to_xdr() frees `gfm_server' */
	conn = gfm_client_connection_convert_to_xdr(gfm_server);
	if ((e = local_peer_alloc_with_connection(conn,
	    mdhost_to_abstract_host(rhost), GFARM_AUTH_ID_TYPE_METADATA_HOST,
	    &peer)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002996,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		gfp_xdr_free(conn);
		return (e);
	}
	if ((e = switch_gfmd_channel(peer, 0, GFM_PROTOCOL_VERSION, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002997,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfmdc_client_journal_ready_to_recv(rhost, peer))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003429,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}
	gflog_info(GFARM_MSG_1002998,
	    "gfmd_channel(%s) : connected", hostname);
	return (GFARM_ERR_NO_ERROR);
}

static int
gfmdc_journal_mdhost_can_sync(struct mdhost *mh,
	struct gfmdc_peer_record *gfmdc_peer)
{
	return (!mdhost_is_self(mh) && mdhost_is_up(mh) &&
	    gfmdc_peer_get_journal_file_reader(gfmdc_peer) != NULL);
}

static gfarm_error_t
gfmdc_journal_asyncsend(struct mdhost *mh, struct peer *peer, int *exist_recsp)
{
	gfarm_error_t e;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	gfarm_uint64_t to_sn;
	struct gfmdc_journal_send_closure *c;

	if ((mdhost_is_sync_replication(mh) &&
	    !gfmdc_peer_is_in_first_sync(gfmdc_peer)) ||
	    !gfmdc_journal_mdhost_can_sync(mh, gfmdc_peer))
		return (GFARM_ERR_NO_ERROR);
	c = gfmdc_journal_asyncsend_alloc(mh);
	if (c == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1002999,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfmdc_client_journal_asyncsend(peer, c, &to_sn))
	    != GFARM_ERR_NO_ERROR) {
		gfmdc_journal_asyncsend_free(c);
		mdhost_disconnect_request(mh, peer);
	} else if (to_sn == 0) {
		gfmdc_journal_asyncsend_free(c);
		*exist_recsp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	*exist_recsp = 0;
	return (GFARM_ERR_NO_ERROR);
}

static int
gfmdc_journal_asyncsend_each_mdhost(struct mdhost *mh, void *closure)
{
	int exist_recs;
	struct mdhost *self = mdhost_lookup_self();
	struct peer *peer;

	if (mh != self && !mdhost_is_sync_replication(mh)) {
		peer = mdhost_get_peer(mh); /* increment refcount */
		if (peer != NULL) {
			(void)gfmdc_journal_asyncsend(mh, peer, &exist_recs);
			mdhost_put_peer(mh, peer); /* decrement refcount */
		}
	}
	return (1);
}

void *
gfmdc_journal_asyncsend_thread(void *arg)
{
	static const char diag[] = "gfmdc_journal_asyncsend_thread";

	for (;;) {
		gfarm_mutex_lock(&journal_sync_info.async_mutex, diag,
		    ASYNC_MUTEX_DIAG);
		while (!mdhost_has_async_replication_target()) {
			gfarm_cond_wait(&journal_sync_info.async_wait_cond,
			    &journal_sync_info.async_mutex,
			    diag, ASYNC_WAIT_COND_DIAG);
		}
		mdhost_foreach(gfmdc_journal_asyncsend_each_mdhost, NULL);
		gfarm_mutex_unlock(&journal_sync_info.async_mutex, diag,
		    ASYNC_MUTEX_DIAG);
		db_journal_wait_until_readable();
	}
	return (NULL);
}

static void
gfmdc_journal_asyncsend_thread_wakeup(void)
{
	static const char diag[] = "gfmdc_journal_asyncsend_thread_wakeup";

	if (mdhost_has_async_replication_target())
		gfarm_cond_signal(&journal_sync_info.async_wait_cond, diag,
			ASYNC_WAIT_COND_DIAG);
}

void *
gfmdc_connect_thread(void *arg)
{
	gfarm_error_t e;
	struct mdhost *mh;
	struct peer *peer;
	static const char diag[] = "gfmdc_connect_thread";

	for (;;) {
		mh = mdhost_lookup_master();
		peer = mdhost_get_peer(mh); /* increment refcount */
		if (peer != NULL) { /* already connected to the master? */
			mdhost_put_peer(mh, peer); /* decrement refcount */
			sleep(GFMDC_CONNECT_INTERVAL);
		} else if ((e = gfmdc_connect()) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003430,
			    "gfmd_channel : "
			    "give up to connect to the master gfmd: %s",
			    gfarm_error_string(e));
			gfmd_terminate(diag);
			/* NOTREACHED */
		}
		if (mdhost_self_is_master())
			break;
	}
	return (NULL);
}

static void *
gfmdc_journal_file_sync_thread(void *arg)
{
	static const char diag[] = "db_journal_file_sync_thread";

#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003000,
	    "journal_file_sync start");
#endif
	journal_sync_info.file_sync_error = db_journal_file_writer_sync();
	gfmdc_journal_recv_end_signal(diag);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003001,
	    "journal_file_sync end");
#endif
	return (NULL);
}

/* caller should do mdhost_get_peer(c->peer) before calling this function */
static void *
gfmdc_journal_syncsend_thread(void *closure)
{
	struct peer *peer = closure;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	struct gfmdc_journal_send_closure *c =
	    gfmdc_peer_get_journal_send_closure(gfmdc_peer);
	struct mdhost *mh = peer_get_mdhost(peer);
	gfarm_error_t e;
	gfarm_uint64_t to_sn;
	static const char diag[] = "gfmdc_journal_syncsend_thread";

	assert(gfmdc_peer != NULL);
	assert(c != NULL);
	assert(c->data == NULL);
	assert(mh != NULL);

	gfmdc_journal_send_closure_reset(c, mh);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003002,
	    "journal_syncsend(%s) start", mdhost_get_name(c->host));
#endif
	do {
		if ((e = gfmdc_client_journal_syncsend(peer, c, &to_sn))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003003,
			    "%s : failed to send journal : %s",
			    mdhost_get_name(c->host), gfarm_error_string(e));
			break;
		}
		if (to_sn == 0) {
			gflog_warning(GFARM_MSG_1003004,
			    "%s : no journal records to send (seqnum=%lld) "
			    ": %s", mdhost_get_name(c->host),
			    (unsigned long long)journal_sync_info.seqnum,
			    gfarm_error_string(e));
			break;
		}
	} while (to_sn < journal_sync_info.seqnum);

	if (e != GFARM_ERR_NO_ERROR)
		mdhost_disconnect_request(c->host, NULL);

	mdhost_put_peer(c->host, peer); /* decrement refcount */
	gfmdc_journal_recv_end_signal(diag);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003005,
	    "journal_syncsend(%s) end", mdhost_get_name(c->host));
#endif
	return (NULL);
}

static void
gfmdc_wait_journal_recv_threads(const char *diag)
{
	gfarm_mutex_lock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	while (journal_sync_info.nrecv_threads > 0)
		gfarm_cond_wait(&journal_sync_info.sync_end_cond,
		    &journal_sync_info.sync_mutex, diag,
		    SYNC_END_COND_DIAG);
	gfarm_mutex_unlock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
}

static int
gfmdc_is_sync_replication_target(struct mdhost *mh, struct peer *peer)
{
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	struct mdhost *self = mdhost_lookup_self();

	return ((mh != self) &&
	    mdhost_is_sync_replication(mh) &&
	    gfmdc_journal_mdhost_can_sync(mh, gfmdc_peer) &&
	    !gfmdc_peer_is_in_first_sync(gfmdc_peer));
}

static int
gfmdc_journal_sync_count_host(struct mdhost *mh, void *closure)
{
	int *countp = closure;
	struct peer *peer = mdhost_get_peer(mh); /* increment refcount */

	if (peer != NULL) {
		(*countp) += gfmdc_is_sync_replication_target(mh, peer);
		mdhost_put_peer(mh, peer); /* decrement refcount */
	}
	return (1);
}

static int
gfmdc_journal_sync_mdhost_add_job(struct mdhost *mh, void *closure)
{
	struct peer *peer = mdhost_get_peer(mh); /* increment refcount */
	static const char diag[] = "gfmdc_journal_sync_mdhost_add_job";

	if (peer == NULL)
		return (1);
	if (!gfmdc_is_sync_replication_target(mh, peer)) {
		mdhost_put_peer(mh, peer); /* decrement refcount */
		return (1);
	}
	gfarm_mutex_lock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	++journal_sync_info.nrecv_threads;
	gfarm_mutex_unlock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	/* gfmdc_journal_syncsend_thread() decrements refcount */
	thrpool_add_job(journal_sync_thread_pool,
	    gfmdc_journal_syncsend_thread, peer);
	return (1);
}

/* PREREQUISITE: giant_lock */
static gfarm_error_t
gfmdc_journal_sync_multiple(gfarm_uint64_t seqnum)
{
	int nhosts = 0;
	static const char diag[] = "gfmdc_journal_sync_multiple";

	mdhost_foreach(gfmdc_journal_sync_count_host, &nhosts);
	if (nhosts == 0) {
		if (gfarm_get_journal_sync_file())
			return (db_journal_file_writer_sync());
		return (GFARM_ERR_NO_ERROR);
	}

	assert(journal_sync_info.nrecv_threads == 0);

	journal_sync_info.file_sync_error = GFARM_ERR_NO_ERROR;
	journal_sync_info.seqnum = seqnum;
	if (gfarm_get_journal_sync_file()) {
		journal_sync_info.nrecv_threads = 1;
		thrpool_add_job(journal_sync_thread_pool,
		    gfmdc_journal_file_sync_thread, NULL);
	} else
		journal_sync_info.nrecv_threads = 0;
	mdhost_foreach(gfmdc_journal_sync_mdhost_add_job, NULL);

	gfmdc_wait_journal_recv_threads(diag);

	return (journal_sync_info.file_sync_error);
}

/* caller should do peer_add_ref(peer) before calling this function */
static void*
gfmdc_journal_first_sync_thread(void *closure)
{
#define FIRST_SYNC_DELAY 1
	gfarm_error_t e;
	struct peer *peer = closure;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	int do_sync, exist_recs = 1;
	struct mdhost *mh = peer_get_mdhost(peer);

	sleep(FIRST_SYNC_DELAY);
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003006,
	    "%s : first sync start", mdhost_get_name(mh));
#endif
	giant_lock();
	do_sync = 0;
	if (gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer) <
	    db_journal_get_current_seqnum()) {
		/* it's still unknown whether seqnum is ok or out_of_sync */
		if (!gfmdc_peer_is_in_first_sync(gfmdc_peer) &&
		    !gfmdc_peer_journal_file_reader_is_expired(gfmdc_peer))
			do_sync = 1;
	} else {
		mdhost_set_seqnum_ok(mh);
	}
	giant_unlock();

	if (!do_sync) {
		peer_del_ref(peer); /* decrement refcount */
		return (NULL);
	}

	gfmdc_peer_set_is_in_first_sync(gfmdc_peer, 1);

	while (exist_recs) {
		if ((e = gfmdc_journal_asyncsend(mh, peer, &exist_recs))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003007,
			    "%s : %s", mdhost_get_name(mh),
			    gfarm_error_string(e));
			break;
		}
	}
#ifdef DEBUG_JOURNAL
	gflog_debug(GFARM_MSG_1003008,
	    "%s : first sync end", mdhost_get_name(mh));
#endif

	gfmdc_peer_set_is_in_first_sync(gfmdc_peer, 0);
	peer_del_ref(peer); /* decrement refcount */
	return (NULL);
}

static void
gfmdc_sync_init(void)
{
	int thrpool_size, jobq_len;
	struct gfmdc_journal_sync_info *si = &journal_sync_info;
	static const char diag[] = "gfmdc_sync_init";

	thrpool_size = gfarm_get_metadb_server_slave_max_size()
		+ (gfarm_get_journal_sync_file() ? 1 : 0);
	jobq_len = thrpool_size + 1;

	journal_sync_thread_pool = thrpool_new(thrpool_size, jobq_len,
	    "sending and writing journal record");
	if (journal_sync_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1003010,
		    "thread pool size:%d, queue length:%d: no memory",
		    thrpool_size, jobq_len); /* exit */

	gfarm_mutex_init(&si->sync_mutex, diag, SYNC_MUTEX_DIAG);
	gfarm_cond_init(&si->sync_end_cond, diag, SYNC_END_COND_DIAG);
	gfarm_mutex_init(&si->async_mutex, diag, ASYNC_MUTEX_DIAG);
	gfarm_cond_init(&si->async_wait_cond, diag, ASYNC_WAIT_COND_DIAG);

	si->nrecv_threads = 0;
	db_journal_set_sync_op(gfmdc_journal_sync_multiple);
}


void
gfmdc_init(void)
{
	mdhost_set_update_hook_for_journal_send(
	    gfmdc_journal_asyncsend_thread_wakeup);

	gfmdc_recv_watcher = peer_watcher_alloc(
	    /* XXX FIXME use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    gfmdc_main, "receiving from gfmd");

	gfmdc_send_thread_pool = thrpool_new(
	    /* XXX FIXME use different config parameter */
	    gfarm_metadb_thread_pool_size, gfarm_metadb_job_queue_length,
	    "sending to gfmd");
	if (gfmdc_send_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_1003011,
		    "gfmd channel thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);
	gfmdc_sync_init();
}

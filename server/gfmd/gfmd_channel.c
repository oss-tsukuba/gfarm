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
#include <time.h>
#include <pwd.h>

#include <gfarm/gflog.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "nanosec.h"
#include "thrsubr.h"
#include "gfutil.h"

#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "gfm_client.h"
#include "metadb_server.h"
#include "config.h"

#include "peer.h"
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
#include "gfmd_channel.h"
#include "gfmd.h"

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

static const char JOURNAL_READY_TO_RECV_MUTEX_DIAG[] =
					"journal_ready_to_recv_info.mutex";
static const char JOURNAL_READY_TO_RECV_WAIT_COND_DIAG[] =
					"journal_ready_to_recv_info.wait_cond";
static const char PEER_RECORD_MUTEX_DIAG[] = "gfmdc_peer_record.mutex";

#define GFMDC_CONNECT_INTERVAL	30
#define GFMDC_SEND_DEFAULT_TIMEOUT	1000000 /* 1 sec. */

/*
 * gmfdc_journal_send_closure
 */

struct gfmdc_journal_send_closure {
	struct mdhost *host;
	void *data;
	gfarm_uint64_t to_sn;

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
	c->to_sn = 0;
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

static void gfmdc_journal_transfer_event(void);

static void
gfmdc_journal_send_closure_reset(struct gfmdc_journal_send_closure *c,
	struct mdhost *mh)
{
	c->host = mh;
	c->data = NULL;
	c->to_sn = 0;
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

	gfmdc_journal_transfer_event();
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

	gfmdc_journal_transfer_event();
}

/*
 * gfmdc_peer_record
 */

/* per peer record for gfmd channel */
struct gfmdc_peer_record {
	pthread_mutex_t mutex;
	struct journal_file_reader *jreader;
	gfarm_uint64_t last_fetch_seqnum;
	int is_received_seqnum, first_sync_completed;

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
	gfmdc_peer->first_sync_completed = 0;

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
gfmdc_peer_is_first_sync_completed(struct gfmdc_peer_record *gfmdc_peer)
{
	int r;
	static const char diag[] = "gfmdc_peer_is_first_sync_completed";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	r = gfmdc_peer->first_sync_completed;
	gfmdc_peer_mutex_unlock(gfmdc_peer, diag);
	return (r);
}

void
gfmdc_peer_set_first_sync_completed(struct gfmdc_peer_record *gfmdc_peer,
	int flag)
{
	static const char diag[] = "gfmdc_peer_set_first_sync_completed";

	gfmdc_peer_mutex_lock(gfmdc_peer, diag);
	gfmdc_peer->first_sync_completed = flag;
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
	e = gfm_server_channel_vget_request(peer, size, diag, format, &ap);
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
	e = gfm_server_channel_vput_reply(
	    mdhost_to_abstract_host(mh), peer, xid, diag,
	    errcode, format, &ap);
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
	e = gfm_client_channel_vrecv_result(peer,
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

static gfarm_error_t
gfmdc_client_send_request(struct mdhost *mh,
	struct peer *peer0, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure, long timeout_microsec,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	/* XXX FIXME gfm_client_channel_vsend_request must be async-request */
	e = gfm_client_channel_vsend_request(
	    mdhost_to_abstract_host(mh), peer0, diag,
	    result_callback, disconnect_callback, closure,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    timeout_microsec, command, format, &ap);
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

static void
gfmdc_client_journal_syncsend_free(void *p, void *arg)
{
	gfmdc_journal_syncsend_completed(arg);
}

static void
gfmdc_client_journal_asyncsend_free(void *p, void *arg)
{
	gfmdc_journal_asyncsend_free(arg);
}

static gfarm_error_t
gfmdc_client_journal_send_result_common(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct gfmdc_journal_send_closure *c = arg;
	static const char *diag = "GFM_PROTO_JOURNAL_SEND";

	if ((e = gfmdc_client_recv_result(peer, c->host, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002976,
		    "%s : %s", mdhost_get_name(c->host),
		    gfarm_error_string(e));
	else if (c->to_sn >= db_journal_get_current_seqnum())
		mdhost_set_seqnum_ok(peer_get_mdhost(peer));
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_syncsend_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;

	e = gfmdc_client_journal_send_result_common(p, arg, size);
	gfmdc_journal_syncsend_completed(arg);
	return (e);
}

static gfarm_error_t
gfmdc_client_journal_asyncsend_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;

	e = gfmdc_client_journal_send_result_common(p, arg, size);
	gfmdc_journal_asyncsend_free(arg);
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
	if (in_time == 0)
		gflog_notice(GFARM_MSG_1004270,
		    "journal send to %s: timeout due to no response "
		    "within %d seconds "
		    "(synchronous_journal_timeout)", mdhost_get_name(c->host),
		     gfarm_get_journal_sync_slave_timeout());
	return (r);
}

static gfarm_error_t
gfmdc_client_journal_send(struct peer *peer,
	gfarm_error_t (*result_op)(void *, void *, size_t),
	void (*disconnect_op)(void *, void *),
	struct gfmdc_journal_send_closure *c,
	gfarm_uint64_t *to_snp)
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
	mdhost_set_seqnum_behind(mh);
	gfmdc_peer_set_last_fetch_seqnum(gfmdc_peer, to_sn);
	c->data = data;
	c->to_sn = to_sn;

	if ((e = gfmdc_client_send_request(mh, peer, diag,
	    result_op, disconnect_op, c, GFMDC_SEND_DEFAULT_TIMEOUT,
	    GFM_PROTO_JOURNAL_SEND, "llb", from_sn, to_sn,
	    (size_t)data_len, data)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002978,
		    "%s: %s", mdhost_get_name(mh), gfarm_error_string(e));
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
	    gfmdc_client_journal_syncsend_result,
	    gfmdc_client_journal_syncsend_free, c, to_snp);
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
	    gfmdc_client_journal_asyncsend_result,
	    gfmdc_client_journal_asyncsend_free, c, to_snp));
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

	e = gfmdc_server_get_request(peer, size, diag, "llB",
	    &from_sn, &to_sn, &recs_len, &recs);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1004763, "from %s : %s: %s",
		    mdhost_get_name(mh), diag, gfarm_error_string(e));
		return (e);
	}

	er = db_journal_recvq_enter(from_sn, to_sn, recs_len, recs);
	if (er != GFARM_ERR_NO_ERROR) { /* probably GFARM_ERR_NO_MEMORY */
		gflog_error(GFARM_MSG_1004764,
		    "from %s : %s: db_journal_recvq_enter(): %s",
		    mdhost_get_name(mh), diag, gfarm_error_string(e));
	} else {
#ifdef DEBUG_JOURNAL
		gflog_debug(GFARM_MSG_1002979,
		    "from %s : recv journal %llu to %llu",
		    mdhost_get_name(mh), (unsigned long long)from_sn,
		    (unsigned long long)to_sn);
#endif
	}

	e = gfmdc_server_put_reply(mh, peer, xid, diag, er, "");
	return (e);
}

static void* gfmdc_journal_first_sync_thread(void *);

static gfarm_error_t
gfmdc_server_journal_ready_to_recv(struct mdhost *mh, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, e_rpc;
	gfarm_uint64_t seqnum;
	int inited = 0;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	struct journal_file_reader *reader;
	const char *host = mdhost_get_name(mh);
	static const char diag[] = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	if ((e = gfmdc_server_get_request(peer, size, diag, "l", &seqnum))
	    == GFARM_ERR_NO_ERROR) {
		gfmdc_peer_set_last_fetch_seqnum(gfmdc_peer, seqnum);
		gfmdc_peer_set_is_received_seqnum(gfmdc_peer, 1);
#ifdef DEBUG_JOURNAL
		gflog_debug(GFARM_MSG_1002980,
		    "%s : last_fetch_seqnum=%llu",
		    host, (unsigned long long)seqnum);
#endif
		reader = gfmdc_peer_get_journal_file_reader(gfmdc_peer);
		if ((e = db_journal_reader_reopen_if_needed(host, &reader,
		    gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer), &inited))
		    == GFARM_ERR_NO_ERROR) {
			gfarm_uint64_t to_sn = db_journal_get_current_seqnum();

			if (seqnum == to_sn) {
				mdhost_set_seqnum_ok(mh);
				gflog_info(GFARM_MSG_1004765,
				    "slave %s: already sync at seqnum %llu",
				    host, (unsigned long long)seqnum);
			} else if (seqnum < to_sn) {
				mdhost_set_seqnum_behind(mh);
				gflog_info(GFARM_MSG_1004766,
				    "slave %s is behind: "
				    "target seqnum %llu, current %llu",
				    host,
				    (unsigned long long)to_sn,
				    (unsigned long long)seqnum);
			} else {
				e = GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN;
				mdhost_set_seqnum_state_by_error(mh, e);
				gflog_error(GFARM_MSG_1004767,
				    "slave %s reported invalid seqnum: "
				    "master %llu, slave %llu",
				    host,
				    (unsigned long long)to_sn,
				    (unsigned long long)seqnum);
			}
		} else {
			mdhost_set_seqnum_state_by_error(mh, e);
			gflog_error(GFARM_MSG_1002981,
			    "gfmd_channel(%s) : %s",
			    host, gfarm_error_string(e));
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
	e_rpc = gfmdc_server_put_reply(mh, peer, xid, diag, e, "");
	if (e == GFARM_ERR_NO_ERROR)
		e = e_rpc;
	if (e == GFARM_ERR_NO_ERROR && mdhost_is_sync_replication(mh)) {
		peer_add_ref(peer); /* increment refcount */
		thrpool_add_job(journal_sync_thread_pool,
		    gfmdc_journal_first_sync_thread, peer);
	}
	return (e_rpc);
}

struct journal_ready_to_recv_info {
	int end;
	pthread_mutex_t mutex;
	pthread_cond_t wait_cond;
	gfarm_error_t error;
};

static void
gfmdc_client_journal_ready_to_recv_notify(void *arg, gfarm_error_t e,
	const char *diag)
{
	struct journal_ready_to_recv_info *ji = arg;

	ji->error = e;
	gfarm_mutex_lock(&ji->mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
	ji->end = 1;
	gfarm_cond_signal(&ji->wait_cond, diag,
	    JOURNAL_READY_TO_RECV_WAIT_COND_DIAG);
	gfarm_mutex_unlock(&ji->mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
}

static gfarm_int32_t
gfmdc_client_journal_ready_to_recv_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *mh = peer_get_mdhost(peer);
	static const char *diag = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	if ((e = gfmdc_client_recv_result(peer, mh, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002982, "%s: %s",
		    mdhost_get_name(mh), gfarm_error_string(e));
	gfmdc_client_journal_ready_to_recv_notify(arg, e, diag);
	return (e);
}

static void
gfmdc_client_journal_ready_to_recv_disconnect(void *p, void *arg)
{
	static const char *diag =
		"gfmdc_client_journal_ready_to_recv_disconnect";

	gfmdc_client_journal_ready_to_recv_notify(arg,
	    GFARM_ERR_CONNECTION_ABORTED, diag);
}

gfarm_error_t
gfmdc_client_journal_ready_to_recv(struct mdhost *mh, struct peer *peer)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	struct journal_ready_to_recv_info ji;
	static const char diag[] = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	gfarm_mutex_init(&ji.mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
	gfarm_cond_init(&ji.wait_cond, diag,
	    JOURNAL_READY_TO_RECV_WAIT_COND_DIAG);
	ji.error = GFARM_ERR_NO_ERROR;
	ji.end = 0;

	seqnum = db_journal_get_current_seqnum();

	if ((e = gfmdc_client_send_request(mh, peer, diag,
	    gfmdc_client_journal_ready_to_recv_result,
	    gfmdc_client_journal_ready_to_recv_disconnect, &ji,
	    GFMDC_SEND_DEFAULT_TIMEOUT,
	    GFM_PROTO_JOURNAL_READY_TO_RECV, "l", seqnum))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002983,
		    "%s: %s", mdhost_get_name(mh), gfarm_error_string(e));
		goto end;
	}

	/* wait for recieving result to get GFARM_ERR_EXPIRED */
	gfarm_mutex_lock(&ji.mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
	while (!ji.end) {
		gfarm_cond_wait(&ji.wait_cond, &ji.mutex, diag,
		    JOURNAL_READY_TO_RECV_WAIT_COND_DIAG);
	}
	gfarm_mutex_unlock(&ji.mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
	e = ji.error;
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003425,
		    "%s: %s", mdhost_get_name(mh), gfarm_error_string(e));
	}

end:
	gfarm_mutex_destroy(&ji.mutex, diag, JOURNAL_READY_TO_RECV_MUTEX_DIAG);
	gfarm_cond_destroy(&ji.wait_cond, diag,
	    JOURNAL_READY_TO_RECV_WAIT_COND_DIAG);

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
	return (gfm_server_channel_main(arg,
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
	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR) {
		if (mdhost_is_up(mh)) { /* throw away old connection */
			gflog_warning(GFARM_MSG_1002986,
			    "gfmd_channel(%s): switching to new connection",
			    mdhost_get_name(mh));
			mdhost_disconnect_request(mh, NULL);
		}

		/*
		 * SF.net #1019
		 * need giant_lock during transition to gfmd channel
		 */
		giant_lock();
		peer_set_async(peer, async);
		peer_set_watcher(peer, gfmdc_recv_watcher);
		peer_set_gfmdc_record(peer, gfmdc_peer);
		mdhost_set_peer(mh, peer, version);

		peer_watch_access(peer);
		giant_unlock();
	}
	return (e);
}

gfarm_error_t
gfm_server_switch_gfmd_channel(struct peer *peer, int from_client,
	int skip)
{
	gfarm_error_t e, er;
	gfarm_int32_t version;
	gfarm_int64_t cookie;
	static const char diag[] = "GFM_PROTO_SWITCH_GFMD_CHANNEL";

	e = gfm_server_get_request(peer, diag, "il", &version, &cookie);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (from_client) {
		gflog_debug(GFARM_MSG_1002987,
			"Operation not permitted: from_client");
		er = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		er = GFARM_ERR_NO_ERROR;
	if ((e = gfm_server_put_reply(peer, diag, er, "i", 0 /*XXX FIXME*/))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1002988,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfp_xdr_flush(peer_get_conn(peer))) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1002989,
		    "%s: protocol flush: %s",
		    diag, gfarm_error_string(e));
	if (debug_mode)
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
	if (master == NULL)
		gflog_fatal(GFARM_MSG_1004737, "no master, abort");
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
		/* try connecting to multiple destinations */
		e = gfm_client_connect_with_seteuid(hostname, port,
		    service_user, &gfm_server, NULL, pwd, 1);
		if (e == GFARM_ERR_NO_ERROR)
			break;
		gflog_error(GFARM_MSG_1002993,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		if (sleep_interval < sleep_max_interval)
			gflog_notice(GFARM_MSG_1002994,
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
	if ((e = peer_alloc_with_connection(&peer, conn,
	    mdhost_to_abstract_host(rhost),
	    GFARM_AUTH_ID_TYPE_METADATA_HOST)) != GFARM_ERR_NO_ERROR) {
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

/*
 * This function is similar to gfmdc_connect(), but it only examines
 * whether another gfmd is running.
 */
int
gfmdc_is_master_gfmd_running(void)
{
	gfarm_error_t e;
	int port;
	const char *hostname;
	struct mdhost *master;
	struct gfm_connection *gfm_server = NULL;
	char *local_user;
	struct passwd *pwd;
	static const char *service_user = GFMD_USERNAME;
	struct gfarm_metadb_server *real_server;

	master = mdhost_lookup_master();
	if (master == NULL)
		gflog_fatal(GFARM_MSG_1004738, "no master, abort");
	gfarm_set_auth_id_type(GFARM_AUTH_ID_TYPE_METADATA_HOST);
	hostname = mdhost_get_name(master);
	port = mdhost_get_port(master);

	if ((e = gfarm_global_to_local_username_by_url(GFARM_PATH_ROOT,
	    service_user, &local_user)) != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1003841,
		    "no local user for the global `%s' user.",
		    service_user); /* exit */
	}
	if ((pwd = getpwnam(local_user)) == NULL) {
		gflog_fatal(GFARM_MSG_1003842,
		    "user `%s' is necessary, but doesn't exist.",
		    local_user); /* exit */
	}
	free(local_user);

	/* try connecting to multiple destinations */
	e = gfm_client_connect_with_seteuid(hostname, port,
	    service_user, &gfm_server, NULL, pwd, 1);
	if (e == GFARM_ERR_NO_ERROR) {
		real_server =
		    gfm_client_connection_get_real_server(gfm_server);
		gflog_notice(GFARM_MSG_1003843,
		    "master gfmd is running on %s",
		    gfarm_metadb_server_get_name(real_server));
		gfm_client_connection_free(gfm_server);
		return (1);
	}
	return (0);
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
	    gfmdc_peer_is_first_sync_completed(gfmdc_peer)) ||
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
	if (to_sn >= db_journal_get_current_seqnum())
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

static gfarm_error_t
gfmdc_peer_switch_to_sync(struct mdhost *mh)
{
	struct peer *peer = mdhost_get_peer(mh); /* increment refcount */
	if (peer != NULL) {
		thrpool_add_job(journal_sync_thread_pool,
		    gfmdc_journal_first_sync_thread, peer);
	}
	return (GFARM_ERR_NO_ERROR);
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
		if (mh == NULL)
			gflog_fatal(GFARM_MSG_1004739, "no master, abort");
		peer = mdhost_get_peer(mh); /* increment refcount */
		if (peer != NULL) { /* already connected to the master? */
			mdhost_put_peer(mh, peer); /* decrement refcount */
			sleep(GFMDC_CONNECT_INTERVAL);
		} else if ((e = gfmdc_connect()) != GFARM_ERR_NO_ERROR &&
		    !gfm_client_is_connection_error(e)) {
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
	struct journal_file_reader *reader;
	gfarm_error_t e;
	gfarm_uint64_t to_sn;
	static const char diag[] = "gfmdc_journal_send_thread";

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
			    "%s: failed to send journal: %s",
			    mdhost_get_name(c->host), gfarm_error_string(e));
			break;
		}
		if (to_sn == 0) {
			gflog_warning(GFARM_MSG_1003004,
			    "%s: no journal record to send (seqnum=%lld)"
			    ": %s", mdhost_get_name(c->host),
			    (unsigned long long)journal_sync_info.seqnum,
			    gfarm_error_string(e));
			break;
		}
	} while (to_sn < journal_sync_info.seqnum);

	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * send signal to nonfull cond just in case the writer
		 * waits for this reader thread
		 */
		reader = gfmdc_peer_get_journal_file_reader(gfmdc_peer);
		journal_file_nonfull_cond_signal(reader, diag);
		mdhost_disconnect_request(c->host, NULL);
	}
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
	struct timespec ts;
	int in_time, timeout = gfarm_get_journal_sync_slave_timeout();
	int timeout_max = 120;

	gfarm_gettime(&ts);
	ts.tv_sec += timeout;
	gfarm_mutex_lock(&journal_sync_info.sync_mutex, diag,
	    SYNC_MUTEX_DIAG);
	while (journal_sync_info.nrecv_threads > 0) {
		in_time = gfarm_cond_timedwait(
		    &journal_sync_info.sync_end_cond,
		    &journal_sync_info.sync_mutex, &ts, diag,
		    SYNC_END_COND_DIAG);
		if (in_time == 0) {
			gflog_warning(GFARM_MSG_1004271,
			    "journal response timeout (%d seconds), check "
			    "synchronous slave servers", timeout);
			timeout *= 2;
			if (timeout > timeout_max)
				timeout = timeout_max;
			gfarm_gettime(&ts);
			ts.tv_sec += timeout;
		}
	}
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
	    gfmdc_peer_is_first_sync_completed(gfmdc_peer));
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
	const char diag[] = "gfmdc_journal_sync_mdhost_add_job";

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
		if (gfarm_get_journal_sync_file()) {
			int e = db_journal_file_writer_sync();
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_fatal(GFARM_MSG_1004208,
				    "failed to sync the journal file: %s",
				    gfarm_error_string(e));
			}
			return (e);
		}
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
	if (journal_sync_info.file_sync_error != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_1004209,
		    "failed to sync the journal file: %s",
		    gfarm_error_string(journal_sync_info.file_sync_error));
	}

	return (journal_sync_info.file_sync_error);
}

/* caller should do peer_add_ref(peer) before calling this function */
static void*
gfmdc_journal_first_sync_thread(void *closure)
{
	gfarm_error_t e;
	struct peer *peer = closure;
	struct gfmdc_peer_record *gfmdc_peer = peer_get_gfmdc_record(peer);
	int do_first_sync = 0;
	int exist_recs = 0;
	struct mdhost *mh = peer_get_mdhost(peer);
	gfarm_uint64_t lf_sn, to_sn;

	lf_sn = gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer);
	to_sn = db_journal_get_current_seqnum();
	if (lf_sn < to_sn) {
		/* it's still unknown whether seqnum is ok or out_of_sync */
		if (!gfmdc_peer_journal_file_reader_is_expired(gfmdc_peer)) {
			do_first_sync = 1;
			exist_recs = 1;
			gflog_info(GFARM_MSG_1004768,
			    "%s: first sync started. "
			    "till seqnum %llu, currently %llu",
			    mdhost_get_name(mh),
			    (unsigned long long)to_sn,
			    (unsigned long long)lf_sn);
		} else {
			gflog_info(GFARM_MSG_1004769,
			    "%s: first sync failed due to expiration. "
			    "master seqnum %llu, slave %llu",
			    mdhost_get_name(mh),
			    (unsigned long long)to_sn,
			    (unsigned long long)lf_sn);
		}
	} else {
		mdhost_set_seqnum_ok(mh);
		gflog_info(GFARM_MSG_1004770,
		    "%s: first sync is unnecessary. "
		    "master seqnum %llu, slave %llu",
		    mdhost_get_name(mh),
		    (unsigned long long)to_sn,
		    (unsigned long long)lf_sn);
	}

	while (exist_recs) {
		if ((e = gfmdc_journal_asyncsend(mh, peer, &exist_recs))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003007,
			    "%s : %s", mdhost_get_name(mh),
			    gfarm_error_string(e));
			break;
		}
	}

	if (do_first_sync) {
		lf_sn = gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer);
		if (exist_recs)
			gflog_info(GFARM_MSG_1004771,
			    "%s: first sync aborted at seqnum %llu",
			    mdhost_get_name(mh),
			    (unsigned long long)lf_sn);
		else
			gflog_info(GFARM_MSG_1004772,
			    "%s: first sync completed at seqnum %llu",
			    mdhost_get_name(mh),
			    (unsigned long long)lf_sn);
	}

	gfmdc_peer_set_first_sync_completed(gfmdc_peer, 1);
	peer_del_ref(peer); /* decrement refcount */
	return (NULL);
}

/*
 * gfmdc_journal_transfer_wait() related variables and functions
 */

static enum { verbose_silent, verbose_wait, verbose_timeout }
	journal_transfer_wait_verbose = verbose_silent;
static int journal_transfer_waiting = 0;
static pthread_mutex_t journal_transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t journal_transfer_event = PTHREAD_COND_INITIALIZER;
static const char JOURNAL_TRANSFER_MUTEX_DIAG[] = "journal_transfer_mutex";
static const char JOURNAL_TRANSFER_EVENT_DIAG[] = "journal_transfer_event";

static void
gfmdc_journal_transfer_event(void)
{
	const char diag[] = "gfmdc_journal_transfer_event";

	gfarm_mutex_lock(&journal_transfer_mutex,
	    diag, JOURNAL_TRANSFER_MUTEX_DIAG);
	if (journal_transfer_waiting)
		gfarm_cond_signal(&journal_transfer_event,
		    diag, JOURNAL_TRANSFER_EVENT_DIAG);
	gfarm_mutex_unlock(&journal_transfer_mutex,
	    diag, JOURNAL_TRANSFER_MUTEX_DIAG);
}

static int
gfmdc_journal_transferring_each_mdhost(struct mdhost *mh, void *closure)
{
	int *transferringp = closure;
	struct peer *peer = mdhost_get_peer(mh); /* increment refcount */

	/* don't touch *transferringp, if peer == NULL || gfmdc_peer == NULL */
	if (peer != NULL && mdhost_may_transfer_journal(mh)) {
		struct gfmdc_peer_record *gfmdc_peer;

		gfmdc_peer = peer_get_gfmdc_record(peer);
		if (gfmdc_peer != NULL &&
		    gfmdc_journal_mdhost_can_sync(mh, gfmdc_peer)) {
			gfarm_uint64_t lf_sn, to_sn;

			lf_sn = gfmdc_peer_get_last_fetch_seqnum(gfmdc_peer);
			to_sn = db_journal_get_current_seqnum();
			if (lf_sn < to_sn) {
				*transferringp = 1;
				switch (journal_transfer_wait_verbose) {
				case verbose_silent:
					/* nothing to do */
					break;
				case verbose_wait:
					gflog_info(GFARM_MSG_1004229,
					    "journal transfer: waiting for %s "
					    "till seqnum %llu, currently %llu",
					    mdhost_get_name(mh),
					    (unsigned long long)to_sn,
					    (unsigned long long)lf_sn);
					break;
				case verbose_timeout:
					gflog_info(GFARM_MSG_1004230,
					    "journal transfer: "
					    "timeout transferring to %s: "
					    "target seqnum %llu, sent %llu",
					    mdhost_get_name(mh),
					    (unsigned long long)to_sn,
					    (unsigned long long)lf_sn);
					break;
				}
			}
		}
		mdhost_put_peer(mh, peer); /* decrement refcount */
	}
	return (1); /* continue this mdhost_foreach() loop */

}

static int
gfmdc_journal_transferring(void)
{
	int transferring = 0;

	mdhost_foreach(gfmdc_journal_transferring_each_mdhost, &transferring);
	return (transferring);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
gfmdc_journal_transfer_wait(void)
{
	struct timespec timeout;
	int retry;
	const char diag[] = "gfmdc_journal_transfer_wait";

	journal_transfer_wait_verbose = verbose_wait;
	if (!gfmdc_journal_transferring()) {
		gflog_info(GFARM_MSG_1004231,
		    "journal transfer: no need to wait");
		return;
	}
	journal_transfer_wait_verbose = verbose_silent;

	gfarm_mutex_lock(&journal_transfer_mutex,
	    diag, JOURNAL_TRANSFER_MUTEX_DIAG);
	journal_transfer_waiting = 1;
	gfarm_mutex_unlock(&journal_transfer_mutex,
	    diag, JOURNAL_TRANSFER_MUTEX_DIAG);

	gfarm_gettime(&timeout);
	timeout.tv_sec += gfarm_get_metadb_server_slave_replication_timeout();

	for (;;) {
		gfarm_mutex_lock(&journal_transfer_mutex,
		    diag, JOURNAL_TRANSFER_MUTEX_DIAG);
		retry = gfarm_cond_timedwait(
		    &journal_transfer_event, &journal_transfer_mutex, &timeout,
		    diag, JOURNAL_TRANSFER_EVENT_DIAG);
		gfarm_mutex_unlock(&journal_transfer_mutex,
		    diag, JOURNAL_TRANSFER_MUTEX_DIAG);
		if (!retry || !gfmdc_journal_transferring())
			break;
	}
	journal_transfer_wait_verbose = verbose_timeout;
	if (gfmdc_journal_transferring())
		gflog_notice(GFARM_MSG_1004232,
		    "journal transfer: timeout due to no response "
		    "within %d seconds "
		    "(metadb_server_slave_replication_timeout)",
		     gfarm_get_metadb_server_slave_replication_timeout());
	else
		gflog_info(GFARM_MSG_1004233, "journal transfer: completed");
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
	mdhost_set_switch_to_sync_hook(gfmdc_peer_switch_to_sync);

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

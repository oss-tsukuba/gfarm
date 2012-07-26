#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "thrsubr.h"
#include "queue.h"

#include "auth.h" /* for peer.h */
#include "gfp_xdr.h"

#include "subr.h"
#include "rpcsubr.h"
#include "abstract_host.h"
#include "mdhost.h"
#include "relay.h"
#include "local_peer.h"
#include "remote_peer.h"
#include "gfm_proto.h"
#include "gfmd_channel.h"
#include "mdhost.h"
#include "db_journal.h"
#include "peer.h"

static const char RELAYED_REQUEST_ACQUIRE_MUTEX[] =
	"relayed_request.acquire_mutex";
static const char RELAYED_REQUEST_ACQUIRE_COND[] =
	"relayed_request.acquire_cond";
static const char RELAYED_REQUEST_RESULT_MUTEX[] =
	"relayed_request.result_mutex";
static const char RELAYED_REQUEST_RESULT_COND[] =
	"relayed_request.result_cond";
static const char RELAY_DB_UPDATE_MUTEX_DIAG[]	= "relay.db_update_mutex";
static const char RELAY_DB_UPDATE_COND_DIAG[]	= "relay.db_update_cond";

struct relayed_request {
	pthread_mutex_t acquire_mutex;
	pthread_cond_t acquire_cond;
	pthread_mutex_t result_mutex;
	pthread_cond_t result_cond;

	gfarm_error_t error;
	/* command (protocol number) */
	gfarm_int32_t command;
	/* diag of command */
	const char *diag;
	/* gfmd channel peer */
	struct peer *mhpeer;
	/* if client peer thread is acquired reply or not */
	int acquired;
	/* if async protocol thread received result from gfmd channel */
	int resulted;
	/* gfmd channel connection */
	struct gfp_xdr *conn;
	/* recv size from gfmd channel */
	size_t rsize;
	/* gfmd channel host (master gfmd) */
	struct mdhost *mdhost;
	/* context in gfm_server_relay_request_reply() */
	enum request_reply_mode mode;
};

struct db_update_info {
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;
	GFARM_STAILQ_ENTRY(db_update_info) next;
};

static pthread_mutex_t	db_update_mutex;
static pthread_cond_t	db_update_cond;
static gfarm_uint64_t	last_db_update_seqnum;

static GFARM_STAILQ_HEAD(db_update_infoq, db_update_info) db_update_infoq
	= GFARM_STAILQ_HEAD_INITIALIZER(db_update_infoq);

static struct relayed_request *
relayed_request_new(gfarm_int32_t command, const char *diag)
{
	struct relayed_request *r;

	GFARM_MALLOC(r);
	if (r == NULL)
		return (NULL);
	gfarm_mutex_init(&r->acquire_mutex, diag,
	    RELAYED_REQUEST_ACQUIRE_MUTEX);
	gfarm_cond_init(&r->acquire_cond, diag, RELAYED_REQUEST_ACQUIRE_COND);
	gfarm_mutex_init(&r->result_mutex, diag, RELAYED_REQUEST_RESULT_MUTEX);
	gfarm_cond_init(&r->result_cond, diag, RELAYED_REQUEST_RESULT_COND);
	r->mhpeer = NULL;
	r->error = GFARM_ERR_NO_ERROR;
	r->command = command;
	r->diag = diag;
	r->acquired = 0;
	r->resulted = 0;
	r->conn = NULL;
	r->rsize = 0;
	r->mdhost = NULL;
	r->mode = RELAY_NOT_SET;
	return (r);
}

static void
relayed_request_acquire_wait(struct relayed_request *r)
{
	gfarm_mutex_lock(&r->acquire_mutex, r->diag,
	    RELAYED_REQUEST_ACQUIRE_MUTEX);
	while (!r->acquired) {
		gfarm_cond_wait(&r->acquire_cond, &r->acquire_mutex, r->diag,
		    RELAYED_REQUEST_ACQUIRE_COND);
	}
	gfarm_mutex_unlock(&r->acquire_mutex, r->diag,
	    RELAYED_REQUEST_ACQUIRE_MUTEX);
}

static void
relayed_request_acquire_notify(struct relayed_request *r)
{
	gfarm_mutex_lock(&r->acquire_mutex, r->diag,
	    RELAYED_REQUEST_ACQUIRE_MUTEX);
	r->acquired = 1;
	gfarm_cond_signal(&r->acquire_cond, r->diag,
	    RELAYED_REQUEST_ACQUIRE_COND);
	gfarm_mutex_unlock(&r->acquire_mutex, r->diag,
	    RELAYED_REQUEST_ACQUIRE_MUTEX);
}

static void
relayed_request_result_wait(struct relayed_request *r)
{
	gfarm_mutex_lock(&r->result_mutex, r->diag,
	    RELAYED_REQUEST_RESULT_MUTEX);
	while (!r->resulted) {
		gfarm_cond_wait(&r->result_cond, &r->result_mutex, r->diag,
		    RELAYED_REQUEST_RESULT_COND);
	}
	gfarm_mutex_unlock(&r->result_mutex, r->diag,
	    RELAYED_REQUEST_RESULT_MUTEX);
}

static void
relayed_request_result_notify(struct relayed_request *r)
{
	gfarm_mutex_lock(&r->result_mutex, r->diag,
	    RELAYED_REQUEST_RESULT_MUTEX);
	r->resulted = 1;
	gfarm_cond_signal(&r->result_cond, r->diag,
	    RELAYED_REQUEST_RESULT_COND);
	gfarm_mutex_unlock(&r->result_mutex, r->diag,
	    RELAYED_REQUEST_RESULT_MUTEX);
}

static void
master_get_db_update_info(struct peer *peer, gfarm_uint64_t *seqnump,
	gfarm_uint64_t *flagsp)
{
	struct remote_peer *rpeer;

	assert(mdhost_self_is_master());
	assert(peer_get_parent(peer) != NULL);

	rpeer = peer_to_remote_peer(peer);
	*seqnump = remote_peer_get_db_update_seqnum(rpeer);
	*flagsp = remote_peer_get_db_update_flags(rpeer);
}

/*
 * call this function at updating meta data.
 * flags will be merged to the value previously set.
 */
void
master_set_db_update_info_to_peer(struct peer *peer, gfarm_uint64_t flags)
{
	struct remote_peer *rpeer;

	if (!mdhost_self_is_master() || peer_get_parent(peer) == NULL)
		return;
	rpeer = peer_to_remote_peer(peer);
	remote_peer_set_db_update_seqnum(rpeer,
	    db_journal_get_current_seqnum());
	remote_peer_merge_db_update_flags(rpeer, flags);
}

static gfarm_error_t
slave_add_db_update_info(gfarm_uint64_t seqnum, gfarm_uint64_t flags,
	const char *diag)
{
	struct db_update_info *di;

	if (db_journal_get_current_seqnum() >= seqnum)
		return (GFARM_ERR_NO_ERROR);

	gfarm_mutex_lock(&db_update_mutex, diag, RELAY_DB_UPDATE_MUTEX_DIAG);
	if (last_db_update_seqnum != seqnum) {
		GFARM_MALLOC(di);
		if (di == NULL) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		} else {
			di->seqnum = seqnum;
			di->flags = flags;
			last_db_update_seqnum = seqnum;
			GFARM_STAILQ_INSERT_TAIL(&db_update_infoq, di, next);
		}
	}
	gfarm_mutex_unlock(&db_update_mutex, diag, RELAY_DB_UPDATE_MUTEX_DIAG);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
slave_add_initial_db_update_info(gfarm_uint64_t seqnum, const char *diag)
{
	return (slave_add_db_update_info(seqnum, DBUPDATE_ALL, diag));
}

static void
slave_remove_db_update_info(gfarm_uint64_t seqnum, const char *diag)
{
	struct db_update_info *di, *tdi;
	int removed = 0;

	/*
	 * remove db_update_infos which have less or equal seqnum than
	 * the applied journal seqnum in slave.
	 */
	gfarm_mutex_lock(&db_update_mutex, diag, RELAY_DB_UPDATE_MUTEX_DIAG);
	GFARM_STAILQ_FOREACH_SAFE(di, &db_update_infoq, next, tdi) {
		if (di->seqnum > seqnum)
			break;
		GFARM_STAILQ_REMOVE_HEAD(&db_update_infoq, next);
		free(di);
		removed = 1;
	}
	if (removed)
		gfarm_cond_broadcast(&db_update_cond, diag,
		    RELAY_DB_UPDATE_COND_DIAG);
	gfarm_mutex_unlock(&db_update_mutex, diag, RELAY_DB_UPDATE_MUTEX_DIAG);
}

void
slave_clear_db_update_info(void)
{
	slave_remove_db_update_info(GFARM_UINT64_MAX,
	    "slave_clear_db_update_info");
}

/* needs db_update_mutex to be locked */
static int
slave_needs_to_wait(gfarm_uint64_t wait_db_update_flags, const char *diag)
{
	int need = 0;
	struct db_update_info *di;

	GFARM_STAILQ_FOREACH(di, &db_update_infoq, next) {
		if ((di->flags & wait_db_update_flags) != 0) {
			need = 1;
			break;
		}
	}

	return (need);
}

gfarm_error_t
wait_db_update_info(struct peer *peer,
	gfarm_uint64_t wait_db_update_flags, const char *diag)
{
#define DB_UPDATE_INFO_SLEEP_INTERVAL	30
#define DB_UPDATE_INFO_TIMEOUT		300
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	struct mdhost *mh, *mhself;
	struct timeval timeout;
	struct timespec ts;
	int needs_to_wait, mhup;

	assert(wait_db_update_flags);

	/* DBUPDATE_NOWAIT means no relay but no wait */
	if (mdhost_self_is_master() || wait_db_update_flags == DBUPDATE_NOWAIT)
		return (GFARM_ERR_NO_ERROR);

	gfarm_mutex_lock(&db_update_mutex, diag,
		RELAY_DB_UPDATE_MUTEX_DIAG);
	needs_to_wait = slave_needs_to_wait(wait_db_update_flags, diag);
	gfarm_mutex_unlock(&db_update_mutex, diag,
		RELAY_DB_UPDATE_MUTEX_DIAG);

	if (!needs_to_wait)
		return (GFARM_ERR_NO_ERROR);

	gettimeofday(&timeout, NULL);
	timeout.tv_sec += DB_UPDATE_INFO_TIMEOUT;
	ts.tv_sec = DB_UPDATE_INFO_SLEEP_INTERVAL;
	ts.tv_nsec = 0;
	mhself = mdhost_lookup_self();
	needs_to_wait = 1;

	for (;;) {
		mh = mdhost_lookup_master();
		if (mhself == mh)
			/* self is transformed to master */
			break;
		mhup = mdhost_is_up(mh);

		gfarm_mutex_lock(&db_update_mutex, diag,
			RELAY_DB_UPDATE_MUTEX_DIAG);
		if (mhup)
			needs_to_wait = slave_needs_to_wait(
			    wait_db_update_flags, diag);
		if (!mhup || needs_to_wait)
			gfarm_cond_timedwait(&db_update_cond, &db_update_mutex,
			    &ts, diag, RELAY_DB_UPDATE_COND_DIAG);
		gfarm_mutex_unlock(&db_update_mutex, diag,
			RELAY_DB_UPDATE_MUTEX_DIAG);
		if (!needs_to_wait && mhup)
			break;
		if (gfarm_timeval_is_expired(&timeout)) {
			e = GFARM_ERR_CONNECTION_ABORTED;
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s", diag, gfarm_error_string(e));
			break;
		}
	}

	return (e);
}

static gfarm_error_t
slave_request_relay_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct relayed_request *r = arg;

	r->mhpeer = peer;
	r->conn = peer_get_conn(peer);
	r->mdhost = peer_get_mdhost(peer);
	r->rsize = size;

	relayed_request_result_notify(r);
	relayed_request_acquire_wait(r);

	free(r);

	return (GFARM_ERR_NO_ERROR);
}

static void
slave_request_relay_disconnect(void *p, void *arg)
{
	gfarm_error_t e;
	struct relayed_request *r = arg;

	/* FIXME better to set the reason of disconnection */
	e = GFARM_ERR_CONNECTION_ABORTED;
	gflog_error(GFARM_MSG_UNFIXED, "%s: %s",
	    r->diag, gfarm_error_string(e));

	r->error = e;

	relayed_request_result_notify(r);
	relayed_request_acquire_wait(r);

	free(r);
}

static gfarm_error_t
slave_request_relay0(struct relayed_request *r, gfarm_int32_t command,
	const char *format, va_list *app, const char *wformat, ...)
{
	gfarm_error_t e;
	va_list wap;
	struct mdhost *mh = mdhost_lookup_master();

	va_start(wap, wformat);
	e = async_client_vsend_wrapped_request(
	    mdhost_to_abstract_host(mh), mdhost_get_peer(mh), r->diag,
	    slave_request_relay_result,
	    slave_request_relay_disconnect, r,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    wformat, &wap, command, format, app, 1);
	va_end(wap);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(mh), gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
slave_request_relay(struct relayed_request *r, struct peer *peer,
	gfarm_int32_t command, const char *format, va_list *app)
{
	gfarm_error_t e;

	/*
	 * use wrapping format to insert GFM_PROTO_REMOTE_RPC as
	 * primary request.
	 *
	 * packet layout:
	 * |xid|size|request=GFM_PROTO_REMOTE_RPC|peer_id|command|arg...|
	 */
	e = slave_request_relay0(r, command, format, app,
	    "il", GFM_PROTO_REMOTE_RPC, peer_get_id(peer));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
slave_reply_relay(struct relayed_request *r, const char *format, va_list *app,
	const char *wformat, ...)
{
	gfarm_error_t e, ee;
	va_list wap;

	assert(r->error == GFARM_ERR_NO_ERROR);

	va_start(wap, wformat);
	e = async_client_vrecv_wrapped_result(r->mhpeer,
	    mdhost_to_abstract_host(r->mdhost), r->rsize, r->diag, &ee,
	    wformat, &wap, &format, app);
	va_end(wap);
	if (e == GFARM_ERR_NO_ERROR)
		e = ee;
	if (e != GFARM_ERR_NO_ERROR)
		/* possibly expected error */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
ensure_remote_peer(struct peer *peer, gfarm_int32_t command,
	const char *diag, struct relayed_request **rp)
{
	gfarm_error_t e;

	*rp = relayed_request_new(command, diag);
	if (*rp == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	if (!local_peer_get_remote_peer_allocated(
	    peer_to_local_peer(peer)) &&
	    (e = gfmdc_client_remote_peer_alloc(peer))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		(*rp)->error = e;
		(*rp)->resulted = 1;
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

/* returns *reqp != NULL, if !mdhost_self_is_master() case. */
static gfarm_error_t
gfm_server_relay_get_vrequest(struct peer *peer, size_t *sizep,
	int skip, struct relayed_request **rp, const char *diag,
	gfarm_int32_t command, gfarm_uint64_t wait_db_update_flags,
	const char *format,
	va_list *app)
{
	va_list ap;
	gfarm_error_t e;

	va_copy(ap, *app);
	e = gfm_server_get_vrequest(peer, sizep, diag, format, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}

	if (wait_db_update_flags || mdhost_self_is_master()) {
		*rp = NULL;
		if (wait_db_update_flags &&
		    (e = wait_db_update_info(peer, wait_db_update_flags, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(e));
		return (e);
	}

	if ((e = ensure_remote_peer(peer, command, diag, rp))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	va_copy(ap, *app);
	e = slave_request_relay(*rp, peer, command, format, &ap);
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	/*
	 * rp will be freed asynchronously in
	 * slave_request_relay_result() or
	 * slave_request_relay_disconnect()
	 * after relayed_request_acquire_notify() is called.
	 */

	return (e);
}

gfarm_error_t
gfm_server_relay_get_request(struct peer *peer, size_t *sizep,
	int skip, struct relayed_request **rp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_relay_get_vrequest(peer, sizep, skip, rp, diag,
	    command, 0, format, &ap);
	va_end(ap);
	return (e);
}

/* for multiple-argument request */
gfarm_error_t
gfm_server_relay_get_request_dynarg(struct peer *peer, size_t *sizep,
	int skip, struct relayed_request *r, const char *diag,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	size_t sz = SIZE_MAX;

	va_start(ap, format);
	if (r == NULL || r->mode == RELAY_CALC_SIZE) {
		/*
		 * 1. in master, non-relayed request from client
		 * 2. in master, relayed request from slave
		 * 3. in slave, non-relayed request from client
		 * 4. in slave, relayed request from client called from
		 *    gfm_server_relay_request_reply() with
		 *    calculating size context.
		 */
		e = gfm_server_vrecv(peer, &sz, diag, format, &ap);
		*sizep += (SIZE_MAX - sz);
	} else {
		/*
		 * in slave, send request from client to master called from
		 * fm_server_relay_request_reply() with
		 * transfering context.
		 */
		assert(peer == NULL);
		assert(r->conn != NULL);
		e = gfp_xdr_vsend_ref(r->conn, &format, &ap);
	}
	va_end(ap);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	}
	return (e);
}

/* for multiple-argument request */
gfarm_error_t
gfm_server_relay_put_reply_dynarg(struct peer *peer, size_t *sizep,
    const char *diag, gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	struct gfp_xdr *conn = peer_get_conn(peer);
	static const char relay_diag[] = "relay_put_reply_dynarg";

	if (sizep == NULL) {
		/*
		 * 1. in master, non-relayed reply to client
		 * 2. in master, relayed reply to slave
		 * 3. in slave, relayed reply to client
		 */
		e = gfp_xdr_send(conn, "i", ecode);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: %s (gfp_xdr_send ecode): %s",
			    diag, relay_diag, gfarm_error_string(e));
		} else if (ecode == GFARM_ERR_NO_ERROR) {
			va_start(ap, format);
			e = gfp_xdr_vsend(conn, &format, &ap);
			va_end(ap);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "%s: %s (gfp_xdr_vsend): %s",
				    diag, relay_diag, gfarm_error_string(e));
			}
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = ecode;
	} else {
		/*
		 * in master relayed reply with calculating size context
		 */
		e = gfp_xdr_send_size_add(sizep, "i", ecode);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: %s (gfp_xdr_send_size_add ecode): %s",
			    diag, relay_diag, gfarm_error_string(e));
		} else if (ecode == GFARM_ERR_NO_ERROR) {
			va_start(ap, format);
			e = gfp_xdr_vsend_size_add(sizep, &format, &ap);
			va_end(ap);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "%s: %s (gfp_xdr_vsend_size_add): %s",
				    diag, relay_diag, gfarm_error_string(e));
			}
		}
	}

	return (e);
}

gfarm_error_t
gfm_server_relay_put_reply_arg_dynarg(struct peer *peer, size_t *sizep,
    const char *diag, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	static const char relay_diag[] = "relay_put_reply_arg_dynarg";

	va_start(ap, format);
	if (sizep == NULL) {
		/*
		 * 1. in master, non-relayed reply to client
		 * 2. in master, relayed reply to slave
		 * 3. in slave, relayed reply to client
		 */
		e = gfp_xdr_vsend(peer_get_conn(peer), &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: %s (gfp_xdr_vsend): %s",
			    diag, relay_diag, gfarm_error_string(e));
		}
	} else {
		/* in master relayed reply with calculating size context */
		e = gfp_xdr_vsend_size_add(sizep, &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: %s (gfp_xdr_vsend_size_add): %s",
			    diag, relay_diag, gfarm_error_string(e));
		}
	}
	va_end(ap);
	return (e);
}

static gfarm_error_t
gfm_server_reply_with_vrelay(struct peer *peer, gfp_xdr_xid_t xid,
	int skip, put_reply_op_t put_reply_op,
	gfarm_int32_t command, gfarm_uint64_t wait_db_update_flags,
	void *closure, const char *diag)
{
	gfarm_error_t e;
	size_t size = 0;
	struct gfp_xdr *conn = peer_get_conn(peer);
	int relay_reply = peer_get_async(peer) != NULL &&
		mdhost_self_is_master();
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;

	if (!relay_reply && wait_db_update_flags &&
	    (e = wait_db_update_info(peer, wait_db_update_flags, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		e = gfm_server_put_reply(peer, xid, xid != 0 ? &size : NULL,
		    diag, e, "");
	} else if ((e = put_reply_op(relay_reply ? RELAY_CALC_SIZE : NO_RELAY,
	    peer, relay_reply ? &size : NULL, skip, closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}

	if (!relay_reply) {
		gfp_xdr_flush(conn);
		return (e);
	}

	/*
	 * in master, reply to slave via gfmd channel
	 */

	if ((e = gfp_xdr_send_size_add(&size, "ll", 0, 0))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	if ((e = gfp_xdr_send_async_result_header(conn, xid, size))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	master_get_db_update_info(peer, &seqnum, &flags);

	if ((e = gfp_xdr_send(conn, "ll", seqnum, flags))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	if ((e = put_reply_op(RELAY_TRANSFER, peer, NULL, skip, closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	gfp_xdr_flush(conn);

unlock_sender:
	if (peer_get_parent(peer) != NULL)
		peer = peer_get_parent(peer);
	async_client_sender_unlock(mdhost_to_abstract_host(
	    peer_get_mdhost(peer)), peer, diag);

	return (e);
}

/* for multiple-argument request */
static gfarm_error_t
gfm_server_relay_request_reply0(struct peer *peer, gfp_xdr_xid_t xid,
	int skip, get_request_op_t get_request_op, put_reply_op_t put_reply_op,
	gfarm_int32_t command, gfarm_uint64_t wait_db_update_flags,
	void *closure, const char *diag)
{
	gfarm_error_t e;
	int eof, relay, xid_allocated = 0, db_update_info_received = 0;
	char *buf = NULL;
	size_t rsz, size = 0;
	gfarm_uint64_t seqnum, flags;
	struct abstract_host *ah;
	struct peer *mhpeer;
	gfp_xdr_async_peer_t async_server = NULL;
	struct relayed_request *r = NULL;
	static const char diag0[] = "gfm_server_relay_request_reply";

	/*
	 * get request from client
	 */

	gfm_server_start_get_request(peer, diag);
	relay = wait_db_update_flags == 0 && !mdhost_self_is_master();

	if ((e = get_request_op(relay ? RELAY_CALC_SIZE : NO_RELAY,
	    peer, &size, skip, NULL, closure, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}

	if (!relay) {
		if ((e = gfm_server_reply_with_vrelay(peer, xid, skip,
		    put_reply_op, command, wait_db_update_flags, closure, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_ERR_NO_ERROR,
			    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}

	/*
	 * protocol relay to master gfmd
	 */
	ah = mdhost_to_abstract_host(mdhost_lookup_master());
	mhpeer = abstract_host_get_peer(ah, diag);

	if ((e = async_client_sender_lock(ah, mhpeer, NULL, command,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		goto end;
	}

	/*
	 * r will be freed asynchronously in
	 * slave_request_relay_result() or
	 * slave_request_relay_disconnect()
	 * after relayed_request_acquire_notify() is called.
	 */
	if ((e = ensure_remote_peer(peer, command, diag, &r))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	r->mode = RELAY_CALC_SIZE;
	r->conn = peer_get_conn(mhpeer);
	async_server = peer_get_async(mhpeer);

	/* see slave_request_relay() for packet layout detail */
	if ((e = gfp_xdr_send_size_add(&size, "ili", 0, 0, 0))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	r->mode = RELAY_TRANSFER;

	if ((e = gfp_xdr_send_async_request_header(r->conn, async_server, size,
	    slave_request_relay_result, slave_request_relay_disconnect, r,
	    &xid)) != GFARM_ERR_NO_ERROR) {
		goto unlock_sender;
	}

	xid_allocated = 1;
	if ((e = gfp_xdr_send(r->conn, "ili", GFM_PROTO_REMOTE_RPC,
	    peer_get_id(peer), command)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	if ((e = get_request_op(RELAY_TRANSFER, NULL, &size, skip, r, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	if ((e = gfp_xdr_flush(r->conn)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto unlock_sender;
	}

unlock_sender:
	async_client_sender_unlock(ah, mhpeer, diag);

	if (e != GFARM_ERR_NO_ERROR)
		goto acquire_notify;

	/*
	 * NOTE: layering violation
	 * Slave-gfmd is peeking `size' field of the protocol header of here.
	 * And transfering protocol data from the mater-gfmd to a client,
	 * by just using that `size' information.
	 */
	relayed_request_result_wait(r);
	e = r->error;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		/* r is already freed */
		goto end;
	}

	rsz = SIZE_MAX;
	if ((e = gfp_xdr_recv_sized(r->conn, 1, &rsz, &eof, "ll", &seqnum,
	    &flags)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto acquire_notify;
	}

	db_update_info_received = 1;
	r->rsize -= (SIZE_MAX - rsz);

	GFARM_MALLOC_ARRAY(buf, r->rsize);
	if (buf == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto acquire_notify;
	}
	if ((e = gfp_xdr_recv(r->conn, 1, &eof, "r", r->rsize, &rsz, buf))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
	}

acquire_notify:

	if (r != NULL)
		relayed_request_acquire_notify(r);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;

	if ((e = gfp_xdr_send(peer_get_conn(peer), "r", r->rsize, buf))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
		goto end;
	}
	if ((e = gfp_xdr_flush(peer_get_conn(peer)))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_ERR_NO_ERROR,
		    "%s: %s", diag, gfarm_error_string(e));
	}

end:
	free(buf);
	if (e != GFARM_ERR_NO_ERROR) {
		if (xid_allocated) {
			assert(async_server != NULL);
			gfp_xdr_send_async_request_error(async_server, xid,
			    diag0);
		} else
			free(r);
	}

	if (db_update_info_received) {
		if ((e = slave_add_db_update_info(seqnum, flags, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_ERR_NO_ERROR,
			    "%s: %s", diag, gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfm_server_relay_request_reply(struct peer *peer, gfp_xdr_xid_t xid,
	int skip, get_request_op_t get_request_op, put_reply_op_t put_reply_op,
	gfarm_int32_t command, void *closure, const char *diag)
{
	return (gfm_server_relay_request_reply0(peer, xid,
		skip, get_request_op, put_reply_op, command, 0, closure, diag));
}

gfarm_error_t
gfm_server_relay_put_reply0(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, xdr_vsend_t xdr_vsend, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app,
	const char *wformat, ...)
{
	gfarm_error_t e;
	va_list wap;

	va_start(wap, wformat);
	e = gfm_server_put_wrapped_vreply(peer, xid, sizep, gfp_xdr_vsend_ref,
	    diag, ecode, wformat, &wap, format, app);
	va_end(wap);
	return (e);
}

gfarm_error_t
gfm_server_relay_put_reply(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, struct relayed_request *r, const char *diag,
	gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;

	if (r != NULL) {
		/* in slave, receive reply from master */
		relayed_request_result_wait(r);
		e = r->error;

		if (e == GFARM_ERR_NO_ERROR) {
			va_start(ap, format);
			if ((e = slave_reply_relay(r, format, &ap, "ll",
			    &seqnum, &flags)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
				    "%s", gfarm_error_string(e));
			}
			va_end(ap);
			relayed_request_acquire_notify(r);
			if ((e = slave_add_db_update_info(seqnum, flags, diag))
			    != GFARM_ERR_NO_ERROR)
				gflog_debug(GFARM_ERR_NO_ERROR,
				    "%s: %s", diag, gfarm_error_string(e));
		} else {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s", r->diag, gfarm_error_string(e));
		}
		if (ecode == GFARM_ERR_NO_ERROR)
			ecode = e;
	}

	va_start(ap, format);
	if (peer_get_parent(peer) == NULL)
		/*
		 * 1. in master, non-relayed reply to client
		 * 2. in slave, relayed/non-relayed reply to client
		 */
		e = gfm_server_put_vreply(peer, xid, sizep, gfp_xdr_vsend_ref,
		    diag, ecode, format, &ap);
	else {
		/* in master, relayed reply to slave */
		master_get_db_update_info(peer, &seqnum, &flags);
		e = gfm_server_relay_put_reply0(peer, xid, sizep,
		    gfp_xdr_vsend_ref, diag, ecode, format, &ap, "ll", seqnum,
		    flags);
	}
	va_end(ap);

	if (e == GFARM_ERR_NO_ERROR) 
		e = ecode;
	else {
		/* possibly expected error */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
	}

	return (e);
}

void
relay_init(void)
{
	static const char diag[] = "relay_init";

	gfarm_mutex_init(&db_update_mutex, diag, RELAY_DB_UPDATE_MUTEX_DIAG);
	gfarm_cond_init(&db_update_cond, diag, RELAY_DB_UPDATE_COND_DIAG);
	db_journal_set_remove_db_update_info_op(slave_remove_db_update_info);
}

/**
 * @file  relay.c
 * @brief RPC relay among a slave and a master gfmd servers.
 */

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
#include "thrstatewait.h"

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
	struct remote_peer *remote_peer;

	assert(mdhost_self_is_master());
	assert(peer_get_parent(peer) != NULL);

	remote_peer = peer_to_remote_peer(peer);
	*seqnump = remote_peer_get_db_update_seqnum(remote_peer);
	*flagsp = remote_peer_get_db_update_flags(remote_peer);
}

/*
 * call this function at updating meta data.
 * flags will be merged to the value previously set.
 */
void
master_set_db_update_info_to_peer(struct peer *peer, gfarm_uint64_t flags)
{
	struct remote_peer *remote_peer;

	if (!mdhost_self_is_master() || peer_get_parent(peer) == NULL)
		return;
	remote_peer = peer_to_remote_peer(peer);
	remote_peer_set_db_update_seqnum(remote_peer,
	    db_journal_get_current_seqnum());
	remote_peer_merge_db_update_flags(remote_peer, flags);
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
			    "%s: wait_db_update_info() - timeout: %s",
			    diag, gfarm_error_string(e));
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
slave_request_relay0(struct mdhost *master_mh,
	struct relayed_request *r, gfarm_int32_t command,
	const char *format, va_list *app, int isref, const char *wformat, ...)
{
	gfarm_error_t e;
	va_list wap;

	va_start(wap, wformat);
	e = async_client_vsend_wrapped_request_unlocked(
	    mdhost_to_abstract_host(master_mh), r->diag,
	    slave_request_relay_result,
	    slave_request_relay_disconnect, r,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    wformat, &wap, command, format, app, isref);
	va_end(wap);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "send wrapped request to %s: %s",
		    mdhost_get_name(master_mh), gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
slave_request_relay(struct mdhost *master_mh,
	struct relayed_request *r, struct peer *peer,
	gfarm_int32_t command, const char *format, va_list *app, int isref)
{
	gfarm_error_t e;

	/*
	 * use wrapping format to insert GFM_PROTO_REMOTE_RPC as
	 * primary request.
	 *
	 * packet layout:
	 * |xid|size|request=GFM_PROTO_REMOTE_RPC|peer_id|command|arg...|
	 */
	e = slave_request_relay0(master_mh, r, command, format, app, isref,
	    "il", GFM_PROTO_REMOTE_RPC, peer_get_private_peer_id(peer));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
slave_reply_relay(struct relayed_request *r, const char *format, va_list *app,
	const char *wformat, ...)
{
	gfarm_error_t e, ee, we;
	va_list wap;

	assert(r->error == GFARM_ERR_NO_ERROR);

	va_start(wap, wformat);
	e = async_client_vrecv_wrapped_result(r->mhpeer,
	    mdhost_to_abstract_host(r->mdhost), r->rsize, r->diag,
	    &we, wformat, &wap, &ee, &format, app);
	va_end(wap);
	if (e == GFARM_ERR_NO_ERROR) {
		if (we != GFARM_ERR_NO_ERROR)
			e = we;
		else
			e = ee;
	}
	if (e != GFARM_ERR_NO_ERROR)
		/* possibly expected error */
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	return (e);
}

static void
gfm_server_start_get_request(struct peer *peer, const char *diag)
{
	if (debug_mode)
		gflog_info(GFARM_MSG_1000225, "<%s> start receiving", diag);
}

/* sizep != NULL, if this is an inter-gfmd-relayed request. */
static gfarm_error_t
gfm_server_vrecv(struct peer *peer, size_t *sizep,
	const char *diag, const char *format, va_list *app)
{
	gfarm_error_t e;
	int eof;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (sizep != NULL)
		e = gfp_xdr_vrecv_sized(client, 0, 1, sizep,
		    &eof, &format, app);
	else
		e = gfp_xdr_vrecv(client, 0, 1, &eof, &format, app);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000226,
		    "%s receiving request: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (eof) {
		gflog_warning(GFARM_MSG_1000227,
		    "%s receiving request: missing RPC argument", diag);
		peer_record_protocol_error(peer);
		return (GFARM_ERR_PROTOCOL);
	}
	if (*format != '\0')
		gflog_fatal(GFARM_MSG_1000228,
		    "%s receiving request: invalid format character",
		    diag);
	return (GFARM_ERR_NO_ERROR);
}

/* sizep != NULL, if this is an inter-gfmd-relayed request. */
static gfarm_error_t
gfm_server_get_vrequest(struct peer *peer, size_t *sizep,
	const char *diag, const char *format, va_list *app)
{
	gfarm_error_t e;
	struct gfarm_thr_statewait *statewait;
	struct remote_peer *remote_peer;

	gfm_server_start_get_request(peer, diag);
	e = gfm_server_vrecv(peer, sizep, diag, format, app);

	if (mdhost_self_is_master() && peer_get_async(peer) != NULL) {
		/*
		 * This gfmd is master and the request comes from a slave.
		 */
		remote_peer = peer_to_remote_peer(peer);
		statewait = remote_peer_get_statewait(remote_peer);
		gfarm_thr_statewait_signal(statewait, e, diag);
	}

	return (e);
}

gfarm_error_t
gfm_server_get_request(struct peer *peer, size_t *sizep,
	const char *diag, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_get_vrequest(peer, sizep, diag, format, &ap);
	va_end(ap);
	return (e);
}

/* only slave gfmd is allowed to call this function */
gfarm_error_t
gfm_server_relay_put_vrequest(struct peer *peer,
	struct relayed_request **rp, const char *diag,
	gfarm_int32_t command, const char *format, va_list *app, int isref)
{
	gfarm_error_t e;
	struct relayed_request *r;
	struct peer *mhpeer = NULL;

	r = relayed_request_new(command, diag);
	if (r == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED, "%s: relayed_request_new: %s",
		    diag, gfarm_error_string(e));
		return (e);
	}

	if ((e = gfmdc_ensure_remote_peer(peer, &mhpeer))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: gfmdc_ensure_remote_peer: %s",
		    diag, gfarm_error_string(e));
		free(r);
		return (e);
	}
	e = slave_request_relay(peer_get_mdhost(mhpeer), r, peer, command,
	    format, app, isref);
	gfmdc_ensure_remote_peer_end(mhpeer);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: slave_request_relay(): %s",
		    diag, gfarm_error_string(e));
		free(r);
		return (e);
	}

	/*
	 * *rp will be freed asynchronously in
	 * slave_request_relay_result() or
	 * slave_request_relay_disconnect()
	 * after relayed_request_acquire_notify() is called.
	 */
	*rp = r;
	return (e);
}

/*
 * the style of variable arguments is gfp_xdr_vsend_ref.
 *
 * NOTE: only slave gfmd is allowed to call this function.
 */
gfarm_error_t
gfm_server_relay_put_request(struct peer *peer,
	struct relayed_request **rp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_relay_put_vrequest(peer, rp, diag,
	    command, format, &ap, 0);
	va_end(ap);

	return (e);
}

/**
 * Get a request from a client or a slave gfmd.
 *
 * @param peer     Connection peer.
 * @param sizep    Data size of the request.
 * @param skip     True if the transaction is suppressed.
 * @param rp       RPC relay handler.
 * @param diag     Title for diagnostics messages.
 * @param command  ID of the gfm protocol command.
 * @param format   Format of variable arguments of the gfm protocol command.
 * @param vararg.. gfp_xdr_vrecv/gfp_xdr_vsend_ref style variable arguments
 * @return         Error code.
 *
 * The function is similar to gfm_server_get_request(), but it supports
 * RPC relay among master and slave gfmd servers.
 *
 * This function can be used in a handler of a gfm protocol command which
 * receives the fixed number of arguments and sends the fixed number of
 * response data.  For example, response data of the gfm command
 * GFM_PROTO_GETDIRENTS are:
 *
 * \verbatim
 *    i:ecode
 *    also reply the followings if ecode == GFARM_ERR_NO_ERROR:
 *        i:n_entries
 *        s[n_entries]:ent_names
 *        i[n_entries]:ent_types
 *        l[n_entries]:inode_numbers
 * \endverbatim
 *
 * Since 'ent_names', 'ent_types' and 'inode_numbers' are arrays with
 * variable length, GFM_PROTO_GETDIRENTS returns the variable number of
 * data.  That is to say gfm_server_relay_put_reply() cannot be used in
 * a protocol handler of GFM_PROTO_GETDIRENTS.  For those protocol
 * commands, use gfm_server_relay_request_reply(),
 * gfm_server_relay_get_reply_dynarg() and
 * gfm_server_relay_get_reply_arg_dynarg(), instead.
 *
 * Usually the arguments 'peer', 'sizep', 'skip' and 'diag' given by
 * protocol_switch() to a protocol handler are also passed to this
 * function as they stand.
 *
 * Upon success, the function updates '*rp'.  If the requested command
 * is fed by a slave gfmd, the function creates an RPC relay handler and
 * it is set to '*rp'.  Otherwise '*rp' is set to NULL.
 * The allocated memory for the RPC relay handler will be disposed
 * asynchronously so that the protocol handler doesn't have to dispose
 * it explicitly.
 *
 * Upon successful completion, the function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
gfarm_error_t
gfm_server_relay_get_request(struct peer *peer, size_t *sizep,
	int skip, struct relayed_request **rp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_get_vrequest(peer, sizep, diag, format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (mdhost_self_is_master()) {
		*rp = NULL;
		return (e);
	}

	va_start(ap, format);
	e = gfm_server_relay_put_vrequest(peer, rp, diag,
	    command, format, &ap, 1);
	va_end(ap);

	return (e);
}

/**
 * Get a request from a client or a slave gfmd.
 *
 * @param peer     Connection peer.
 * @param sizep    Data size of the request.
 * @param skip     True if the transaction is suppressed.
 * @param r        RPC relay handler.
 * @param diag     Title for diagnostics messages.
 * @param format   Format of response data.
 * @return         Error code.
 *
 * The function is similar to gfm_server_get_request(), but it supports
 * RPC relay among master and slave gfmd servers.
 *
 * Note that this function must be called in a protocol handler 
 * implemented with gfm_server_relay_request_reply().  (See the document
 * of gfm_server_relay_requst_reply() for more details.)
 *
 * Usually the arguments 'peer', 'sizep', 'skip', 'r' and 'diag' given by
 * gfm_server_relay_request_reply() to the reply processor function are
 * passed to this function as they stand.
 *
 * Upon successful completion, the function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
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

/**
 * Put a reply to a client or a slave gfmd.
 *
 * @param peer     Connection peer.
 * @param sizep    Data size of the request.
 * @param diag     Title for diagnostics messages.
 * @param ecode    Result code of the protocol command.
 * @param format   Format of arguments of the gfmd protocol command.
 * @return         Error code.
 *
 * The function is usually used for sending the variable number of response
 * data of the gfm protocol command.  It is used together with
 * gfm_server_relay_put_reply_arg_dynarg() in a reply processor function
 * called by gfm_server_relay_request_reply().
 *
 * For example, the format of response data of the gfm command
 * GFM_PROTO_GETDIRENTS is:
 *
 * \verbatim
 *    i:ecode
 *    also reply the followings if ecode == GFARM_ERR_NO_ERROR:
 *        i:n_entries
 *        s[n_entries]:ent_names
 *        i[n_entries]:ent_types
 *        l[n_entries]:inode_numbers
 * \endverbatim
 *
 * Using gfm_server_relay_put_reply_dynarg() and 
 * gfm_server_relay_put_reply_arg_dynarg(), the response data can be
 * replied, like this:
 *
 * \verbatim
 *    gfm_server_relay_put_reply_dynarg(..., ecode, "i", n_entries)
 *    if (ecode == GFARM_ERR_NO_ERROR) {
 *       for (i = 0; i < n_entries; i++) 
 *          gfm_server_relay_put_reply_arg_dynarg(..., "s", ent_names[i]);
 *       for (i = 0; i < n_entries; i++) 
 *          gfm_server_relay_put_reply_arg_dynarg(..., "i", ent_types[i]);
 *       for (i = 0; i < n_entries; i++) 
 *          gfm_server_relay_put_reply_arg_dynarg(..., "l", inode_numbers[i]);
 *    }
 * \endverbatim
 *
 * Upon successful completion, the function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
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
		 * in master,
		 * 1. non-relayed reply with calculating size context
		 * 2. relayed reply with calculating size context
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

/**
 * Put a reply to a client or a slave gfmd.
 *
 * @param peer     Connection peer.
 * @param sizep    Data size of the request.
 * @param diag     Title for diagnostics messages.
 * @param format   Format of response data.
 * @return         Error code.
 *
 * The function is similar to gfm_server_put_reply(), but it supports
 * RPC relay among master and slave gfmd servers.
 *
 * Note that this function must be called in a protocol handler 
 * implemented with gfm_server_relay_request_reply().  (See the document
 * of gfm_server_relay_requst_reply() for more details.)
 *
 * If the gfm protocol command feeds the variable number of the response
 * data, use gfm_server_relay_put_reply_arg_dynarg() in combination with
 * this function.  See the document of gfm_server_relay_put_reply_arg_dynarg()
 * for more details.
 *
 * Usually the arguments 'peer', 'sizep' and 'diag' given by
 * gfm_server_relay_request_reply() to the reply processor function
 * are passed to this function as they stand.
 *
 * Upon successful completion, the function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
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
		/*
		 * in master,
		 * 1. non-relayed reply with calculating size context
		 * 2. relayed reply with calculating size context
		 */
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
request_reply_dynarg_norelay(struct peer *peer, gfp_xdr_xid_t xid, int skip,
    get_request_op_t get_request_op, put_reply_op_t put_reply_op,
    gfarm_int32_t command, void *closure, const char *diag)
{
	gfarm_error_t e;
	size_t size = 0;
	struct gfp_xdr *conn = peer_get_conn(peer);
	static const char relay_diag[] = "request_reply_dynarg_norelay";

	/*
	 * Get a request from a client.
	 */
	gfm_server_start_get_request(peer, diag);
	if ((e = get_request_op(NO_RELAY, peer, &size, skip, NULL, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (get_request_op): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	/*
	 * Reply to a client.
	 */
	size = 0;
	if ((e = put_reply_op(RELAY_CALC_SIZE, peer, &size, skip, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
 		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (put_reply_op - calc size): %s",
 		    diag, relay_diag, gfarm_error_string(e));
 		return (e);
 	}

	if ((e = gfp_xdr_send_async_result_header(conn, xid, size))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send_async_result_header): %s",
		    diag, relay_diag, gfarm_error_string(e));
	} else if ((e = put_reply_op(RELAY_TRANSFER, peer, NULL, skip, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (put_reply_op - transfer): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	gfp_xdr_flush(conn);
	return (e);
}

static gfarm_error_t
request_reply_dynarg_master(struct peer *peer, gfp_xdr_xid_t xid, int skip,
    get_request_op_t get_request_op, put_reply_op_t put_reply_op,
    gfarm_int32_t command, void *closure, const char *diag)
{
	gfarm_error_t e;
	size_t size = 0;
	struct gfp_xdr *conn = peer_get_conn(peer);
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;
	struct abstract_host *ah;
	struct peer *slave_mhpeer, *mhpeer = NULL;
	struct gfarm_thr_statewait *statewait;
	struct remote_peer *remote_peer;
	static const char relay_diag[] = "request_reply_dynarg_master";

	/*
	 * Get a request from a slave gfmd.
	 */
	gfm_server_start_get_request(peer, diag);
	if ((e = get_request_op(NO_RELAY, peer, &size, skip, NULL, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (get_request_op): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	slave_mhpeer = peer_get_parent(peer);
	ah = mdhost_to_abstract_host(peer_get_mdhost(slave_mhpeer));
	remote_peer = peer_to_remote_peer(peer);
	statewait = remote_peer_get_statewait(remote_peer);
	gfarm_thr_statewait_signal(statewait, e, diag);
	size = 0;

	/*
	 * Reply to the slave gfmd via gfmd channel.
	 */
	if ((e = put_reply_op(RELAY_CALC_SIZE, peer, &size, skip, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (put_reply_op - calc size): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	if ((e = gfp_xdr_send_size_add(&size, "ill",
	    GFARM_ERR_NO_ERROR, (gfarm_uint64_t)0, (gfarm_uint64_t)0))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send_size_add): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	if ((e = abstract_host_sender_lock(ah, slave_mhpeer, &mhpeer, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (abstract_host_sender_lock): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	if ((e = gfp_xdr_send_async_result_header(conn, xid, size))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send_async_result_header to master): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	master_get_db_update_info(peer, &seqnum, &flags);

	if ((e = gfp_xdr_send(conn, "ill", GFARM_ERR_NO_ERROR, seqnum, flags))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	if ((e = put_reply_op(RELAY_TRANSFER, peer, NULL, skip, closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (put_reply_op - transfer): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	gfp_xdr_flush(conn);

unlock_sender:
	if (mhpeer != NULL)
		abstract_host_sender_unlock(ah, mhpeer, diag);
	return (e);
}

static gfarm_error_t
request_reply_dynarg_slave(struct peer *peer, gfp_xdr_xid_t xid, int skip,
    get_request_op_t get_request_op, put_reply_op_t put_reply_op,
    gfarm_int32_t command, void *closure, const char *diag)
{
	gfarm_error_t e, e2;
	gfarm_int32_t relay_err;
	gfp_xdr_xid_t relay_xid;
	int eof, relay_xid_allocated = 0;
	int db_update_info_received = 0, reply_sent = 0;
	char *buf = NULL;
	size_t rsz, size = 0;
	gfarm_uint64_t seqnum, flags;
	struct peer *mhpeer;
	gfp_xdr_async_peer_t async_server = NULL;
	struct relayed_request *r = NULL;
	static const char relay_diag[] = "request_reply_dynarg_slave";

	/*
	 * Get a request from a client.
	 */
	gfm_server_start_get_request(peer, diag);
	if ((e = get_request_op(RELAY_CALC_SIZE, peer, &size, skip,
	    NULL, closure, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (get_request_op - calc size): %s",
		    diag, relay_diag, gfarm_error_string(e));
		return (e);
	}

	/*
	 * `r' will be freed asynchronously in
	 * slave_request_relay_result() or
	 * slave_request_relay_disconnect()
	 * after relayed_request_acquire_notify() is called.
	 */
	r = relayed_request_new(command, diag);
	if (r == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED, "%s: relayed_request_new: %s",
		    diag, gfarm_error_string(e));
		goto end;
	}

	if ((e = gfmdc_ensure_remote_peer(peer, &mhpeer))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfmdc_ensure_remote_peer)",
		    diag, gfarm_error_string(e));
		free(r);
		goto end;
	}

	/*
	 * See slave_request_relay() for packet layout detail.
	 */
	if ((e = gfp_xdr_send_size_add(&size, "ili", 0, (gfarm_int64_t)0, 0))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send_size_add): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}

	r->mode = RELAY_TRANSFER;
	r->conn = peer_get_conn(mhpeer);
	async_server = peer_get_async(mhpeer);

	if ((e = gfp_xdr_send_async_request_header(r->conn, async_server, size,
	    slave_request_relay_result, slave_request_relay_disconnect, r,
	    &relay_xid)) != GFARM_ERR_NO_ERROR) {
		goto unlock_sender;
	}
	relay_xid_allocated = 1;

	if ((e = gfp_xdr_send(r->conn, "ili", GFM_PROTO_REMOTE_RPC,
	    peer_get_private_peer_id(peer), command)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	if ((e = get_request_op(RELAY_TRANSFER, NULL, &size, skip, r, closure,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (get_request_op - transfer): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}
	if ((e = gfp_xdr_flush(r->conn)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_flush): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto unlock_sender;
	}

unlock_sender:
	gfmdc_ensure_remote_peer_end(mhpeer);

	if (!relay_xid_allocated)
		goto end;

	/*
	 * NOTE: layering violation
	 * Slave-gfmd is peeking `size' field of the protocol header of here.
	 * And transfering protocol data from the mater-gfmd to a client,
	 * by just using that `size' information.
	 */
	relayed_request_result_wait(r);
	if (e == GFARM_ERR_NO_ERROR)
		e = r->error;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (relayed_request_result_wait): %s/%s",
		    diag, relay_diag,
		    gfarm_error_string(r->error), gfarm_error_string(e));
		goto acquire_notify;
	}

	if ((e = gfp_xdr_recv_sized(r->conn, 1, 1, &r->rsize, &eof,
	    "i", &relay_err)) != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR)
			e = GFARM_ERR_UNEXPECTED_EOF;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_recv_sized): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto acquire_notify;
	}
	if (relay_err != GFARM_ERR_NO_ERROR) {
		e = relay_err;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_recv_sized:errcode): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto acquire_notify;
	}

	if ((e = gfp_xdr_recv_sized(r->conn, 1, 1, &r->rsize, &eof,
	    "ll", &seqnum, &flags)) != GFARM_ERR_NO_ERROR || eof) {
		if (e == GFARM_ERR_NO_ERROR)
			e = GFARM_ERR_UNEXPECTED_EOF;
		r->error = e;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_recv_sized): %s",
		    diag, relay_diag, gfarm_error_string(e));
		goto acquire_notify;
	}

	db_update_info_received = 1;

	GFARM_MALLOC_ARRAY(buf, r->rsize);
	if (buf == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s: %s", diag, relay_diag, gfarm_error_string(e));
		goto acquire_notify;
	}
	rsz = r->rsize;
	if ((e = gfp_xdr_recv(r->conn, 1, &eof, "r", rsz, &rsz, buf))
	    != GFARM_ERR_NO_ERROR || eof || rsz != 0) {
		if (e == GFARM_ERR_NO_ERROR) {
			assert(eof); /* if NO_ERROR && !eof, rsz must be 0 */
			e = GFARM_ERR_UNEXPECTED_EOF;
		}
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_recv): %s",
		    diag, relay_diag, gfarm_error_string(e));
	}

acquire_notify:
	relayed_request_acquire_notify(r);

	if (e != GFARM_ERR_NO_ERROR) {
		r = NULL;
		goto end;
	}

	if ((e = gfp_xdr_send_async_result_header(peer_get_conn(peer),
	    xid, r->rsize)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send_async_result_header to client): %s",
		    diag, relay_diag, gfarm_error_string(e));
	} else if ((e = gfp_xdr_send(peer_get_conn(peer), "r", r->rsize, buf))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_send): %s",
		    diag, relay_diag, gfarm_error_string(e));
	}
	r = NULL;
	reply_sent = 1;

end:
	if (!reply_sent) {
		rsz = 0;
		if ((e2 = gfp_xdr_send_size_add(&rsz, "i", 0))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s (gfp_xdr_send_size_add): %s",
			    diag, relay_diag, gfarm_error_string(e2));
		} else if ((e2 = gfp_xdr_send_async_result_header(
		    peer_get_conn(peer), xid, rsz))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s "
			    "(gfp_xdr_send_async_result_header to client): %s",
			    diag, relay_diag, gfarm_error_string(e2));
		} else if ((e2 = gfp_xdr_send(peer_get_conn(peer),
		    "i", (gfarm_int32_t)e)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s (error reply): %s",
			    diag, relay_diag, gfarm_error_string(e2));
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}
	if ((e2 = gfp_xdr_flush(peer_get_conn(peer))) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s (gfp_xdr_flush): %s",
		    diag, relay_diag, gfarm_error_string(e));
		if (e == GFARM_ERR_NO_ERROR)
			e = e2;
	}

	 /* buf and r may be NULL */
	free(buf);
	free(r);

	if (db_update_info_received) {
		if ((e = slave_add_db_update_info(seqnum, flags, diag))
		    != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: slave_add_db_update_info: %s",
			    diag, gfarm_error_string(e));
	}

	return (e);
}

/**
 * Process a gfm protocol command with RPC relay support.
 *
 * @param peer            Connection peer.
 * @param xid             Transaction ID.
 * @param skip            True if the transaction is suppressed.
 * @param get_request_op  Pointer to a request processor function.
 * @param put_reply_op    Pointer to a reply processor function.
 * @param command         ID of the gfm protocol command.
 * @param closure         Data passed to request/reply processor functions.
 * @param diag            Title for diagnostics messages.
 * @return                Error code.
 *
 * Some of the gfm protocol commands receive the variable number of
 * arguments and/or send the variable number of response data.
 * To support RPC relay, protocol handlers of those commands must use
 * this function.  (Also protocol handlers of other commands may use
 * this function, but it makes the protocol handlers complex.)
 *
 * Usually the arguments 'peer', 'xid', 'sizep' and 'skip' given by
 * protocol_switch() are passed to this function as they stand.
 *
 * The argument 'get_request_op' is a pointer to the request processor
 * function.  Its prototype is:
 *
 *    gfarm_error_t
 *    get_request(enum request_reply_mode mode, struct peer *peer,
 *       size_t *sizep, int skip, struct relayed_request *r, void *closure,
 *       const char *diag);
 * 
 * gfm_server_relay_request_reply() forwards the arguments 'peer', 'sizep',
 * 'skip', 'closure' and 'diag' to the 'get_request_op' function as they
 * are stands.  The argument 'r' is an RPC relay handler created by
 * gfm_server_relay_request_reply().
 *
 * On a slave gfmd, gfm_server_relay_request_reply() calls the
 * 'get_request_op' function  twice.  The argument 'mode' of the
 * 'get_request_op' function is set to RELAY_CALC_SIZE at the first call,
 * and set to RELAY_TRANSFER at the second call.  On other gfmd servers,
 * gfm_server_relay_request_reply() calls the 'get_request_op' function
 * only once and the value of 'mode' is NO_RELAY.
 * 
 * For the 'get_request_op' function to get a request, use
 * gfm_server_relay_get_request_dynarg().
 *
 * The argument 'put_reply_op' of gfm_server_relay_request_reply() is
 * a pointer to the reply processor function.  Its prptotype is:
 *
 *    gfarm_error_t
 *    put_reply(enum request_reply_mode mode, struct peer *peer,
 *       size_t *sizep, int skip, void *closure, const char *diag);
 * 
 * gfm_server_relay_request_reply() forwards the arguments 'peer', 'sizep',
 * 'skip', 'closure' and 'diag' to the 'put_reply_op' function as they
 * stand.
 *
 * On a master gfmd enabling RPC relay, gfm_server_request_relay() calls
 * the 'put_reply_op' function twice.  The argument 'mode' of the
 * 'put_reply_op' function is set to RELAY_CALC_SIZE at the first call,
 * and set to RELAY_TRANSFER at the second call.  On a master gfmd disabling
 * RPC relay, gfm_server_relay_request_reply() calls the 'put_reply_op'
 * function only once and the value of 'mode' is NO_RELAY.  On a slave
 * gfmd, gfm_server_relay_request_reply() never calls the 'put_reply_op'
 * function.
 * 
 * For the 'put_reply_op' function to put a reply, use
 * gfm_server_relay_put_reply_dynarg() and
 * gfm_server_relay_put_reply_arg_dynarg().
 *
 * Upon successful completion, this function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
gfarm_error_t
gfm_server_relay_request_reply(struct peer *peer, gfp_xdr_xid_t xid,
	int skip, get_request_op_t get_request_op, put_reply_op_t put_reply_op,
	gfarm_int32_t command, void *closure, const char *diag)
{
	if (!mdhost_self_is_master()) {
		/*
		 * This gfmd is a slave.
		 */
		return request_reply_dynarg_slave(peer, xid, skip,
		    get_request_op, put_reply_op, command, closure, diag);
	} else if (peer_get_async(peer) != NULL) {
		/*
		 * This gfmd is master and the request comes from a slave.
		 */
		return request_reply_dynarg_master(peer, xid, skip,
		    get_request_op, put_reply_op, command, closure, diag);
	} else {
		/*
		 * The request comes from a client.
		 */
		return request_reply_dynarg_norelay(peer, xid, skip,
		    get_request_op, put_reply_op, command, closure, diag);
	}
}

/*
 * NOTE: only slave gfmd is allowed to call this function.
 */
static gfarm_error_t
gfm_server_relay_get_vreply(struct relayed_request *r, const char *diag,
	const char *format, va_list *app)
{
	gfarm_error_t e, e2;
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;

	/* in slave, receive reply from master */
	relayed_request_result_wait(r);
	e = r->error;

	if (e == GFARM_ERR_NO_ERROR) {
		if ((e = slave_reply_relay(r, format, app, "ll",
		    &seqnum, &flags)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(e));
		}
		relayed_request_acquire_notify(r);
		if ((e2 = slave_add_db_update_info(
		    seqnum, flags, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: %s", diag, gfarm_error_string(e2));
			if (e == GFARM_ERR_NO_ERROR)
				e = e2;
		}
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: %s", r->diag, gfarm_error_string(e));
		relayed_request_acquire_notify(r);
	}
	return (e); /* notify the caller of the result */
}

gfarm_error_t
gfm_server_relay_get_reply(struct relayed_request *r, const char *diag,
	const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_server_relay_get_vreply(r, diag, format, &ap);
	va_end(ap);
	return (e);
}

static gfarm_error_t
gfm_server_put_vreply_to_client(struct peer *peer, gfp_xdr_xid_t xid,
	int isref, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (debug_mode)
		gflog_info(GFARM_MSG_1000229,
		    "<%s> sending reply: %d (%s)", diag, (int)ecode,
		    gfarm_error_string(ecode));

	e = gfp_xdr_vsend_async_wrapped_result(client, xid, isref,
	    0, NULL, NULL, ecode, format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000230,
		    "%s sending reply: %s",
		    diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	/* do not call gfp_xdr_flush() here for a compound protocol */

	return (ecode);
}

static gfarm_error_t
gfm_server_put_vreply_to_slave(struct peer *peer, gfp_xdr_xid_t xid,
	int isref, const char *diag, gfarm_error_t ecode,
	const char *wrapping_format, va_list *wrapping_app,
	const char *format, va_list *app)
{
	gfarm_error_t e;
	struct peer *slave_mhpeer = peer_get_parent(peer);

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "<%s> sending reply: %d (%s)", diag, (int)ecode,
		    gfarm_error_string(ecode));

	e = async_server_vput_wrapped_reply_unlocked(
	    peer_get_abstract_host(slave_mhpeer),
	    xid, isref, diag,
	    GFARM_ERR_NO_ERROR, wrapping_format, wrapping_app,
	    ecode, format, app);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s sending relayed reply: %s",
		    diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}

	return (ecode);
}

/* send reply from master to slave gfmd */
static gfarm_error_t
master_reply_relay(struct peer *peer, gfp_xdr_xid_t xid,
	int isref, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app,
	const char *wformat, ...)
{
	gfarm_error_t e;
	va_list wap;

	va_start(wap, wformat);
	e = gfm_server_put_vreply_to_slave(peer, xid, isref, diag,
	    ecode, wformat, &wap, format, app);
	va_end(wap);
	return (e);
}

static gfarm_error_t
gfm_server_put_vreply(struct peer *peer, gfp_xdr_xid_t xid,
	int isref, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;
	struct peer *slave_mhpeer, *mhpeer = NULL;
	struct abstract_host *ah;
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;

	if (peer_get_async(peer) == NULL) {
		/*
		 * The request comes from a client.
		 */
		e = gfm_server_put_vreply_to_client(peer, xid, isref, diag,
		    ecode, format, app);
	} else {
		/*
		 * The request comes from a slave gfmd.
		 * i.e. This gfmd is a master gfmd.
		 * NOTE: when you change this, change
		 * gfm_server_put_reply_begin() and gfm_server_put_reply_end()
		 * as well.
		 */
		slave_mhpeer = peer_get_parent(peer);
		ah = mdhost_to_abstract_host(peer_get_mdhost(slave_mhpeer));
		if ((e = abstract_host_sender_lock(ah, slave_mhpeer,
		    &mhpeer, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: abstract_host_sender_lock(): %s",
			    diag, gfarm_error_string(e));
		} else {
			master_get_db_update_info(peer, &seqnum, &flags);
			e = master_reply_relay(peer, xid, isref, diag,
			    ecode, format, app, "ll", seqnum, flags);
			abstract_host_sender_unlock(ah, mhpeer, diag);
		}
	}

	return (e);
}

gfarm_error_t
gfm_server_put_reply(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	const char *diag, gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_put_vreply(peer, xid, 0, diag, ecode, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfm_server_put_vreply_begin(struct peer *peer, struct peer **slave_mhpeerp,
	gfp_xdr_xid_t xid, int *size_posp, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct peer *slave_mhpeer, *mhpeer;
	struct abstract_host *ah;
	gfarm_uint64_t seqnum;
	gfarm_uint64_t flags;

	if (debug_mode)
		gflog_info(GFARM_MSG_UNFIXED,
		    "<%s> xid:%d sending reply: %d (%s)",
		    diag, (int)xid, (int)ecode, gfarm_error_string(ecode));

	if (peer_get_async(peer) == NULL) {
		/* The request comes from a client. */
		slave_mhpeer = NULL;
		e = gfp_xdr_vrpc_send_result_begin(client, xid, size_posp,
		    ecode, &format, app);
	} else {
		/*
		 * The request comes from a slave gfmd.
		 * i.e. This gfmd is a master gfmd.
		 * NOTE: when you change this, change gfm_server_put_reply() 
		 * as well.
		 */
		slave_mhpeer = peer_get_parent(peer);
		ah = mdhost_to_abstract_host(peer_get_mdhost(slave_mhpeer));
		if ((e = abstract_host_sender_lock(ah, slave_mhpeer, &mhpeer,
		    diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: abstract_host_sender_lock(): %s",
			    diag, gfarm_error_string(e));
		} else {
			master_get_db_update_info(peer, &seqnum, &flags);
			e = gfp_xdr_rpc_send_result_begin(
			    peer_get_conn(mhpeer), xid, size_posp,
			    GFARM_ERR_NO_ERROR, "ll", seqnum, flags);
			if (e != GFARM_ERR_NO_ERROR)
				abstract_host_sender_unlock(ah, mhpeer, diag);
		}

	}
	if (e == GFARM_ERR_NO_ERROR)
		*slave_mhpeerp = slave_mhpeer;
	return (e);
}

gfarm_error_t
gfm_server_put_reply_begin(struct peer *peer, struct peer **slave_mhpeerp,
	gfp_xdr_xid_t xid, int *size_posp,
	const char *diag, gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_put_vreply_begin(peer, slave_mhpeerp, xid, size_posp,
	    diag, ecode, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfm_server_put_reply_end(struct peer *peer, struct peer *slave_mhpeer,
	const char *diag, int size_pos)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	struct abstract_host *ah;

	gfp_xdr_rpc_send_end(client, size_pos);

	if (slave_mhpeer == NULL) {
		/*
		 * The request comes from a client.
		 * nothing to do
		 */
	} else {
		/*
		 * The request comes from a slave gfmd.
		 * i.e. This gfmd is a master gfmd.
		 * NOTE: when you change this, change gfm_server_put_reply() 
		 * as well.
		 */
		ah = mdhost_to_abstract_host(peer_get_mdhost(slave_mhpeer));
		abstract_host_sender_unlock(ah, slave_mhpeer, diag);
	}
	return (GFARM_ERR_NO_ERROR);
}

/**
 * Put a reply to a client or a slave gfmd.
 *
 * @param peer     Connection peer.
 * @param xid      Transaction ID.
 * @param sizep    Data size of the reply.
 * @param r        RPC relay handler.
 * @param diag     Title for diagnostics messages.
 * @param ecode    Result code of the protocol command.
 * @param format   Format of response data.
 * @return         Error code.
 *
 * The function is similar to gfm_server_put_reply(), but it supports
 * RPC relay among master and slave gfmd servers.
 *
 * This function can be used in a protocol handler of a gfm protocol
 * command with the fixed number of arguments and response data.
 * (See the document of gfm_server_relay_get_request() for more details.)
 *
 * Usually the arguments 'peer', 'sizep' and 'diag' given by
 * protocol_switch() to the protocol handler function are passed to
 * this function as they stand.
 *
 * The argument 'r' is a pointer to an RPC relay handler created by
 * gfm_server_relay_get_request().
 *
 * Upon successful completion, the function returns GFARM_ERR_NO_ERROR.
 * Otherwise it returns an error code.
 */
gfarm_error_t
gfm_server_relay_put_reply(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, struct relayed_request *r, const char *diag,
	gfarm_error_t *ecodep, const char *format, ...)
{
	gfarm_error_t e, ecode = *ecodep;
	va_list ap;

	if (r != NULL) {
		/* in slave, receive reply from master */
		va_start(ap, format);
		e = gfm_server_relay_get_vreply(r, diag, format, &ap);
		va_end(ap);

		*ecodep = e; /* notify the caller of the result */
		if (ecode == GFARM_ERR_NO_ERROR)
			ecode = e;
	}

	va_start(ap, format);
	e = gfm_server_put_vreply(peer, xid, 1, diag, ecode, format, &ap);
	va_end(ap);

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

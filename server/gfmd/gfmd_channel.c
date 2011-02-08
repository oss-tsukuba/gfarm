/*
 * $Id$
 */

#include <assert.h>
#include <pthread.h>
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
#include "thrsubr.h"

#include "gfp_xdr.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "auth.h"
#include "gfm_client.h"
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
#include "db_journal.h"
#include "journal_file.h"
#include "gfmd_channel.h"

#ifdef ENABLE_JOURNAL
static struct thread_pool *gfmdc_recv_thread_pool;
static struct thread_pool *gfmdc_send_thread_pool;

#define BACK_CHANNEL_DIAG "gfmd_channel"

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
gfmdc_server_put_reply(struct mdhost *host,
	struct peer *peer, gfp_xdr_xid_t xid,
	const char *diag, gfarm_error_t errcode, char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_server_channel_vput_reply(
	    mdhost_to_abstract_host(host), peer, xid, diag,
	    errcode, format, &ap, BACK_CHANNEL_DIAG);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfmdc_client_recv_result(struct peer *peer, struct mdhost *host,
	size_t size, const char *diag, const char *format, ...)
{
	gfarm_error_t e, errcode;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vrecv_result(peer,
	    mdhost_to_abstract_host(host), size, diag,
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
gfmdc_client_send_request(struct mdhost *host,
	struct peer *peer0, const char *diag,
	gfarm_int32_t (*result_callback)(void *, void *, size_t),
	void (*disconnect_callback)(void *, void *),
	void *closure,
	gfarm_int32_t command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vsend_request(
	    mdhost_to_abstract_host(host), peer0, diag,
	    result_callback, disconnect_callback, closure,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    command, format, &ap, BACK_CHANNEL_DIAG);
	va_end(ap);
	return (e);
}

struct journal_send_closure {
	struct mdhost *host;
	void *data;
};

static gfarm_int32_t
gfmdc_client_journal_send_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct journal_send_closure *closure = arg;
	struct mdhost *host = closure->host;
	static const char *diag = "GFM_PROTO_JOURNAL_SEND";

	if ((e = gfmdc_client_recv_result(peer, host, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(host), gfarm_error_string(e));
	free(closure->data);
	free(closure);
	return (e);
}

static void
gfmdc_client_journal_send_free(void *p, void *arg)
{
	struct journal_send_closure *closure = arg;

	free(closure->data);
	free(closure);
}

static gfarm_error_t
gfmdc_client_journal_send(struct mdhost *host)
{
	gfarm_error_t e;
	int data_len, no_rec;
	char *data;
	gfarm_uint64_t min_seqnum, from_sn, to_sn, lf_sn;
	struct journal_file_reader *reader;
	struct journal_send_closure *closure;
	static const char *diag = "GFM_PROTO_JOURNAL_SEND";

	lf_sn = mdhost_get_last_fetch_seqnum(host);
	min_seqnum = lf_sn == 0 ? 0 : lf_sn + 1;
	reader = mdhost_get_journal_file_reader(host);

	e = db_journal_fetch(reader, min_seqnum, &data, &data_len, &from_sn,
	    &to_sn, &no_rec, mdhost_get_name(host));
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(host), gfarm_error_string(e));
		return (e);
	} else if (no_rec)
		return (GFARM_ERR_NO_ERROR);
	mdhost_set_last_fetch_seqnum(host, to_sn);

	GFARM_MALLOC(closure);
	if (closure == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		free(data);
		return (e);
	}
	closure->host = host;
	closure->data = data;

	/* FIXME must be async-request */
	if ((e = gfmdc_client_send_request(host, NULL, diag,
	    gfmdc_client_journal_send_result,
	    gfmdc_client_journal_send_free, closure,
	    GFM_PROTO_JOURNAL_SEND, "llb", from_sn, to_sn,
	    (size_t)data_len, data)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(host), gfarm_error_string(e));
		free(data);
	}
	return (e);
}

static gfarm_error_t
gfmdc_server_journal_send(struct mdhost *host, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, er;
	gfarm_uint64_t from_sn, to_sn;
	unsigned char *recs = NULL;
	size_t recs_len;
	static const char *diag = "GFM_PROTO_JOURNAL_SEND";

	if ((er = gfmdc_server_get_request(peer, size, diag, "llB",
	    &from_sn, &to_sn, &recs_len, &recs))
	    == GFARM_ERR_NO_ERROR)
		er = db_journal_recvq_enter(from_sn, to_sn, recs_len, recs,
		    diag);
#ifdef DEBUG_JOURNAL
	if (er == GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "from %s : recv journal %llu to %llu",
		    mdhost_get_name(host), (unsigned long long)from_sn,
		    (unsigned long long)to_sn);
#endif
	e = gfmdc_server_put_reply(host, peer, xid, diag, er, "");
	return (e);
}

static gfarm_error_t
gfmdc_server_journal_ready_to_recv(struct mdhost *host, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	static const char *diag = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	if ((e = gfmdc_server_get_request(peer, size, diag, "l", &seqnum))
	    == GFARM_ERR_NO_ERROR) {
		mdhost_set_last_fetch_seqnum(host, seqnum);
		mdhost_set_is_recieved_seqnum(host, 1);
#ifdef DEBUG_JOURNAL
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s : last_fetch_seqnum=%llu",
		    mdhost_get_name(host), (unsigned long long)seqnum);
#endif
	}
	e = gfmdc_server_put_reply(host, peer, xid, diag, e, "");
	return (e);
}

static gfarm_int32_t
gfmdc_client_journal_ready_to_recv_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *host = peer_get_mdhost(peer);
	static const char *diag = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	if ((e = gfmdc_client_recv_result(peer, host, size, diag, ""))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED, "%s : %s",
		    mdhost_get_name(host), gfarm_error_string(e));
	return (e);
}

static void
gfmdc_client_journal_ready_to_recv_disconnect(void *p, void *arg)
{
}

gfarm_error_t
gfmdc_client_journal_ready_to_recv(struct mdhost *host)
{
	gfarm_error_t e;
	gfarm_uint64_t seqnum;
	static const char *diag = "GFM_PROTO_JOURNAL_READY_TO_RECV";

	giant_lock();
	seqnum = db_journal_get_current_seqnum();
	giant_unlock();
	/* FIXME must be async-request */
	if ((e = gfmdc_client_send_request(host, NULL, diag,
	    gfmdc_client_journal_ready_to_recv_result,
	    gfmdc_client_journal_ready_to_recv_disconnect, NULL,
	    GFM_PROTO_JOURNAL_READY_TO_RECV, "l", seqnum))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s : %s", mdhost_get_name(host), gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfmdc_protocol_switch(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
	struct mdhost *host = abstract_host_to_mdhost(h);
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	switch (request) {
	case GFM_PROTO_JOURNAL_READY_TO_RECV:
		/* in master */
		e = gfmdc_server_journal_ready_to_recv(host, peer, xid, size);
		break;
	case GFM_PROTO_JOURNAL_SEND:
		/* in slave */
		e = gfmdc_server_journal_send(host, peer, xid, size);
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
	struct mdhost *host = NULL;
	gfp_xdr_async_peer_t async = NULL;

	giant_lock();
	if (from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"Operation not permitted: from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (peer_get_async(peer) == NULL &&
	    (host = peer_get_mdhost(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"Operation not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = gfp_xdr_async_peer_new(&async))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: gfp_xdr_async_peer_new(): %s",
		    diag, gfarm_error_string(e));
	}
	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR) {
		peer_set_async(peer, async);
		peer_set_protocol_handler(peer,
		    gfmdc_recv_thread_pool,
		    gfmdc_main);

		if (mdhost_is_up(host)) /* throw away old connetion */ {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "gfmd_channel(%s): switching to new connection",
			    mdhost_get_name(host));
			mdhost_disconnect(host, NULL);
		}
		abstract_host_set_peer(mdhost_to_abstract_host(host),
		    peer, version);
		peer_watch_access(peer);
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

	er = switch_gfmd_channel(peer, from_client, version, diag);
	if ((e = gfm_server_put_reply(peer, diag, er, "i", 0 /*XXX FIXME*/))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (debug_mode)
		gflog_debug(GFARM_MSG_UNFIXED, "gfp_xdr_flush");
	if ((e = gfp_xdr_flush(peer_get_conn(peer))) != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: protocol flush: %s",
		    diag, gfarm_error_string(e));
	return (e);
}

void
gfmdc_init(void)
{
	/* XXX FIXME: use different config parameter */
	gfmdc_recv_thread_pool = thrpool_new(
	    gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "receiving from gfmd");
	gfmdc_send_thread_pool = thrpool_new(
	    gfarm_metadb_thread_pool_size,
	    gfarm_metadb_job_queue_length, "sending to gfmd");
	if (gfmdc_recv_thread_pool == NULL ||
	    gfmdc_send_thread_pool == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "gfmd channel thread pool size:"
		    "%d, queue length:%d: no memory",
		    gfarm_metadb_thread_pool_size,
		    gfarm_metadb_job_queue_length);
}

static void
gfmdc_seteuid(const char *guser)
{
	gfarm_error_t e;
	char *local_user;
	struct passwd *pw;
	uid_t uid;

	if ((e = gfarm_global_to_local_username_by_host(
	    gfarm_metadb_server_name, gfarm_metadb_server_port,
	    guser, &local_user)) != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "no local user for the global `%s' user.",
		    guser); /* exit */
	}
	if ((pw = getpwnam(local_user)) == NULL) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "user `%s' is necessary, but doesn't exist.",
		    local_user); /* exit */
	}
	uid = pw->pw_uid;
	if (seteuid(uid) == -1)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "seteuid(%d)", (int)uid);
	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "acquiring information about user `%s': %s",
		    local_user, gfarm_error_string(e));
	}
	free(local_user);
}

static gfarm_error_t
gfmdc_connect(struct mdhost *host)
{
	gfarm_error_t e;
	int port;
	const char *hostname;
	gfarm_int32_t gfmd_knows_me;
	struct gfm_connection *conn;
	struct peer *peer = NULL;
	struct journal_file_reader *reader;
	/* XXX FIXME must be configuable */
	unsigned int sleep_interval = 10;
	/* XXX FIXME must be configuable */
	static unsigned int sleep_max_interval = 640;
	static int hack_to_make_cookie_not_work = 0; /* XXX FIXME */
	static const char *service_user = GFMD_USERNAME;
	static const char *diag = "gfmdc_connect";

	if (geteuid() != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "gfmd must be run by root user"); /* exit */

	gfarm_set_auth_id_type(GFARM_AUTH_ID_TYPE_METADATA_HOST);

	hostname = mdhost_get_name(host);
	port = mdhost_get_port(host);

	for (;;) {
		gfarm_auth_privilege_lock(diag);
		gfmdc_seteuid(service_user);
		e = gfm_client_connect(hostname, port, service_user,
		    &conn, NULL);
		if (seteuid(0) == -1)
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "seteuid(0)"); /* exit */
		gfarm_auth_privilege_unlock(diag);
		if (e == GFARM_ERR_NO_ERROR)
			break;
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		if (sleep_interval < sleep_max_interval)
			gflog_error(GFARM_MSG_UNFIXED,
			    "connecting to gfmd at %s:%d failed, "
			    "sleep %d sec: %s", hostname, port, sleep_interval,
			    gfarm_error_string(e));
		sleep(sleep_interval);
		if (sleep_interval < sleep_max_interval)
			sleep_interval *= 2;
	}
	if ((e = gfm_client_switch_gfmd_channel(conn,
	    GFM_PROTOCOL_VERSION, (gfarm_int64_t)hack_to_make_cookie_not_work,
	    &gfmd_knows_me))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		return (e);
	}
	if ((e = peer_alloc_with_connection(&peer,
	    gfm_client_connection_conn(conn), mdhost_to_abstract_host(host),
	    GFARM_AUTH_ID_TYPE_METADATA_HOST)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		goto error;
	}
	if ((e = switch_gfmd_channel(peer, 0, GFM_PROTOCOL_VERSION, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfmd_channel(%s) : %s",
		    hostname, gfarm_error_string(e));
		goto error;
	}
	mdhost_set_connection(host, conn);
	mdhost_activate(host);
	if ((e = gfmdc_client_journal_ready_to_recv(host))
	    != GFARM_ERR_NO_ERROR) {
		mdhost_set_connection(host, NULL);
		goto error;
	}
	reader = mdhost_get_journal_file_reader(host);
	if (reader != NULL && journal_file_reader_is_invalid(reader)) {
		if ((e = journal_file_reader_reopen(reader,
		    mdhost_get_last_fetch_seqnum(host)))
		    != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "gfmd_channel(%s) : %s",
			    hostname, gfarm_error_string(e));
			goto error;
		}
	}
	return (GFARM_ERR_NO_ERROR);
error:
	gfm_client_connection_free(conn);
	if (peer)
		peer_free(peer);
	return (e);
}

static int
gfmdc_each_mdhost(struct mdhost *host, void *closure)
{
	gfarm_error_t e;

	if (mdhost_is_master(host))
		return (1);
	if (!mdhost_is_recieved_seqnum(host))
		return (1);

	e = gfmdc_client_journal_send(host);
	if (e != GFARM_ERR_NO_ERROR) {
		/* XXX */
		mdhost_disconnect(host, mdhost_get_peer(host));
	}
	return (1);
}

void *
gfmdc_master_thread(void *arg)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 500 * 1000 * 1000;

	for (;;) {
		mdhost_foreach(gfmdc_each_mdhost, NULL);
		nanosleep(&ts, NULL);
	}
	return (NULL);
}

void *
gfmdc_slave_thread(void *arg)
{
	gfarm_error_t e;
	struct mdhost *host;

	host = mdhost_lookup_master();
	assert(host != NULL);
	for (;;) {
		if (mdhost_get_connection(host) != NULL) {
			sleep(30);
		} else if ((e = gfmdc_connect(host)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_ERR_NO_ERROR,
			    "gfmd_channel(%s) : give up to connect : %s",
			    mdhost_get_name(host), gfarm_error_string(e));
			break;
		}
	}
	return (NULL);
}
#endif /* ENABLE_JOURNAL */

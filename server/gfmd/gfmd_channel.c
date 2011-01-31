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
#include "gfmd_channel.h"

static struct thread_pool *gfmdc_recv_thread_pool;
static struct thread_pool *gfmdc_send_thread_pool;

#define BACK_CHANNEL_DIAG "gfmd_channel"

#ifdef ENABLE_JOURNAL
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
	e = gfm_server_channel_vput_reply(ABS_HOST(host), peer, xid, diag,
	    errcode, format, &ap, BACK_CHANNEL_DIAG);
	va_end(ap);

	return (e);
}

static gfarm_error_t
gfmdc_server_journal_fetch(struct mdhost *host, struct peer *peer,
	gfp_xdr_xid_t xid, size_t size)
{
	gfarm_error_t e, er;
	int data_len;
	char *data;
	gfarm_uint64_t min_seqnum, from_sn, to_sn, lf_sn;
	static const char *diag = "GFM_PROTO_JOURNAL_FETCH";

	e = gfmdc_server_get_request(peer, size, diag, "l", &min_seqnum);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "gfmd_channel %s : %s",
		    mdhost_get_name(host), gfarm_error_string(e));
	} else {
		giant_lock();
		er = db_journal_fetch(mdhost_get_journal_file_reader(host),
		    min_seqnum, &data, &data_len, &from_sn, &to_sn,
		    mdhost_get_name(host));
		giant_unlock();
		if (to_sn > 0 && er == GFARM_ERR_NO_ERROR) {
			lf_sn = mdhost_get_last_fetch_seqnum(host);
			if (from_sn <= lf_sn) {
				er = GFARM_ERR_EXPIRED;
				gflog_error(GFARM_MSG_UNFIXED,
				    "invalid seqnum "
				    "(%llu <= last fetched:%llu)",
				    (unsigned long long)from_sn,
				    (unsigned long long)lf_sn);
			}
		}
		if (er != GFARM_ERR_NO_ERROR) {
			data = NULL;
			data_len = 0;
		}
		e = gfmdc_server_put_reply(host, peer, xid, diag, er, "llb",
		    from_sn, to_sn, (size_t)data_len, data);
		free(data);
	}
	return (e);
}
#endif

static gfarm_error_t
gfmdc_protocol_switch(struct abstract_host *h,
	struct peer *peer, int request, gfp_xdr_xid_t xid, size_t size,
	int *unknown_request)
{
#ifdef ENABLE_JOURNAL
	struct mdhost *host = MD_HOST(h);
#endif
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	switch (request) {
#ifdef ENABLE_JOURNAL
	case GFM_PROTO_JOURNAL_FETCH:
		e = gfmdc_server_journal_fetch(host, peer, xid, size);
		break;
#endif
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
			    "back_channel(%s): switching to new connection",
			    mdhost_get_name(host));
			mdhost_disconnect(host, NULL);
		}
		abstract_host_set_peer(ABS_HOST(host), peer, version);
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
	struct peer *peer;
	gfp_xdr_async_peer_t async = NULL;
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
	    gfm_client_connection_conn(conn), ABS_HOST(host),
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
	return (GFARM_ERR_NO_ERROR);
error:
	gfm_client_connection_free(conn);
	gfp_xdr_async_peer_free(async, peer);
	return (e);
}

static gfarm_error_t
gfmdc_client_recv_result(struct peer *peer, struct mdhost *host,
       size_t size, const char *diag, const char *format, ...)
{
	gfarm_error_t e, errcode;
	va_list ap;

	va_start(ap, format);
	e = gfm_client_channel_vrecv_result(peer, ABS_HOST(host), size, diag,
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
	e = gfm_client_channel_vsend_request(ABS_HOST(host), peer0, diag,
	    result_callback, disconnect_callback, closure,
#ifdef COMPAT_GFARM_2_3
	    NULL,
#endif
	    command, format, &ap, BACK_CHANNEL_DIAG);
	va_end(ap);
	return (e);
}

static gfarm_int32_t
gfmdc_client_journal_fetch_result(void *p, void *arg, size_t size)
{
	gfarm_error_t e;
	struct peer *peer = p;
	struct mdhost *host = arg;
	gfarm_uint64_t from_sn, to_sn;
	char *data = NULL;
	size_t data_len;
	static const char *diag = "GFM_PROTO_JOURNAL_FETCH";

	e = gfmdc_client_recv_result(peer, host, size, diag, "llB",
	    &from_sn, &to_sn, &data_len, &data);
#ifdef DEBUG_JOURNAL
	if (e == GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED, "from %s : fetch %llu to %llu",
		    mdhost_get_name(host), (unsigned long long)from_sn,
		    (unsigned long long)to_sn);
#endif
	free(data);
	return (e);
}

static void
gfmdc_client_journal_fetch_free(void *p, void *arg)
{
}

gfarm_error_t
gfmdc_client_journal_fetch(struct mdhost *host, gfarm_uint64_t seqnum)
{
	/* FIXME must be async-request */
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_JOURNAL_FETCH";

	e = gfmdc_client_send_request(host, NULL, diag,
	    gfmdc_client_journal_fetch_result,
	    gfmdc_client_journal_fetch_free, host,
	    GFM_PROTO_JOURNAL_FETCH, "l", seqnum);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	return (e);
}

void
gfmdc_thread(void)
{
	gfarm_error_t e;
	struct mdhost *host;

	host = mdhost_lookup_master();
	assert(host == NULL);
	for (;;) {
		if ((e = gfmdc_connect(host)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_ERR_NO_ERROR,
			    "gfmd_channel(%s) : give up to connect : %s",
			    mdhost_get_name(host), gfarm_error_string(e));
			return;
		}
	}
}

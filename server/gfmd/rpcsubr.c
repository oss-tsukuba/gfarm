#include <pthread.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "rpcsubr.h"
#include "peer.h"
#include "abstract_host.h"

void
gfm_server_start_get_request(struct peer *peer, const char *diag)
{
	if (debug_mode)
		gflog_info(GFARM_MSG_1000225, "<%s> start receiving", diag);
}

/* sizep != NULL, if this is an inter-gfmd-relayed request. */
gfarm_error_t
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
gfarm_error_t
gfm_server_get_vrequest(struct peer *peer, size_t *sizep,
	const char *diag, const char *format, va_list *app)
{
	gfm_server_start_get_request(peer, diag);
	return (gfm_server_vrecv(peer, sizep, diag, format, app));
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

/*
 * if this is an inter-gfmd-relayed reply, xid is valid and sizep != NULL.
 * Note that *sizep won't be used actually, but sizep != NULL condition will
 * be used to see whether this is an inter-gfmd-relayed reply or not.
 */
static gfarm_error_t
gfm_server_put_wrapped_vreply0(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, xdr_vsend_t xdr_vsend, int do_lock, const char *diag,
	gfarm_error_t ecode, const char *wrapping_format, va_list *wrapping_app,
	const char *format, va_list *app)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	struct peer *slave_mhpeer;

	if (debug_mode)
		gflog_info(GFARM_MSG_1000229,
		    "<%s> sending reply: %d", diag, (int)ecode);

	if (sizep != NULL) {
		slave_mhpeer = peer_get_parent(peer);
		if (do_lock) {
			e = async_server_vput_wrapped_reply(
			    peer_get_abstract_host(slave_mhpeer), slave_mhpeer,
			    xid, xdr_vsend, diag,
			    ecode, wrapping_format, wrapping_app, format, app);
		} else {
			e = async_server_vput_wrapped_reply_unlocked(
			    peer_get_abstract_host(slave_mhpeer),
			    xid, xdr_vsend, diag,
			    ecode, wrapping_format, wrapping_app, format, app);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s sending relayed reply: %s",
			    diag, gfarm_error_string(e));
			peer_record_protocol_error(peer);
			return (e);
		}
	} else {
		e = gfp_xdr_send(client, "i", (gfarm_int32_t)ecode);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000230,
			    "%s sending reply: %s",
			    diag, gfarm_error_string(e));
			peer_record_protocol_error(peer);
			return (e);
		}
		if (ecode == GFARM_ERR_NO_ERROR) {
			e = (*xdr_vsend)(client, &format, app);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000231,
				    "%s sending reply: %s",
				    diag, gfarm_error_string(e));
				peer_record_protocol_error(peer);
				return (e);
			}
			if (*format != '\0')
				gflog_fatal(GFARM_MSG_1000232,
				    "%s sending reply: %s", diag,
				    "invalid format character");
		}
	}
	/* do not call gfp_xdr_flush() here for a compound protocol */

	return (ecode);
}

gfarm_error_t
gfm_server_put_wrapped_vreply(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, xdr_vsend_t xdr_vsend, const char *diag,
	gfarm_error_t ecode, const char *wrapping_format,
	va_list *wrapping_app, const char *format, va_list *app)
{
	return (gfm_server_put_wrapped_vreply0(peer, xid, sizep, xdr_vsend, 
	    0, diag, ecode, wrapping_format, wrapping_app, format,
	    app));
}

gfarm_error_t
gfm_server_put_vreply(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, xdr_vsend_t xdr_vsend, const char *diag,
	gfarm_error_t ecode, const char *format, va_list *app)
{
	return (gfm_server_put_wrapped_vreply0(peer, xid, sizep, xdr_vsend,
	    1, diag, ecode, NULL, NULL, format, app));
}

gfarm_error_t
gfm_server_put_reply(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	const char *diag, gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfm_server_put_vreply(peer, xid, sizep, gfp_xdr_vsend, diag,
	    ecode, format, &ap);
	va_end(ap);
	return (e);
}

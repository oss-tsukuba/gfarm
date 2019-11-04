#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

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

gfarm_error_t
gfm_server_get_request(struct peer *peer, const char *diag,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int eof;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (debug_mode)
		gflog_info(GFARM_MSG_1000225, "<%s> start receiving", diag);

	va_start(ap, format);
	e = gfp_xdr_vrecv(client, 0, 1, &eof, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1000226,
		    "%s receiving request: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (eof) {
		gflog_notice(GFARM_MSG_1000227,
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

gfarm_error_t
gfm_server_put_reply(struct peer *peer, const char *diag,
	gfarm_error_t ecode, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);

	if (debug_mode)
		gflog_info(GFARM_MSG_1000229,
		    "<%s> sending reply: %d", diag, (int)ecode);

	va_start(ap, format);
	e = gfp_xdr_send(client, "i", (gfarm_int32_t)ecode);
	if (e != GFARM_ERR_NO_ERROR) {
		va_end(ap);
		gflog_notice(GFARM_MSG_1000230,
		    "%s sending reply: %s", diag, gfarm_error_string(e));
		peer_record_protocol_error(peer);
		return (e);
	}
	if (ecode == GFARM_ERR_NO_ERROR) {
		e = gfp_xdr_vsend(client, &format, &ap);
		if (e != GFARM_ERR_NO_ERROR) {
			va_end(ap);
			gflog_notice(GFARM_MSG_1000231,
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
	va_end(ap);
	/* do not call gfp_xdr_flush() here for a compound protocol */

	return (ecode);
}

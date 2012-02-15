#include <stdarg.h>
#include <stddef.h>

#include <gfarm/gfarm.h>

#include "gfp_xdr.h"

#include "rpcsubr.h"
#include "mdhost.h"
#include "relay.h"

gfarm_error_t
relay_put_request(struct relayed_request **reqp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
relay_get_reply(struct relayed_request *req, const char *diag,
	gfarm_error_t *ep, const char *format, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/* returns *reqp != NULL, if !mdhost_self_is_master() case. */
gfarm_error_t
gfm_server_get_request_with_relay(
	struct peer *peer, size_t *sizep,
	int skip, struct relayed_request **relayp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	if (!mdhost_self_is_master())
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);

	va_start(ap, format);
	e = gfm_server_get_vrequest(peer, sizep, diag, format, &ap);
	va_end(ap);
	if (e == GFARM_ERR_NO_ERROR)
		*relayp = NULL;
	return (e);
}

gfarm_error_t gfm_server_put_reply_with_relay(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	struct relayed_request *relay, const char *diag,
	gfarm_error_t *ep, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	if (!mdhost_self_is_master())
		return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);

	va_start(ap, format);
	e = gfm_server_put_vreply(peer, xid, sizep, gfp_xdr_vsend_ref, diag,
	    *ep, format, &ap);
	va_end(ap);
	return (e);
}

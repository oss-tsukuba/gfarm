#include <stdarg.h>
#include <stddef.h>

#include <gfarm/gfarm.h>

#include "gfp_xdr.h"

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
	int skip, struct relayed_request **reqp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t gfm_server_put_reply_with_relay(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	struct relayed_request *req, const char *diag,
	gfarm_error_t *ep, const char *fmormat, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

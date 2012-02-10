#include <stdarg.h>
#include <stddef.h>

#include <gfarm/gfarm.h>

#include "relay.h"

gfarm_error_t
relay_put_request(struct relayed_reqeust **reqp, const char *diag,
	gfarm_int32_t command, const char *format, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
relay_get_reply(struct relayed_reqeust *req, const char *diag,
	gfarm_error_t *ep, const char *format, ...)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}


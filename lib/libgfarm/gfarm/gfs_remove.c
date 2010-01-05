#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

static gfarm_error_t
gfm_remove_request(struct gfm_connection *gfm_server, void *closure,
	const char *base)
{
	gfarm_error_t e;

	if ((e = gfm_client_remove_request(gfm_server, base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("remove request: %s", gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_remove_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e;

	if ((e = gfm_client_remove_result(gfm_server)) != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug("remove result: %s", gfarm_error_string(e));
#endif
	}
	return (e);
}

gfarm_error_t
gfs_remove(const char *path)
{
	return (gfm_name_op(path, GFARM_ERR_IS_A_DIRECTORY /*XXX posix ok?*/,
	    gfm_remove_request,
	    gfm_remove_result,
	    gfm_name_success_op_connection_free,
	    NULL));
}

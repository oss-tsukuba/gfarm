#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

struct gfm_symlink_closure {
	/* input */
	const char *src;
};

static gfarm_error_t
gfm_symlink_request(struct gfm_connection *gfm_server, void *closure,
	const char *base)
{
	struct gfm_symlink_closure *c = closure;
	gfarm_error_t e;

	if ((e = gfm_client_symlink_request(gfm_server, c->src, base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000155,
		    "symlink(%s, %s) request: %s", c->src, base,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_symlink_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e;

	if ((e = gfm_client_symlink_result(gfm_server)) != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1000156,
		    "symlink result: %s", gfarm_error_string(e));
#endif
	}
	return (e);
}

gfarm_error_t
gfs_symlink(const char *src, const char *path)
{
	struct gfm_symlink_closure closure;

	closure.src = src;
	return (gfm_name_op(path, GFARM_ERR_OPERATION_NOT_PERMITTED,
	    gfm_symlink_request,
	    gfm_symlink_result,
	    gfm_name_success_op_connection_free,
	    &closure));
}

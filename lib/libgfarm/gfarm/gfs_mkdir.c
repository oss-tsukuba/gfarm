#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

struct gfm_mkdir_closure {
	/* input */
	gfarm_mode_t mode;
};

static gfarm_error_t
gfm_mkdir_request(struct gfm_connection *gfm_server, void *closure,
	const char *base)
{
	struct gfm_mkdir_closure *c = closure;
	gfarm_error_t e;

	if ((e = gfm_client_mkdir_request(gfm_server, base, c->mode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000133, "mkdir(%s) request: %s",
		    base, gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_mkdir_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e;

	if ((e = gfm_client_mkdir_result(gfm_server)) != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1000134,
		    "mkdir() result: %s", gfarm_error_string(e));
#endif
	}
	return (e);
}

static int
gfm_mkdir_must_be_warned(gfarm_error_t e, void *closure)
{
	/* error in inode_lookup_basename() */
	return (e == GFARM_ERR_ALREADY_EXISTS);
}

gfarm_error_t
gfs_mkdir(const char *path, gfarm_mode_t mode)
{
	struct gfm_mkdir_closure closure;

	closure.mode = mode;
	return (gfm_name_op_modifiable(path, GFARM_ERR_ALREADY_EXISTS,
	    gfm_mkdir_request,
	    gfm_mkdir_result,
	    gfm_name_success_op_connection_free,
	    gfm_mkdir_must_be_warned, &closure));
}

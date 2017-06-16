#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "context.h"
#include "gfm_client.h"
#include "lookup.h"

struct gfm_remove_closure {
	/* input */
	const char *path;	/* for gfarm_file_trace */
};

static gfarm_error_t
gfm_remove_request(struct gfm_connection *gfm_server, void *closure,
	const char *base)
{
	gfarm_error_t e;

	if ((e = gfm_client_remove_request(gfm_server, base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1000137,
		    "remove request: %s", gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfm_remove_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_remove_closure *c = closure;
	gfarm_error_t e;

	if ((e = gfm_client_remove_result(gfm_server)) != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
		gflog_debug(GFARM_MSG_1000138,
		    "remove result: %s", gfarm_error_string(e));
#endif
	} else {
		if (gfarm_ctxp->file_trace) {
			int src_port;

			gfm_client_source_port(gfm_server, &src_port);
			gflog_trace(GFARM_MSG_1003270,
			    "%s/%s/%s/%d/DELETE/%s/%d/////\"%s\"///",
			    gfarm_get_local_username(),
			    gfm_client_username(gfm_server),
			    gfarm_host_get_self_name(), src_port,
			    gfm_client_hostname(gfm_server),
			    gfm_client_port(gfm_server),
			    c->path);
		}
	}
	return (e);
}

static int
gfm_remove_must_be_warned(gfarm_error_t e, void *closure)
{
	/* error returned inode_lookup_basename */
	return (0);
}

gfarm_error_t
gfs_remove(const char *path)
{
	struct gfm_remove_closure closure;

	closure.path = path;
	return (gfm_name_op_modifiable(path,
	    GFARM_ERR_DEVICE_BUSY,
	    gfm_remove_request,
	    gfm_remove_result,
	    gfm_name_success_op_connection_free,
	    gfm_remove_must_be_warned,
	    &closure));
}

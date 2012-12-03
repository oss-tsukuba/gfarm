#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "context.h"
#include "gfm_client.h"
#include "lookup.h"

struct gfm_rename_closure {
	/* input, for gfarm_file_trace */
	const char *src;
	const char *dst;
};

static gfarm_error_t
gfm_rename_request(struct gfm_connection *gfm_server, void *closure,
	const char *sname, const char *dname)
{
	gfarm_error_t e = gfm_client_rename_request(gfm_server, sname, dname);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002670,
		    "rename(%s, %s) request: %s", sname, dname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_rename_result(struct gfm_connection *gfm_server, void *closure)
{
	int src_port;
	struct gfm_rename_closure *c = closure;

	gfarm_error_t e = gfm_client_rename_result(gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002671,
		    "rename result: %s",
		    gfarm_error_string(e));
	} else {
		if (gfarm_ctxp->file_trace) {
			gfm_client_source_port(gfm_server, &src_port);
			gflog_trace(GFARM_MSG_1003271,
			    "%s/%s/%s/%d/MOVE/%s/%d/////\"%s\"///\"%s\"",
			    gfarm_get_local_username(),
			    gfm_client_username(gfm_server),
			    gfarm_host_get_self_name(), src_port,
			    gfm_client_hostname(gfm_server),
			    gfm_client_port(gfm_server),
			    c->src, c->dst);
		}
	}
	return (e);
}

static int
gfm_rename_must_be_warned(gfarm_error_t e, void *closure)
{
	/* error returned from inode_lookup_basename() */
	return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
}

gfarm_error_t
gfs_rename(const char *src, const char *dst)
{
	struct gfm_rename_closure closure;
	gfarm_error_t e;

	closure.src = src;
	closure.dst = dst;

	e = gfm_name2_op_modifiable(src, dst, GFARM_FILE_SYMLINK_NO_FOLLOW,
	    NULL, gfm_rename_request, gfm_rename_result,
	    gfm_name2_success_op_connection_free, NULL,
	    gfm_rename_must_be_warned, &closure);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_PATH_IS_ROOT)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001382,
			"error occurred during gfs_rename(%s)(%s): %s",
			src, dst,
			gfarm_error_string(e));
	}
	return (e);
}


#include <stddef.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "lookup.h"

struct gfm_conn_follow_closure {
	struct gfm_connection *gfm_server;
};

static gfarm_error_t
gfm_conn_follow_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfm_conn_follow_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_conn_follow_closure *c = closure;
	gfarm_error_t e;

	e = gfm_client_connection_addref(gfm_server);
	if (e == GFARM_ERR_NO_ERROR)
		c->gfm_server = gfm_server;
	return (e);
}

gfarm_error_t
gfm_client_connection_and_process_acquire_by_path_follow(const char *path,
	struct gfm_connection **gfm_serverp)
{
	struct gfm_conn_follow_closure closure;
	gfarm_error_t e;

	e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_conn_follow_request,
	    gfm_conn_follow_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	if (e == GFARM_ERR_NO_ERROR) {
		*gfm_serverp = closure.gfm_server;
	} else {
		gflog_debug(GFARM_MSG_1003266,
			"gfm_client_connection_and_process_acquire_by_path"
			"_follow: gfm_inode_op(%s): %s",
			path, gfarm_error_string(e));
	}

	return (e);
}

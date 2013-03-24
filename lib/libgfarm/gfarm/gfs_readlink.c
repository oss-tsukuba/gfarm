#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

struct gfm_readlink_closure {
	char **srcp;
};

static gfarm_error_t
gfm_readlink_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	gfarm_error_t e = gfm_client_readlink_request(gfm_server, ctx);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000135,
		    "readlink request; %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_readlink_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_readlink_closure *c = closure;
	gfarm_error_t e = gfm_client_readlink_result(gfm_server, ctx, c->srcp);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000136,
		    "readlink result; %s", gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_readlink(const char *path, char **srcp)
{
	struct gfm_readlink_closure closure;

	closure.srcp = srcp;
	return (gfm_inode_op_no_follow_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_readlink_request,
	    gfm_readlink_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure));
}

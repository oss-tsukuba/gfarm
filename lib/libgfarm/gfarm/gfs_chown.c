#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "context.h"
#include "gfs_profile.h"
#include "gfm_client.h"
#include "lookup.h"

struct gfm_chown_closure {
	const char *username, *groupname;
};

static gfarm_error_t
gfm_chown_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_chown_closure *c = closure;
	gfarm_error_t e = gfm_client_fchown_request(gfm_server, ctx,
	    c->username, c->groupname);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000116,
		    "fchown_fd request; %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_chown_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	gfarm_error_t e = gfm_client_fchown_result(gfm_server, ctx);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000117,
		    "fchown result; %s", gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_chown(const char *path, const char *username, const char *groupname)
{
	struct gfm_chown_closure closure;

	closure.username = username;
	closure.groupname = groupname;
	return (gfm_inode_op_modifiable(path, GFARM_FILE_LOOKUP,
	    gfm_chown_request,
	    gfm_chown_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure));
}

gfarm_error_t
gfs_lchown(const char *path, const char *username, const char *groupname)
{
	struct gfm_chown_closure closure;

	closure.username = username;
	closure.groupname = groupname;
	return (gfm_inode_op_no_follow_modifiable(path, GFARM_FILE_LOOKUP,
	    gfm_chown_request,
	    gfm_chown_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure));
}

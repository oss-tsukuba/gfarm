#include <stddef.h>
#include <unistd.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"
#include "gfs_attrplus.h"

struct gfm_getattrplus_closure {
	char **patterns;
	int npatterns, flags;

	struct gfs_stat *st;
	int *nattrsp;
	char ***attrnamesp;
	void ***attrvaluesp;
	size_t **attrsizesp;
};

static gfarm_error_t
gfm_getattrplus_request(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_getattrplus_closure *c = closure;
	gfarm_error_t e = gfm_client_fgetattrplus_request(gfm_server, ctx,
		c->patterns, c->npatterns, c->flags);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1002469,
		    "fgetattrplus request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_getattrplus_result(struct gfm_connection *gfm_server,
	struct gfp_xdr_context *ctx, void *closure)
{
	struct gfm_getattrplus_closure *c = closure;
	gfarm_error_t e = gfm_client_fgetattrplus_result(gfm_server, ctx,
		c->st, c->nattrsp,
		c->attrnamesp, c->attrvaluesp, c->attrsizesp);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002470,
		    "fgetattrplus result; %s", gfarm_error_string(e));
#endif
	return (e);
}

static gfarm_error_t
gfs_getattrplus0(
	const char *path, char **patterns, int npatterns, int flags,
	int cflags, struct gfs_stat *st, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	struct gfm_getattrplus_closure closure;
	gfarm_error_t e;

	closure.patterns = patterns;
	closure.npatterns = npatterns;
	closure.flags = flags;

	closure.st = st;
	closure.nattrsp = nattrsp;
	closure.attrnamesp = attrnamesp;
	closure.attrvaluesp = attrvaluesp;
	closure.attrsizesp = attrsizesp;

	e = gfm_inode_op_readonly(path, cflags|GFARM_FILE_LOOKUP,
	    gfm_getattrplus_request,
	    gfm_getattrplus_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002471,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_getattrplus(
	const char *path, char **patterns, int npatterns, int flags,
	struct gfs_stat *st, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	return (gfs_getattrplus0(path, patterns, npatterns, flags, 0,
	    st, nattrsp, attrnamesp, attrvaluesp, attrsizesp));
}

gfarm_error_t
gfs_lgetattrplus(
	const char *path, char **patterns, int npatterns, int flags,
	struct gfs_stat *st, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	return (gfs_getattrplus0(path, patterns, npatterns, flags,
	    GFARM_FILE_SYMLINK_NO_FOLLOW,
	    st, nattrsp, attrnamesp, attrvaluesp, attrsizesp));
}

#include <stddef.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "quota_info.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_dirquota.h"

struct gfm_dirquota_add_closure {
	const char *username;
	const char *dirsetname;
};

static gfarm_error_t
gfm_dirquota_add_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_dirquota_add_closure *c = closure;
	gfarm_error_t e = gfm_client_quota_dir_set_request(gfm_server,
	    c->username, c->dirsetname);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "quota_dir_set request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_dirquota_add_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_quota_dir_set_result(gfm_server);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "quota_dir_set result: %s", gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_dirquota_add(const char *path,
	const char *username, const char *dirsetname)
{
	struct gfm_dirquota_add_closure closure;
	gfarm_error_t e;

	closure.username = username;
	closure.dirsetname = dirsetname;
	e = gfm_inode_op_modifiable(path, GFARM_FILE_LOOKUP,
	    gfm_dirquota_add_request,
	    gfm_dirquota_add_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfs_dirquota_add(%s) failed: %s",
		    path, gfarm_error_string(e));

	return (e);
}


struct gfm_dirquota_get_closure {
	struct gfarm_dirset_info *dirset_info;
	struct gfarm_quota_limit_info *limit_info;
	struct gfarm_quota_subject_info *usage_info;
	struct gfarm_quota_subject_time *grace_info;
	gfarm_uint64_t flags;
};

static gfarm_error_t
gfm_dirquota_get_request(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_quota_dir_get_request(gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "quota_dir_get request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_dirquota_get_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_dirquota_get_closure *c = closure;
	gfarm_error_t e = gfm_client_quota_dir_get_result(gfm_server,
	    c->dirset_info, c->limit_info, c->usage_info, c->grace_info,
	    &c->flags);

#if 0 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "quota_dir_get result: %s", gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_dirquota_get(const char *path,
	struct gfarm_dirset_info *dirset_info,
	struct gfarm_quota_limit_info *limit_info,
	struct gfarm_quota_subject_info *usage_info,
	struct gfarm_quota_subject_time *grace_info,
	gfarm_uint64_t *flagsp)
{
	struct gfm_dirquota_get_closure closure;
	gfarm_error_t e;

	closure.dirset_info = dirset_info;
	closure.limit_info = limit_info;
	closure.usage_info = usage_info;
	closure.grace_info = grace_info;
	e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_dirquota_get_request,
	    gfm_dirquota_get_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfs_dirquota_get(%s) failed: %s",
		    path, gfarm_error_string(e));
	else
		*flagsp = closure.flags;

	return (e);
}

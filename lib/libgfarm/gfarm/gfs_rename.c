#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

static gfarm_error_t
gfm_rename_request(struct gfm_connection *gfm_server, void *closure,
	const char *sname, const char *dname)
{
	gfarm_error_t e = gfm_client_rename_request(gfm_server, sname, dname);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "rename(%s, %s) request: %s", sname, dname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_rename_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_rename_result(gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED,
		    "rename result: %s",
		    gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_rename(const char *src, const char *dst)
{
	gfarm_error_t e = gfm_name2_op(src, dst,
	    GFARM_FILE_SYMLINK_NO_FOLLOW,
	    NULL, gfm_rename_request, gfm_rename_result,
	    gfm_name_success_op_connection_free, NULL, NULL);
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


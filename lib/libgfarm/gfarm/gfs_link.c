#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

static gfarm_error_t
gfm_link_request(struct gfm_connection *gfm_server, void *closure,
	const char *dname)
{
	gfarm_error_t e = gfm_client_flink_request(gfm_server, dname);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002664,
		    "link(%s) request: %s", dname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_link_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_flink_result(gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002665,
		    "link result: %s",
		    gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_link(const char *src, const char *dst)
{
	gfarm_error_t e = gfm_name2_op(src, dst,
	    GFARM_FILE_SYMLINK_NO_FOLLOW | GFARM_FILE_OPEN_LAST_COMPONENT,
	    gfm_link_request, NULL, gfm_link_result,
	    gfm_name_success_op_connection_free, NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_PATH_IS_ROOT)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001376,
			"Creation of link (%s)(%s) failed: %s",
			src, dst,
			gfarm_error_string(e));
	}
	return (e);
}


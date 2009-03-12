#include <stddef.h>
#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_readlink(const char *path, char **srcp)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_request(gfarm_metadb_server, path,
	    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
		gflog_warning("tmp_open(%s) request: %s", path,
		    gfarm_error_string(e));
	else if ((e = gfm_client_readlink_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("readlink request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_result(gfarm_metadb_server, path, NULL))
	    != GFARM_ERR_NO_ERROR)
#if 0
		gflog_warning("tmp_open(%s) result: %s", path,
		    gfarm_error_string(e));
#else
		;
#endif
	else if ((e = gfm_client_readlink_result(gfarm_metadb_server, srcp))
	    != GFARM_ERR_NO_ERROR)
#if 0
		gflog_warning("readlink result: %s",
		    gfarm_error_string(e));
#else
		;
#endif
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));
		free(*srcp);
	}
	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e);
}

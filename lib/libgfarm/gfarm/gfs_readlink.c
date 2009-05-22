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
	struct gfm_connection *gfm_server;
	int retry = 0;

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_tmp_open_request(gfm_server, path,
		    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_readlink_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("readlink request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_tmp_open_result(gfm_server, path, NULL))
		    != GFARM_ERR_NO_ERROR)
#if 0
			gflog_warning("tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
#else
			;
#endif
		else if ((e = gfm_client_readlink_result(gfm_server, srcp))
		    != GFARM_ERR_NO_ERROR)
#if 0
			gflog_warning("readlink result: %s",
			    gfarm_error_string(e));
#else
			;
#endif
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
			free(*srcp);
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e);
}

#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "gfs_profile.h"
#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_chown(const char *path, const char *username, const char *groupname)
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
		else if ((e = gfm_client_fchown_request(gfm_server,
		    username, groupname)) != GFARM_ERR_NO_ERROR)
			gflog_warning("fchown request: %s",
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
		else if ((e = gfm_client_fchown_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
#if 0
			gflog_warning("fchown result: %s",
			    gfarm_error_string(e));
#else
			;
#endif
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e);
}

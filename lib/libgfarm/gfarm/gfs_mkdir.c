#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_mkdir(const char *path, gfarm_mode_t mode)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	struct gfm_connection *gfm_server;
	int retry = 0;
	const char *base;

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_lookup_dir_request(gfm_server, path, &base))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("lookup_dir(%s) request: %s", path,
			    gfarm_error_string(e));
		else {
			if (base[0] == '/' && base[1] == '\0') /* "/" is special */
				e_save = GFARM_ERR_ALREADY_EXISTS;
			else if ((e = gfm_client_mkdir_request(gfm_server,
			    base, mode)) != GFARM_ERR_NO_ERROR)
				gflog_warning("mkdir request: %s",
				    gfarm_error_string(e));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_request(gfm_server))
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
		} else if ((e = gfm_lookup_dir_result(gfm_server, path, &base))
		    != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
			gflog_warning("lookup_dir(%s) result: %s", path,
			    gfarm_error_string(e));
#else
			;
#endif
		else {
			if (base[0] == '/' && base[1] == '\0') /* "/" is special */
				e_save = GFARM_ERR_ALREADY_EXISTS;
			else if ((e = gfm_client_mkdir_result(gfm_server))
			    != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
				gflog_warning("mkdir result: %s",
				    gfarm_error_string(e));
#else
				;
#endif
		}

		break;
	}
	if (e == GFARM_ERR_NO_ERROR &&
	    (e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}

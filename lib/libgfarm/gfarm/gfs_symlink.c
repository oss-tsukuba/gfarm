#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_symlink(const char *src, const char *dst)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	struct gfm_connection *gfm_server;
	int retry = 0;
	const char *dbase;

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_lookup_dir_request(gfm_server,
		    dst, &dbase)) != GFARM_ERR_NO_ERROR)
			gflog_warning("lookup_dir(%s) request: %s", dst,
			    gfarm_error_string(e));
		else {
			/* "/" is special */
			if (dbase[0] == '/' && dbase[1] == '\0')
				e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
			else if ((e = gfm_client_symlink_request(
			    gfm_server, src, dbase)) != GFARM_ERR_NO_ERROR)
				gflog_warning("symlink request: %s",
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
		} else if ((e = gfm_lookup_dir_result(gfm_server,
		    dst, &dbase)) != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
			gflog_warning("lookup_dir(%s) result: %s", dst,
			    gfarm_error_string(e));
#else
			;
#endif
		else {
			/* "/" is special */
			if (dbase[0] == '/' && dbase[1] == '\0')
				e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
			else if ((e = gfm_client_symlink_result(
			    gfm_server)) != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
				gflog_warning("symlink result: %s",
				    gfarm_error_string(e));
#else
			;
#endif
		}
		break;
	}
	if (e == GFARM_ERR_NO_ERROR &&
	    (e = gfm_client_compound_end_result(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}

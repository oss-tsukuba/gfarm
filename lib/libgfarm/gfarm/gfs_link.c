#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_link(const char *src, const char *dst)
{
	gfarm_error_t e, e_save;
	int retry = 0;
	struct gfm_connection *sgfmd, *dgfmd;
	const char *spath, *dpath, *dbase;

	for (;;) {
		e_save = GFARM_ERR_NO_ERROR;
		spath = src;
		dpath = dst;

		if ((e = gfarm_url_parse_metadb(&spath, &sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("url_parse_metadb(%s): %s", src,
			    gfarm_error_string(e));
			return (e);
		} else if ((e = gfarm_url_parse_metadb(&dpath, &dgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("url_parse_metadb(%s): %s", dst,
			    gfarm_error_string(e));
			gfm_client_connection_free(sgfmd);
			return (e);
		} else if (sgfmd != dgfmd) {
			gfm_client_connection_free(dgfmd);
			gfm_client_connection_free(sgfmd);
			return (GFARM_ERR_CROSS_DEVICE_LINK);
		}


		if ((e = gfm_tmp_open_request(sgfmd, spath, GFARM_FILE_LOOKUP))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("tmp_open(%s) request: %s", src,
			    gfarm_error_string(e));
		} else if ((e = gfm_client_save_fd_request(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("save_fd request: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_lookup_dir_request(dgfmd, dpath, &dbase))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("lookup_dir(%s) request: %s", dst,
			    gfarm_error_string(e));
		} else if (dbase[0] == '/' && dbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = gfm_client_flink_request(dgfmd, dbase))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("flink request: %s",
			    gfarm_error_string(e));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_request(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		} else if ((e = gfm_tmp_open_result(sgfmd, spath, NULL))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(dgfmd);
				gfm_client_connection_free(sgfmd);
				continue;
			}
#if 0 /* DEBUG */
			gflog_debug("tmp_open(%s) result: %s", src,
			    gfarm_error_string(e));
#endif
		} else if ((e = gfm_client_save_fd_result(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("save_fd result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_lookup_dir_result(dgfmd, dpath, &dbase))
		    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
			gflog_debug("lookup_dir(%s) result: %s", dst,
			    gfarm_error_string(e));
#endif
		} else if (dbase[0] == '/' && dbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = gfm_client_flink_result(dgfmd))
		    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
			gflog_debug("flink result: %s",
			    gfarm_error_string(e));
#endif
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_result(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(dgfmd);
	gfm_client_connection_free(sgfmd);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}

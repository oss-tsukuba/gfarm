#include <stddef.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_rename(const char *src, const char *dst)
{
	gfarm_error_t e, e_save;
	int retry = 0;
	struct gfm_connection *sgfmd, *dgfmd;
	const char *spath, *dpath, *sbase, *dbase;

	for (;;) {
		e_save = GFARM_ERR_NO_ERROR;
		spath = src;
		dpath = dst;

		if ((e = gfarm_url_parse_metadb(&spath, &sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000139,
			    "url_parse_metadb(%s): %s", src,
			    gfarm_error_string(e));
			return (e);
		} else if ((e = gfarm_url_parse_metadb(&dpath, &dgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000140,
			    "url_parse_metadb(%s): %s", dst,
			    gfarm_error_string(e));
			gfm_client_connection_free(sgfmd);
			return (e);
		} else if (sgfmd != dgfmd) {
			gfm_client_connection_free(dgfmd);
			gfm_client_connection_free(sgfmd);
			return (GFARM_ERR_CROSS_DEVICE_LINK);
		}

		if ((e = gfm_tmp_lookup_parent_request(sgfmd, spath, &sbase))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000141,
			    "tmp_lookup_parent(%s) request: %s", src,
			    gfarm_error_string(e));
		} else if (sbase[0] == '/' && sbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = gfm_client_save_fd_request(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000142, "save_fd request: %s",
			    gfarm_error_string(e));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_lookup_dir_request(dgfmd, dpath, &dbase))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000143,
			    "lookup_dir(%s) request: %s", dst,
			    gfarm_error_string(e));
		} else if (dbase[0] == '/' && dbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (e_save == GFARM_ERR_NO_ERROR &&
		    (e = gfm_client_rename_request(sgfmd, sbase, dbase))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000144, "rename request: %s",
			    gfarm_error_string(e));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_request(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000145,
			    "compound_end request: %s",
			    gfarm_error_string(e));

		} else if ((e = gfm_tmp_lookup_parent_result(sgfmd, spath,
		    &sbase)) != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(dgfmd);
				gfm_client_connection_free(sgfmd);
				continue;
			}
#if 0 /* DEBUG */
			gflog_debug(GFARM_MSG_1000146,
			    "tmp_lookup_parent(%s) result: %s", src,
			    gfarm_error_string(e));
#endif
		} else if (sbase[0] == '/' && sbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = gfm_client_save_fd_result(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000147, "save_fd result: %s",
			    gfarm_error_string(e));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_lookup_dir_result(dgfmd, dpath, &dbase))
		    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
			gflog_debug(GFARM_MSG_1000148,
			    "lookup_dir(%s) result: %s", dst,
			    gfarm_error_string(e));
#endif
		} else if (dbase[0] == '/' && dbase[1] == '\0') {
			/* "/" is special */
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (e_save == GFARM_ERR_NO_ERROR &&
		    (e = gfm_client_rename_result(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
#if 0 /* DEBUG */
			gflog_debug(GFARM_MSG_1000149,
			    "rename(%s, %s) result: %s", src, dst,
			    gfarm_error_string(e));
#endif
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;

		if ((e = gfm_client_compound_end_result(sgfmd))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1000150,
			    "compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(dgfmd);
	gfm_client_connection_free(sgfmd);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}

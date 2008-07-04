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
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	const char *dbase;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_request(gfarm_metadb_server, src,
	    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
		gflog_warning("tmp_open(%s) request: %s", src,
		    gfarm_error_string(e));
	else if ((e = gfm_client_save_fd_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("save_fd request: %s", gfarm_error_string(e));
	else if ((e = gfm_lookup_dir_request(gfarm_metadb_server,
	    dst, &dbase)) != GFARM_ERR_NO_ERROR)
		gflog_warning("lookup_dir(%s) request: %s", dst,
		    gfarm_error_string(e));
	else {
		/* "/" is special */
		if (dbase[0] == '/' && dbase[1] == '\0')
			e_save = GFARM_ERR_OPERATION_NOT_PERMITTED;
		else if ((e = gfm_client_flink_request(
		    gfarm_metadb_server, dbase)) != GFARM_ERR_NO_ERROR)
			gflog_warning("flink request: %s",
			    gfarm_error_string(e));
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
			
	if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_result(gfarm_metadb_server, src, NULL))
	    != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
		gflog_warning("tmp_open(%s) result: %s", src,
		    gfarm_error_string(e));
#else
		;
#endif
	else if ((e = gfm_client_save_fd_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("mkdir result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_lookup_dir_result(gfarm_metadb_server,
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
		else if ((e = gfm_client_flink_result(
		    gfarm_metadb_server)) != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
			gflog_warning("flink result: %s",
			    gfarm_error_string(e));
#else
		;
#endif
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
}

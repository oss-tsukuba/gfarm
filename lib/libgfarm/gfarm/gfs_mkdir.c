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
	const char *base;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_lookup_dir_request(gfarm_metadb_server, path, &base))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("lookup_dir(%s) request: %s", path,
		    gfarm_error_string(e));
	else {
		if (base[0] == '/' && base[1] == '\0') /* "/" is special */
			e_save = GFARM_ERR_ALREADY_EXISTS;
		else if ((e = gfm_client_mkdir_request(gfarm_metadb_server,
		    base, mode)) != GFARM_ERR_NO_ERROR)
			gflog_warning("mkdir request: %s",
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
	else if ((e = gfm_lookup_dir_result(gfarm_metadb_server, path, &base))
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
		else if ((e = gfm_client_mkdir_result(gfarm_metadb_server))
		    != GFARM_ERR_NO_ERROR)
#if 0 /* DEBUG */
			gflog_warning("mkdir result: %s",
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

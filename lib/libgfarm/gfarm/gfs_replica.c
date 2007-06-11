/*
 * $Id$
 */

#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

gfarm_error_t
gfs_replica_list_by_name(const char *path, int *np, char ***hostsp)
{
	gfarm_error_t e;
	gfarm_int32_t n;
	char **hosts;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_request(gfarm_metadb_server, path,
	    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
		gflog_warning("tmp_open(%s) request: %s", path,
		    gfarm_error_string(e));
	else if ((e = gfm_client_replica_list_by_name_request(
			  gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("replica_list_by_name request: %s",
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
		gflog_warning("tmp_open(%s) result: %s", path,
		    gfarm_error_string(e));
	else if ((e = gfm_client_replica_list_by_name_result(
			  gfarm_metadb_server, &n, &hosts))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("replica_list_by_name result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));
		while (--n >= 0)
			free(hosts[n]);
		free(hosts);
	}
	/* NOTE: the opened descriptor is automatically closed by gfmd */

	if (e == GFARM_ERR_NO_ERROR) {
		*np = n;
		*hostsp = hosts;
	}
	return (e);
}

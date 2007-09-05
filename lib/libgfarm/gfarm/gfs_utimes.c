#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"

gfarm_error_t
gfs_utimes(const char *path, const struct gfarm_timespec *tsp)
{
	gfarm_error_t e;
	struct gfarm_timespec atime, mtime;
	struct timeval now;

	if (tsp == NULL) {
		gettimeofday(&now, NULL);
		atime.tv_sec =
		mtime.tv_sec = now.tv_sec;
		atime.tv_nsec =
		mtime.tv_nsec = now.tv_usec * 1000;
	} else {
		atime = tsp[0];
		mtime = tsp[1];
	}

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_tmp_open_request(gfarm_metadb_server, path,
	    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
		gflog_warning("tmp_open(%s) request: %s", path,
		    gfarm_error_string(e));
	else if ((e = gfm_client_futimes_request(gfarm_metadb_server,
	    atime.tv_sec, atime.tv_nsec, mtime.tv_sec, mtime.tv_nsec))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("futimes request: %s", gfarm_error_string(e));
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
	else if ((e = gfm_client_futimes_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("futimes result: %s", gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));
	}
	/* NOTE: the opened descriptor is automatically closed by gfmd */

	return (e);
}

#include <stddef.h>
#include <unistd.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "gfs_profile.h"
#include "gfm_client.h"
#include "config.h"
#include "lookup.h"
#include "gfs_misc.h"

static double gfs_stat_time;

gfarm_error_t
gfs_stat(const char *path, struct gfs_stat *s)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

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
		else if ((e = gfm_client_fstat_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("fstat request: %s",
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
		else if ((e = gfm_client_fstat_result(gfm_server, s))
		    != GFARM_ERR_NO_ERROR)
#if 0
			gflog_warning("fstat result: %s",
			    gfarm_error_string(e));
#else
			;
#endif
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
			gfs_stat_free(s);
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_stat_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_lstat(const char *path, struct gfs_stat *s)
{
	return (gfs_stat(path, s)); /* XXX FIXME */
}

gfarm_error_t
gfs_fstat(GFS_File gf, struct gfs_stat *s)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = gfs_pio_metadb(gf);

	for (;;) {
		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_request(
				  gfm_server, gfs_pio_fileno(gf)))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_fstat_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("fstat request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_fstat_result(gfm_server, s))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("fstat result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));

		break;
	}

	return (e);
}

void
gfs_stat_display_timers(void)
{
	gflog_info("gfs_stat        : %g sec", gfs_stat_time);
}

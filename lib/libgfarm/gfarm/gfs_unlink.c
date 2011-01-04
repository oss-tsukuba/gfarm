/*
 * $Id$
 */

#include <stddef.h>
#include <unistd.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "gfs_profile.h"

static double gfs_unlink_time;

gfarm_error_t
gfs_unlink(const char *path)
{
	gfarm_error_t e;
	struct gfs_stat st;
	int is_dir;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_lstat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001396,
			"gfs_lstat(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	if (is_dir) {
		gflog_debug(GFARM_MSG_1001397,
			"Not a directory(%s): %s",
			path,
			gfarm_error_string(GFARM_ERR_IS_A_DIRECTORY));
		return (GFARM_ERR_IS_A_DIRECTORY);
	}

	/* XXX FIXME there is race condition here */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_unlink_time += gfarm_timerval_sub(&t2, &t1));

	return (gfs_remove(path));
}

void
gfs_unlink_display_timers(void)
{
	gflog_info(GFARM_MSG_1000157,
	    "gfs_unlink      : %g sec", gfs_unlink_time);
}

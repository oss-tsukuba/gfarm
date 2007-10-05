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

	e = gfs_stat(path, &st);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	if (is_dir)
		return (GFARM_ERR_IS_A_DIRECTORY);

	/* XXX FIXME there is race condition here */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_unlink_time += gfarm_timerval_sub(&t2, &t1));

	return (gfs_remove(path));
}

void
gfs_unlink_display_timers(void)
{
	gflog_info("gfs_unlink      : %g sec", gfs_unlink_time);
}

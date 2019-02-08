/*
 * $Id$
 */

#include <stddef.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "context.h"
#include "gfs_profile.h"

#define staticp	(gfarm_ctxp->gfs_unlink_static)

struct gfarm_gfs_unlink_static {
	double unlink_time;
	unsigned long long unlink_count;
};

gfarm_error_t
gfarm_gfs_unlink_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_unlink_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->unlink_time = 0;
	s->unlink_count = 0;

	ctxp->gfs_unlink_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_unlink_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_unlink_static);
}

gfarm_error_t
gfs_unlink(const char *path)
{
	gfarm_error_t e;
	struct gfs_stat st;
	int is_dir;
	gfarm_timerval_t t1, t2;

	GFARM_KERNEL_UNUSE2(t1, t2);
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
	gfs_profile(staticp->unlink_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->unlink_count++);

	return (gfs_remove(path));
}

struct gfs_profile_list unlink_profile_items[] = {
	{ "unlink_time", "gfs_unlink time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_unlink_static, unlink_time) },
	{ "unlink_count", "gfs_unlink count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_unlink_static, unlink_count) },
};

void
gfs_unlink_display_timers(void)
{
	int n = GFARM_ARRAY_LENGTH(unlink_profile_items);

	gfs_profile_display_timers(n, unlink_profile_items, staticp);
}

gfarm_error_t
gfs_unlink_profile_value(const char *name, char *value, size_t *sizep)
{
	int n = GFARM_ARRAY_LENGTH(unlink_profile_items);

	return (gfs_profile_value(name, n, unlink_profile_items,
		    staticp, value, sizep));
}

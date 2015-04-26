/*
 * $Id$
 */

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>

#include "nanosec.h"

void
gfarm_nanosleep_by_timespec(const struct timespec *tsp)
{
	struct timespec req = *tsp, rem;

	for (;;) {
		if (nanosleep(&req, &rem) == 0)
			break;
		if (errno != EINTR) {
			gflog_warning(GFARM_MSG_1003547, "nanosleep(): %s",
			    strerror(errno));
			break;
		}
		/* ignore EINTR */
		req = rem;
	}
}

void
gfarm_nanosleep(unsigned long long nsec)
{
	struct timespec ts;

	ts.tv_sec = nsec / GFARM_SECOND_BY_NANOSEC;
	ts.tv_nsec = nsec % GFARM_SECOND_BY_NANOSEC;
	gfarm_nanosleep_by_timespec(&ts);
}

void
gfarm_gettime(struct timespec *ts)
{
	int rv;
	static int gettimeofday_ok = 1;

#ifdef HAVE_CLOCK_GETTIME
	static int clock_gettime_ok = 1;

	if (clock_gettime_ok) {
		rv = clock_gettime(CLOCK_REALTIME, ts);
		if (rv == 0)
			return;
		gflog_notice_errno(GFARM_MSG_1004227, "clock_gettime");
		clock_gettime_ok = 0;
	}
#endif
	if (gettimeofday_ok) {
		struct timeval tv;

		rv = gettimeofday(&tv, NULL);
		if (rv == 0) {
			ts->tv_sec = tv.tv_sec;
			ts->tv_nsec = tv.tv_usec * GFARM_MICROSEC_BY_NANOSEC;
			return;
		}
		gflog_notice_errno(GFARM_MSG_1004228, "gettimeofday");
		gettimeofday_ok = 0;
	}
	ts->tv_sec = time(NULL);
	ts->tv_nsec = 0;
}

#ifdef HAVE_UTIMENSAT
#include <fcntl.h>		/* AT_FDCWD, AT_SYMLINK_NOFOLLOW */
#include <sys/stat.h>		/* utimensat */
#else
#include <sys/time.h>		/* utimes */
#include <sys/types.h>		/* lstat */
#include <sys/stat.h>		/* lstat */
#endif

static int
utimens_common(const char *path, const struct timespec ts[2], int no_follow)
{
#ifdef HAVE_UTIMENSAT
	return (utimensat(
	    AT_FDCWD, path, ts, no_follow ? AT_SYMLINK_NOFOLLOW : 0));
#else
	struct timeval tv[2];

	if (no_follow) {
		struct stat st;

		if (lstat(path, &st) != 0)
			return (-1);
		if (S_ISLNK(st.st_mode)) {
			errno = EOPNOTSUPP;
			return (-1);
		}
	}
	if (ts) {
		tv[0].tv_sec = ts[0].tv_sec;
		tv[0].tv_usec = ts[0].tv_nsec / GFARM_MICROSEC_BY_NANOSEC;
		tv[1].tv_sec = ts[1].tv_sec;
		tv[1].tv_usec = ts[1].tv_nsec / GFARM_MICROSEC_BY_NANOSEC;
		return (utimes(path, tv));
	} else
		return (utimes(path, NULL));
#endif
}

/* support errno */
int
gfarm_local_lutimens(const char *path, const struct timespec ts[2])
{
	return (utimens_common(path, ts, 1));
}

/* support errno */
int
gfarm_local_utimens(const char *path, const struct timespec ts[2])
{
	return (utimens_common(path, ts, 0));
}

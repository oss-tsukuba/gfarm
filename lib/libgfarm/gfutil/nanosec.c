/*
 * $Id$
 */

#include <errno.h>
#include <string.h>
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
#ifdef HAVE_CLOCK_GETTIME
	clock_gettime(CLOCK_REALTIME, ts);
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * GFARM_MICROSEC_BY_NANOSEC;
#endif
}

/*
 * $Id$
 */

#include <stdio.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "nanosec.h"
#include "timespec.h"

#define GFARM_SECOND_BY_NANOSEC	1000000000

int
gfarm_timespec_cmp(
	const struct gfarm_timespec *t1, const struct gfarm_timespec *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return (1);
	if (t1->tv_sec < t2->tv_sec)
		return (-1);
	if (t1->tv_nsec > t2->tv_nsec)
		return (1);
	if (t1->tv_nsec < t2->tv_nsec)
		return (-1);
	return (0);
}

static void
gfarm_timespec_normalize(struct gfarm_timespec *t)
{
	long n;

	if (t->tv_nsec >= GFARM_SECOND_BY_NANOSEC) {
		n = t->tv_nsec / GFARM_SECOND_BY_NANOSEC;
		t->tv_nsec -= n * GFARM_SECOND_BY_NANOSEC;
		t->tv_sec += n;
	} else if (t->tv_nsec < 0) {
		n = -t->tv_nsec / GFARM_SECOND_BY_NANOSEC + 1;
		t->tv_nsec += n * GFARM_SECOND_BY_NANOSEC;
		t->tv_sec -= n;
	}
}

void
gfarm_timespec_add(struct gfarm_timespec *t1, const struct gfarm_timespec *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_nsec += t2->tv_nsec;
	gfarm_timespec_normalize(t1);
}

void
gfarm_timespec_sub(struct gfarm_timespec *t1, const struct gfarm_timespec *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	gfarm_timespec_normalize(t1);
}

void
gfarm_timespec_add_nanosec(struct gfarm_timespec *t, long nanosec)
{
	t->tv_nsec += nanosec;
	gfarm_timespec_normalize(t);
}

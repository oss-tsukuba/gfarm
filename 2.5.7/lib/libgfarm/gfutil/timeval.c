#include <sys/time.h>
#include <unistd.h>

#include "gfutil.h"

int
gfarm_timeval_cmp(const struct timeval *t1, const struct timeval *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return (1);
	if (t1->tv_sec < t2->tv_sec)
		return (-1);
	if (t1->tv_usec > t2->tv_usec)
		return (1);
	if (t1->tv_usec < t2->tv_usec)
		return (-1);
	return (0);
}

static void
gfarm_timeval_normalize(struct timeval *t)
{
	long n;

	if (t->tv_usec >= GFARM_SECOND_BY_MICROSEC) {
		n = t->tv_usec / GFARM_SECOND_BY_MICROSEC;
		t->tv_usec -= n * GFARM_SECOND_BY_MICROSEC;
		t->tv_sec += n;
	} else if (t->tv_usec < 0) {
		n = -t->tv_usec / GFARM_SECOND_BY_MICROSEC + 1;
		t->tv_usec += n * GFARM_SECOND_BY_MICROSEC;
		t->tv_sec -= n;
	}
}

void
gfarm_timeval_add(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	gfarm_timeval_normalize(t1);
}

void
gfarm_timeval_sub(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	gfarm_timeval_normalize(t1);
}

void
gfarm_timeval_add_microsec(struct timeval *t, long microsec)
{
	t->tv_usec += microsec;
	gfarm_timeval_normalize(t);
}

int
gfarm_timeval_is_expired(const struct timeval *expiration)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return (gfarm_timeval_cmp(&now, expiration) > 0);
}

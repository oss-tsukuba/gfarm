/*
 * $Id$
 *
 * TIMER routine
 *
 * gfarm_timerval_t t1, t2;
 *
 * gfarm_timerval_calibrate();
 *
 * gfarm_gattimerval(&t1);
 * ...
 * gfarm_gattimerval(&t2);
 *
 * printf("%g sec\n", gfarm_timerval_sub(&t2, &t1));
 */

#include <sys/time.h>
#include "timer.h"

#if 0

#include <pthread.h>
#include <unistd.h>

double gfarm_timerval_calibration;

unsigned long long
gfarm_get_cycles(void)
{
	unsigned long long rv;

	__asm __volatile("rdtsc" : "=A" (rv));
	return (rv);
}

static void
gfarm_timerval_calibrate_once(void)
{
	gfarm_timerval_t t1, t2;
	struct timeval s1, s2;

	/* warming up */
	gfarm_gettimerval(&t1);
	gettimeofday(&s1, NULL);

	gfarm_gettimerval(&t1);
	gettimeofday(&s1, NULL);
	sleep(1);
	gfarm_gettimerval(&t2);
	gettimeofday(&s2, NULL);

	gfarm_timerval_calibration =
		((s2.tv_sec - s1.tv_sec) +
		 (s2.tv_usec - s1.tv_usec) * .000001) /
		(t2 - t1);
}

void
gfarm_timerval_calibrate(void)
{
	static pthread_once_t calibrated = PTHREAD_ONCE_INIT;

	pthread_once(&calibrated, gfarm_timerval_calibrate_once);
}

#else /* gettimeofday */

void
gfarm_timerval_calibrate(void)
{}

#endif

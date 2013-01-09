/*
 * $Id$
 *
 * TIMER routine
 */

#ifdef i386

typedef unsigned long long gfarm_timerval_t;
extern double gfarm_timerval_calibration;

unsigned long long gfarm_get_cycles(void);

#define gfarm_gettimerval(tp)		(*(tp) = gfarm_get_cycles())
#define gfarm_timerval_second(tp)	(*(tp) * gfarm_timerval_calibration)
#define gfarm_timerval_sub(t1p, t2p) \
	((*(t1p) - *(t2p)) * gfarm_timerval_calibration)

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
#define GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t)	((t) = 0)
#else
#define GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t)
#endif

#else /* gettimeofday */

typedef struct timeval gfarm_timerval_t;

#define gfarm_gettimerval(t1)		gettimeofday(t1, NULL)
#define gfarm_timerval_second(t1) \
	((double)(t1)->tv_sec + (double)(t1)->tv_usec * .000001)
#define gfarm_timerval_sub(t1, t2) \
	(((double)(t1)->tv_sec - (double)(t2)->tv_sec)	\
	+ ((double)(t1)->tv_usec - (double)(t2)->tv_usec) * .000001)

#define GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t)

#endif

void
gfarm_timerval_calibrate(void);

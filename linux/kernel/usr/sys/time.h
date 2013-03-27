#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_
#include <linux/time.h>
static inline int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
	if (tz)
		*tz = sys_tz;
	if (tv)
		do_gettimeofday(tv);
	return (0);
}
static inline time_t
time(time_t *t)
{
	time_t i = get_seconds();
	if (t)
		*t = i;
	return (i);
}
#endif /* _SYS_TIME_H_ */


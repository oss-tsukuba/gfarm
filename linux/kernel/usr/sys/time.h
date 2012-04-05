#ifndef _TIME_H_
#define _TIME_H_
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
#endif /* _TIME_H_ */


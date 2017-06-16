#include <time.h>
#include <unistd.h>

#include "nanosec.h"
#include "gfutil.h"

void
gfarm_sleep(time_t sec)
{
	struct timespec ts;

	ts.tv_sec = sec;
	ts.tv_nsec = 0;
	gfarm_nanosleep_by_timespec(&ts);
}

/*
 * $Id$
 */

#define GFARM_SECOND_BY_NANOSEC 1000000000

void gfarm_nanosleep_by_timespec(const struct timespec *req);
void gfarm_nanosleep(unsigned long long nsec);

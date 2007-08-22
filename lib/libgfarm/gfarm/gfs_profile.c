/*
 * $Id$
 */

#include "timer.h"

int gf_profile;

void
gfs_profile_set(void)
{
	gf_profile = 1;
	gfarm_timerval_calibrate();
}

void
gfs_profile_unset(void)
{
	gf_profile = 0;
}

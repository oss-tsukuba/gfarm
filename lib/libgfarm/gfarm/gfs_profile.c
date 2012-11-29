/*
 * $Id$
 */

#include <gfarm/error.h>

#include "timer.h"

#include "context.h"

void
gfs_profile_set(void)
{
	gfarm_ctxp->profile = 1;
	gfarm_timerval_calibrate();
}

void
gfs_profile_unset(void)
{
	gfarm_ctxp->profile = 0;
}

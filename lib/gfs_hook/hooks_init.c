/*
 * $Id$
 */

#include <unistd.h>
#include <gfarm/gfarm.h>
#include "hooks_subr.h"

char *
gfs_hook_initialize(void)
{
#if 0
	return "gfs_hook_initialize: Cannot initialize";
#else
	char *e;

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		return e;

	gfs_pio_set_local(0, 1);

	return NULL;
#endif
}

/*
 * $Id$
 */

#include <sys/types.h>
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

	_gfs_hook_debug(fprintf(stderr, "GFS: gfs_hook_initialize\n"));

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		return (e);

	if (gfs_pio_set_local_check() != NULL)
		gfs_pio_set_local(0, 1);

	return (NULL);
#endif
}

/*
 * $Id$
 */

#include <sys/types.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfs_hook.h>
#include "hooks_subr.h"

extern int gf_hook_default_global;

char *
gfs_hook_initialize(void)
{
	char *e;

	_gfs_hook_debug(fprintf(stderr, "GFS: gfs_hook_initialize\n"));

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		return (e);

	if (gf_hook_default_global)
		gfs_hook_set_default_view_global();

	if (gfs_pio_set_local_check() != NULL)
		gfs_pio_set_local(0, 1);

	return (NULL);
}

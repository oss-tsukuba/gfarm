/*
 * $Id$
 */

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfs_hook.h>
#include "hooks_subr.h"

extern int gf_hook_default_global;

char *
gfs_hook_initialize(void)
{
	char *e;
	int fd;

	_gfs_hook_debug(fprintf(stderr, "GFS: gfs_hook_initialize\n"));

	/*
	 * Reserve several file descriptors for applications.  At least,
	 * 'configure' uses 5 and 6.  Maybe, tcsh and zsh also.
	 */
	fd = open("/dev/null", O_RDWR);
	while (fd < 7)
		fd = open("/dev/null", O_RDWR);
	close(fd);

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		return (e);

	if (gf_hook_default_global)
		gfs_hook_set_default_view_global();

	if (gfs_pio_set_local_check() != NULL)
		gfs_pio_set_local(0, 1);

	return (NULL);
}

/*
 * $Id$
 */

#include <unistd.h>
#include <errno.h>
#include <gfarm/gfarm.h>

char *
gfs_execve(const char *filename, char *const argv [], char *const envp[])
{
	char *hostname, *e;
	char **delivered_paths;

	if (!gfarm_is_url(filename)) {
		execve(filename, argv, envp);
		return gfarm_errno_to_error(errno);
	}

	/* XXX - If this host is an active filesystem node */

	e = gfarm_host_get_canonical_self_name(&hostname);
	if (e != NULL)
		return (e);

	e = gfarm_url_program_deliver(filename, 1, &hostname,
				      &delivered_paths);
	if (e != NULL)
		return (e);

	execve(delivered_paths[0], argv, envp);
	return gfarm_errno_to_error(errno);
}

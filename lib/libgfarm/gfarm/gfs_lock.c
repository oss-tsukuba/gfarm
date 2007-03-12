/*
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#include <gfarm/gfarm_misc.h>

#define GFARM_LOCKFILE_SUF	":::lock"

static int
gfs_i_lockfile(char *file, char **lockfile)
{
	char *lfile;

	*lockfile = NULL;
	if (file == NULL)
		return (-1);

	GFARM_MALLOC_ARRAY(lfile, strlen(file) + sizeof(GFARM_LOCKFILE_SUF));
	if (lfile == NULL)
		return (-1);
	sprintf(lfile, "%s%s", file, GFARM_LOCKFILE_SUF);
	*lockfile = lfile;

	return (0);
}

static int
gfs_i_lock(char *file)
{
	char *lockfile;
	int fd, saved_errno;
	mode_t saved_mask;

	if (file == NULL)
		return (-2);
	if (gfs_i_lockfile(file, &lockfile))
		return (-2);
	
	saved_mask = umask(0);
	fd = open(lockfile, O_CREAT | O_EXCL, 0644);
	saved_errno = errno;
	umask(saved_mask);
	free(lockfile);
	if (fd != -1) {
		close(fd);
		return (0);
	}
	else if (saved_errno != EEXIST)
		return (-2); /* other reasons.  cannot lock */

	return (-1);
}

int
gfs_lock_local_path_section(char *localpath)
{
	const struct timespec interval = { 0, 100000000 }; /* 100 msec */
	int r;

	while ((r = gfs_i_lock(localpath)) == -1) {
		nanosleep(&interval, NULL);
	}
	return (r);
}

int
gfs_unlock_local_path_section(char *localpath)
{
	char *lockfile;

	if (gfs_i_lockfile(localpath, &lockfile))
		return (-1);

	unlink(lockfile);
	free(lockfile);
	return (0);
}

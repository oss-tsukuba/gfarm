#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfs.h>
#include "hooks_subr.h"

#define MAX_GFS_FILE_BUF	2048

static GFS_File _gfs_file_buf[MAX_GFS_FILE_BUF];

int
gfs_hook_insert_gfs_file(GFS_File gf)
{
	int fd, save_errno;

	_gfs_hook_debug(fprintf(stderr, "gfs_hook_insert_gfs_file: %p\n", gf));

	fd = dup(gfs_pio_fileno(gf));
	if (fd == -1) {
		save_errno = errno;
		gfs_pio_close(gf);
		errno = save_errno;
		return (-1);
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		__syscall_close(fd);
		gfs_pio_close(gf);
		errno = EMFILE;
		return (-1);
	}
	if (_gfs_file_buf[fd] != NULL) {
		__syscall_close(fd);
		gfs_pio_close(gf);
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
	_gfs_file_buf[fd] = gf;
	return (fd);
}

void
gfs_hook_clear_gfs_file(int fd)
{
	_gfs_hook_debug(fprintf(stderr, "gfs_hook_clear_gfs_file: %d\n", fd));

	_gfs_file_buf[fd] = NULL;
	__syscall_close(fd);
}

/*  printf and puts should not be put into the following function. */
GFS_File
gfs_hook_is_open(int fd)
{
	if (fd >= 0 && fd < MAX_GFS_FILE_BUF)
		return (_gfs_file_buf[fd]);
	return (NULL);
}

int
gfs_hook_is_url(const char *path, const char **urlp)
{
	/*
	 * Objectivity patch:
	 *   '/gfarm:' is also considered as a Gfarm URL
	 */
	if (*path == '/')
		++path;
	if (gfarm_is_url(path)) {
		*urlp = path;
		return (1);
	}
	return (0);
}

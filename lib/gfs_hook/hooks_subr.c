#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "hooks_subr.h"

#define MAX_GFS_FILE_BUF	2048

static GFS_File _gfs_file_buf[MAX_GFS_FILE_BUF];

void
gfs_hook_not_initialized(void)
{
	static int printed = 0;

	if (!printed) {
		printed = 1;
		fprintf(stderr,
			"fatal error: gfarm_initialize() isn't called\n");
	}
}

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

/*
 *  Check whether pathname is gfarm url or not.
 *
 *  Gfarm URL:  gfarm:[:section:]pathname
 *
 *  If *secp is not NULL, it is necessary to free the memory space for
 *  both *urlp and *secp.
 */
int
gfs_hook_is_url(const char *path, char **urlp, char **secp)
{
	*secp = NULL;
	/*
	 * Objectivity patch:
	 *   '/gfarm:' is also considered as a Gfarm URL
	 */
	if (*path == '/')
		++path;
	if (gfarm_is_url(path)) {
		static char prefix[] = "gfarm:";
		if (!gfarm_initialized) {
			gfs_hook_not_initialized();
			return (0); /* don't perform gfarm operation */
		}
		/*
		 * extension for accessing individual sections
		 *   gfarm::section:pathname
		 */
		if (*(path + sizeof(prefix) - 1) == ':') {
			char *loc = strchr(path + sizeof(prefix), ':');
			int urlsize, secsize;
			if (loc == NULL) /* no section or no pathname */
				return (0);
			urlsize = sizeof(prefix) - 1 + strlen(loc + 1);
			secsize = strlen(path) - urlsize - 2;
			*urlp = calloc(urlsize + 1, sizeof(char));
			*secp = calloc(secsize + 1, sizeof(char));
			strcat(*urlp, prefix);
			strcat(*urlp, loc + 1);
			strncpy(*secp, path + sizeof(prefix), secsize);
			/*
			 * This case needs to free memory space of
			 * both *urlp and *secp.
			 */
		}
		else {
			*urlp = path;
		}
		return (1);
	}
	return (0);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "hooks_subr.h"

#define MAX_GFS_FILE_BUF	2048

static struct {
	unsigned char d_type;   /* file types in <gfarm/gfs.h> */
        union {
		GFS_File f;
		struct {
			GFS_Dir dir;
			int readcount;
			struct gfs_dirent *suspended;
			struct gfs_stat gst;
		} *d;
	} u;
} _gfs_file_buf[MAX_GFS_FILE_BUF];
static int _gfs_file_refcount[MAX_GFS_FILE_BUF];

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

/*
 * gfs_file_buf management
 */

int
gfs_hook_insert_gfs_file(GFS_File gf)
{
	int fd, save_errno;

	_gfs_hook_debug(fprintf(stderr, "GFS: insert_gfs_file: %p\n", gf));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
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
	if (_gfs_file_buf[fd].u.f != NULL) {
		__syscall_close(fd);
		gfs_pio_close(gf);
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
	_gfs_file_buf[fd].d_type = GFS_DT_REG;
	_gfs_file_buf[fd].u.f = gf;
	return (fd);
}

int
gfs_hook_insert_gfs_dir(GFS_Dir dir, char *url)
{
	int fd, save_errno;
	char *e;

	_gfs_hook_debug(fprintf(stderr, "GFS: insert_gfs_dir: %p\n", dir));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
	fd = open("/dev/null", O_RDONLY);
	if (fd == -1) {
		save_errno = errno;
		gfs_closedir(dir);
		errno = save_errno;
		return (-1);
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = EMFILE;
		return (-1);
	}
	if (_gfs_file_buf[fd].u.d != NULL) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
        _gfs_file_buf[fd].u.d = malloc(sizeof(*_gfs_file_buf[fd].u.d));
        if (_gfs_file_buf[fd].u.d == NULL) {
		__syscall_close(fd);
		gfs_closedir(dir);
		errno = ENOMEM;
		return (-1);
        }
	_gfs_file_buf[fd].d_type = GFS_DT_DIR;
        _gfs_file_buf[fd].u.d->dir = dir;
        _gfs_file_buf[fd].u.d->readcount = 0;
        _gfs_file_buf[fd].u.d->suspended = NULL;
        e = gfs_stat(url, &_gfs_file_buf[fd].u.d->gst);
	return (fd);
}

unsigned char
gfs_hook_gfs_file_type(int fd)
{
	return (_gfs_file_buf[fd].d_type);
}

int
gfs_hook_clear_gfs_file(int fd)
{
	int ref;

	_gfs_hook_debug(fprintf(stderr, "GFS: clear_gfs_file: %d\n", fd));

	if (gfs_hook_gfs_file_type(fd) == GFS_DT_REG)
		_gfs_file_buf[fd].u.f = NULL;
	else {
		_gfs_file_buf[fd].u.d->dir = NULL;
		_gfs_file_buf[fd].u.d->readcount = 0;
		_gfs_file_buf[fd].u.d->suspended = NULL;
		gfs_stat_free(&_gfs_file_buf[fd].u.d->gst);
		free(_gfs_file_buf[fd].u.d); 
		_gfs_file_buf[fd].u.d = NULL;
	}

	_gfs_file_buf[fd].d_type = GFS_DT_UNKNOWN;
	__syscall_close(fd);

	ref = _gfs_file_refcount[fd];
	if (ref > 0)
		--_gfs_file_refcount[fd];

	return ref;
}

int
gfs_hook_insert_filedes(int fd, GFS_File gf)
{
	_gfs_hook_debug(
		fprintf(stderr, "GFS: insert_filedes: %d, %p\n", fd, gf));

	if (_gfs_file_buf[fd].u.f != NULL)
		return (-1);

	_gfs_file_buf[fd].d_type = GFS_DT_REG;
	_gfs_file_buf[fd].u.f = gf;

	return (fd);
}

void
gfs_hook_inc_refcount(int fd)
{
	_gfs_hook_debug(
		fprintf(stderr, "GFS: inc_refcount: %d\n", fd));

	++_gfs_file_refcount[fd];
}

/*  printf and puts should not be put into the following function. */
void *
gfs_hook_is_open(int fd)
{
	if (fd < 0 || fd >= MAX_GFS_FILE_BUF)
		return (NULL);

	switch (gfs_hook_gfs_file_type(fd)) {
	case GFS_DT_REG:
		return (_gfs_file_buf[fd].u.f);
	case GFS_DT_DIR:
		return (_gfs_file_buf[fd].u.d->dir);
	default:
		return (NULL);
	}
}

void
gfs_hook_inc_readcount(int fd)
{
	_gfs_hook_debug(
		fprintf(stderr, "GFS: inc_readount: %d\n", fd));

	++_gfs_file_buf[fd].u.d->readcount;
}

int
gfs_hook_is_read(int fd)
{
	return (_gfs_file_buf[fd].u.d->readcount > 0);
}

void
gfs_hook_set_suspended_gfs_dirent(int fd, struct gfs_dirent *entry)
{
	_gfs_file_buf[fd].u.d->suspended = entry;
}

struct gfs_dirent *
gfs_hook_get_suspended_gfs_dirent(int fd)
{
	return (_gfs_file_buf[fd].u.d->suspended);
}

struct gfs_stat *
gfs_hook_get_gfs_stat(int fd)
{
	return (&_gfs_file_buf[fd].u.d->gst);
}

/*
 *  Check whether pathname is gfarm url or not.
 *
 *  Gfarm URL:  gfarm:[:section:]pathname
 *
 *  It is necessary to free the memory space for *url.
 *  Also, it's necessary to free the memory space for *secp,
 *  if *secp is not NULL.
 */
int
gfs_hook_is_url(const char *path, char **urlp, char **secp)
{
	/*
	 * ROOT patch:
	 *   'gfarm@' is also considered as a Gfarm URL
	 */
	static char gfarm_url_prefix_for_root[] = "gfarm@";

	*secp = NULL;
	/*
	 * Objectivity patch:
	 *   '/gfarm:' is also considered as a Gfarm URL
	 */
	if (*path == '/')
		++path;
	if (gfarm_is_url(path) ||
	    /* ROOT patch */
	    memcmp(path, gfarm_url_prefix_for_root,
	    sizeof(gfarm_url_prefix_for_root) - 1) == 0) {
		static char prefix[] = "gfarm:";

		if (!gfarm_initialized &&
		    gfs_hook_initialize() != NULL) {
			gfs_hook_not_initialized();
			return (0); /* don't perform gfarm operation */
		}
		/*
		 * extension for accessing individual sections
		 *   gfarm::section:pathname
		 */
		if (path[sizeof(prefix) - 1] == ':') {
			const char *p = path + sizeof(prefix);
			int secsize = strcspn(p, "/:");
			int urlsize;

			if (p[secsize] != ':')
				return (0); /* gfarm::foo/:bar or gfarm::foo */
			urlsize = sizeof(prefix) - 1 + strlen(p + secsize + 1);
			*urlp = malloc(urlsize + 1);
			*secp = malloc(secsize + 1);
			if (*urlp == NULL || *secp == NULL) {
				if (*urlp != NULL)
					free(*urlp);
				if (*secp != NULL)
					free(*secp);
				return (0); /* XXX - should return ENOMEM */
			}
			memcpy(*urlp, prefix, sizeof(prefix) - 1);
			strcpy(*urlp + sizeof(prefix) - 1, p + secsize + 1);
			memcpy(*secp, p, secsize);
			(*secp)[secsize] = '\0';
			/*
			 * This case needs to free memory space of
			 * both *urlp and *secp.
			 */
		}
		else {
			*urlp = malloc(strlen(path) + 1);
			if (*urlp == NULL)
				return (0) ; /* XXX - should return ENOMEM */
			/*
			 * the reason why we don't just call strcpy(*url, path)
			 * is because the path may be "gfarm@path/name".
			 * (ROOT patch)
			 */
			memcpy(*urlp, prefix, sizeof(prefix) - 1);
			strcpy(*urlp + sizeof(prefix) - 1,
			    path + sizeof(prefix) - 1);
		}
		return (1);
	}
	return (0);
}

/*
 * default file view manipulation
 */

enum gfs_hook_file_view _gfs_hook_default_view = local_view;
int _gfs_hook_index = 0;
int _gfs_hook_num_fragments = GFARM_FILE_DONTCARE;

void
gfs_hook_set_default_view_local()
{
	_gfs_hook_default_view = local_view;
}

void
gfs_hook_set_default_view_index(int index, int nfrags)
{
	_gfs_hook_default_view = index_view;
	_gfs_hook_index = index;
	_gfs_hook_num_fragments = nfrags;
}

void
gfs_hook_set_default_view_global()
{
	_gfs_hook_default_view = global_view;
}

char *
gfs_hook_set_view_local(int filedes, int flag)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_local(gf, flag)) != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_local: %s\n", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_index(int filedes, int nfrags, int index, 
			char *host, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_index(gf, nfrags, index, host, flags))
	    != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_index: %s\n", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_global(int filedes, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_global(gf, flags)) != NULL) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_global: %s\n", e));
		return e;
	}
	return NULL;
}

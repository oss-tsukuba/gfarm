/*
 * $Id$
 */

#include <sys/types.h> /* mode_t */
#include <sys/stat.h> /* umask() */
#include <sys/time.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "gfs_pio.h"
#include "timer.h"

char GFS_FILE_ERROR_EOF[] = "end of file";

char *
gfs_pio_set_view_default(GFS_File gf)
{
	char *e, *e_save = NULL;

	if (gf->view_context != NULL) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = gfs_pio_flush(gf);
		e = (*gf->ops->view_close)(gf);
		if (e_save == NULL)
			e_save = e;
	}
	gf->ops = NULL;
	gf->view_context = NULL;
	gf->view_flags = 0;
	gf->error = e_save;
	return (e_save);
}

static char *
gfs_pio_check_view_default(GFS_File gf)
{
	char *e;

	e = gfs_pio_error(gf);
	if (e != NULL)
		return (e);

	if (gf->view_context == NULL) /* view isn't set yet */
		return (gfs_pio_set_view_global(gf, 0));
	return (NULL);
}

int gfs_pio_fileno(GFS_File gf)
{
	char *e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (-1);

	return ((*gf->ops->view_fd)(gf));
}

static char *
gfs_file_alloc(GFS_File *gfp)
{
	GFS_File gf = malloc(sizeof(struct gfs_file));
	char *buffer = malloc(GFS_FILE_BUFSIZE);

	if (buffer == NULL || gf == NULL) {
		if (buffer != NULL)
			free(buffer);
		if (gf != NULL)
			free(gf);
		*gfp = NULL;
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(gf, 0, sizeof(struct gfs_file));
	gf->mode = 0;
	gf->open_flags = 0;
	gf->error = NULL;
	gf->io_offset = 0;

	gf->buffer = buffer;
	gf->p = 0;
	gf->length = 0;
	gf->offset = 0;

	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);

	*gfp = gf;
	return (NULL);
}

static void
gfs_file_free(GFS_File gf)
{
	free(gf->buffer);
	/* do not touch gf->pi here */
	free(gf);
}

/*
 * note that unlike access(2), gfarm_path_info_access() doesn't/can't check
 * access permission of ancestor directories.
 */
char *
gfarm_path_info_access(struct gfarm_path_info *pi, int mode)
{
	gfarm_mode_t mask = 0;

	if (strcmp(pi->status.st_user, gfarm_get_global_username()) == 0) {
		if (mode & GFS_X_OK)
			mask |= 0100;
		if (mode & GFS_W_OK)
			mask |= 0200;
		if (mode & GFS_R_OK)
			mask |= 0400;
#if 0 /* XXX - check against st_group */
	} else if (gfarm_is_group_member(pi->status.st_group)) {
		if (mode & GFS_X_OK)
			mask |= 0010;
		if (mode & GFS_W_OK)
			mask |= 0020;
		if (mode & GFS_R_OK)
			mask |= 0040;
#endif
	} else {
		if (mode & GFS_X_OK)
			mask |= 0001;
		if (mode & GFS_W_OK)
			mask |= 0002;
		if (mode & GFS_R_OK)
			mask |= 0004;
	}
	return (((pi->status.st_mode & mask) == mask) ?
		NULL : GFARM_ERR_PERMISSION_DENIED);
}

static double gfs_pio_create_time;
static double gfs_pio_open_time;
static double gfs_pio_close_time;
static double gfs_pio_seek_time;
static double gfs_pio_read_time;
static double gfs_pio_write_time;
static double gfs_pio_getline_time;

char *
gfs_pio_create(const char *url, int flags, gfarm_mode_t mode, GFS_File *gfp)
{
	char *e, *pathname;
	GFS_File gf;
	int pi_available = 0;
	mode_t mask;
	char *user;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	user = gfarm_get_global_username();
	if (user == NULL) {
		e = "gfs_pio_create(): programming error, "
		    "gfarm library isn't properly initialized";
		goto finish;
	}
#if 0 /*  XXX - ROOT I/O opens a new file with O_CREAT|O_RDRW mode. */
	if ((flags & GFARM_FILE_ACCMODE) != GFARM_FILE_WRONLY) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED; /* XXX */
		goto finish;
	}
#endif

	mask = umask(0);
	umask(mask);
	mode &= ~mask;

	e = gfarm_url_make_path_for_creation(url, &pathname);
	if (e != NULL)
		goto finish;
	e = gfs_file_alloc(&gf);
	if (e != NULL) {
		free(pathname);
		goto finish;
	}

	/* gfs_pio_create() always assumes CREATE, TRUNC */
	flags |= GFARM_FILE_CREATE | GFARM_FILE_TRUNC;

	if ((flags & (GFARM_FILE_TRUNC|GFARM_FILE_SEQUENTIAL)) !=
	    (GFARM_FILE_TRUNC|GFARM_FILE_SEQUENTIAL)) {
		/* MODE_READ is needed to re-calculate checksum. */
		flags = (flags & ~GFARM_FILE_ACCMODE) | GFARM_FILE_RDWR;
	} else if ((flags & ~GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY) {
		flags = (flags & ~GFARM_FILE_ACCMODE) | GFARM_FILE_WRONLY;
	} 
	gf->open_flags = flags;

	gf->mode = GFS_FILE_MODE_WRITE;
	if ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR)
		gf->mode |= GFS_FILE_MODE_READ;
	e = gfarm_path_info_get(pathname, &gf->pi);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		free(pathname);
		gfs_file_free(gf);
		goto finish;
	}
	if (e == NULL) {
		/* XXX unlink and re-create the file? */
		free(pathname);
		e = gfarm_path_info_access(&gf->pi, GFS_W_OK);
		if (e != NULL) {
			gfarm_path_info_free(&gf->pi);
			gfs_file_free(gf);
			goto finish;
		}
		if (!GFARM_S_ISREG(gf->pi.status.st_mode)) {
			if (GFARM_S_ISDIR(gf->pi.status.st_mode))
				e = GFARM_ERR_IS_A_DIRECTORY;
			else
				e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
			gfarm_path_info_free(&gf->pi);
			gfs_file_free(gf);
			goto finish;
		}
		/*
		 * XXX should check the follows:
		 * - the mode is consistent among same job
		 * - creator of the metainfo has same job id
		 * - O_TRUNC / !O_TRUNC case
		 */
		mode |= GFARM_S_IFREG;
		if (GFARM_S_IS_PROGRAM(mode) != GFS_FILE_IS_PROGRAM(gf)) {
			gfarm_path_info_free(&gf->pi);
			gfs_file_free(gf);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			goto finish;
		}
		pi_available = 1;
	}
	if (!pi_available) {
		struct timeval now;

		gettimeofday(&now, NULL);
		gf->pi.pathname = pathname;
		gf->pi.status.st_mode = (GFARM_S_IFREG | mode);
		gf->pi.status.st_user = strdup(user); /* XXX NULL check */
		gf->pi.status.st_group = strdup("*"); /* XXX for now */
		gf->pi.status.st_atimespec.tv_sec =
		gf->pi.status.st_mtimespec.tv_sec =
		gf->pi.status.st_ctimespec.tv_sec = now.tv_sec;
		gf->pi.status.st_atimespec.tv_nsec =
		gf->pi.status.st_mtimespec.tv_nsec =
		gf->pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
		gf->pi.status.st_size = 0;
		gf->pi.status.st_nsections = 0;
	}
	*gfp = gf;
	gfs_uncachedir();

	e = NULL;
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_create_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_pio_open(const char *url, int flags, GFS_File *gfp)
{
	char *e, *pathname;
	GFS_File gf;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	if ((flags & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY) {
#if 0 /*  XXX - ROOT I/O opens a new file with O_CREAT|O_RDRW mode. */
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED; /* XXX */
		goto finish;
#else
		flags |= GFARM_FILE_CREATE;
#endif
	}
	e = gfarm_url_make_path(url, &pathname);
	if (e != NULL)
		goto finish;
	e = gfs_file_alloc(&gf);
	if (e != NULL) {
		free(pathname);
		goto finish;
	}
	gf->open_flags = flags;
	if (((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR)
	    || ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY))
		gf->mode |= GFS_FILE_MODE_READ;
	if (((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR)
	    || ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY))
		gf->mode |= GFS_FILE_MODE_WRITE;
	e = gfarm_path_info_get(pathname, &gf->pi);
	free(pathname);
	if (e != NULL)
		goto free_gf;
	if (gf->mode & GFS_FILE_MODE_READ) {
		e = gfarm_path_info_access(&gf->pi, GFS_R_OK);
		if (e != NULL)
			goto free_gf_pi;
	}
	if (gf->mode & GFS_FILE_MODE_WRITE) {
		e = gfarm_path_info_access(&gf->pi, GFS_W_OK);
		if (e != NULL)
			goto free_gf_pi;
	}
	if (!GFARM_S_ISREG(gf->pi.status.st_mode)) {
		if (GFARM_S_ISDIR(gf->pi.status.st_mode))
			e = GFARM_ERR_IS_A_DIRECTORY;
		else
			e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto free_gf_pi;
	}
	gf->mode |= GFS_FILE_MODE_NSEGMENTS_FIXED;
	*gfp = gf;

	e = NULL;
	goto finish;
free_gf_pi:
	gfarm_path_info_free(&gf->pi);
free_gf:
	gfs_file_free(gf);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_open_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

int
gfs_pio_eof(GFS_File gf)
{
	return (gf->error == GFS_FILE_ERROR_EOF);
}

#define GFS_PIO_ERROR(gf) \
	((gf)->error != GFS_FILE_ERROR_EOF ? (gf)->error : NULL)

char *
gfs_pio_error(GFS_File gf)
{
	return (GFS_PIO_ERROR(gf));
}

void
gfs_pio_clearerr(GFS_File gf)
{
	gf->error = NULL;
}

char *
gfs_pio_get_nfragment(GFS_File gf, int *nfragmentsp)
{
	if (GFS_FILE_IS_PROGRAM(gf))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((gf->mode & GFS_FILE_MODE_NSEGMENTS_FIXED) == 0)
		return (GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE);
	*nfragmentsp = gf->pi.status.st_nsections;
	return (NULL);
}

char *
gfs_pio_close(GFS_File gf)
{
	char *e, *e_save;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e_save = gfs_pio_check_view_default(gf);
	if (e_save == NULL) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = gfs_pio_flush(gf);

		e = (*gf->ops->view_close)(gf);
		if (e_save == NULL)
			e_save = e;
	}
	gfarm_path_info_free(&gf->pi);
	gfs_file_free(gf);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_close_time += gfarm_timerval_sub(&t2, &t1));

	return (e_save);
}

char *
gfs_pio_purge(GFS_File gf)
{
	gf->p = gf->length = 0;
	return (NULL);
}

#define CHECK_WRITABLE(gf) { \
	if (((gf)->mode & GFS_FILE_MODE_WRITE) == 0) \
		return (gfarm_errno_to_error(EBADF)); \
}
/*
 * we check this against gf->open_flags rather than gf->mode,
 * because we may set GFARM_FILE_MODE_READ even if write-only case.
 */
#define CHECK_READABLE(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) \
		return (gfarm_errno_to_error(EBADF)); \
}

#define CHECK_READABLE_EOF(gf) { \
	if (((gf)->open_flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY) \
		return (EOF); \
}

static char *
gfs_pio_fillbuf(GFS_File gf)
{
	char *e;
	size_t len;

	CHECK_READABLE(gf);

	if (gf->error != NULL)
		return (GFS_PIO_ERROR(gf));
	if (gf->p < gf->length)
		return (NULL);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = gfs_pio_flush(gf);
		if (e != NULL)
			return (e);
	} else {
		gf->offset += gf->p;
		gf->p = gf->length = 0;
	}
	assert(gf->io_offset == gf->offset);

	e = (*gf->ops->view_read)(gf, gf->buffer, GFS_FILE_BUFSIZE, &len);
	if (e != NULL) {
		gf->error = e;
		return (e);
	}
	gf->length = len;
	gf->io_offset += len;
	if (len == 0)
		gf->error = GFS_FILE_ERROR_EOF;
	return (NULL);
}

/* unlike other functions, this returns `*writtenp' even if an error happens */
static char *
do_write(GFS_File gf, const char *buffer, size_t length, size_t *writtenp)
{
	char *e = NULL;
	size_t written, len;

	if (length == 0) {
		*writtenp = 0;
		return (NULL);
	}
	if (gf->io_offset != gf->offset) {
		/* this happens when switching from reading to writing */
		gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
		e = (*gf->ops->view_seek)(gf, SEEK_SET, gf->offset, NULL);
		if (e != NULL) {
			gf->error = e;
			*writtenp = 0;
			return (e);
		}
		gf->io_offset = gf->offset;
	}
	for (written = 0; written < length; written += len) {
		e = (*gf->ops->view_write)(
			gf, buffer + written, length - written,
			&len);
		if (e != NULL) {
			gf->error = e;
			break;
		}
	}
	gf->io_offset += written;
	*writtenp = written;
	return (e);
}

char *
gfs_pio_flush(GFS_File gf)
{
	char *e = gfs_pio_check_view_default(gf);
	size_t written;

	if (e != NULL)
		return (e);

	CHECK_WRITABLE(gf);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = do_write(gf, gf->buffer, gf->length, &written);
		if (e != NULL)
			return (e);
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;
	}
	if (gf->p >= gf->length) {
		gf->offset += gf->p;
		gf->p = gf->length = 0;
	}
	return (NULL);
}

char *
gfs_pio_seek(GFS_File gf, file_offset_t offset, int whence,
	     file_offset_t *resultp)
{
	char *e;
	file_offset_t where;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (e);

	if (whence == SEEK_SET || whence == SEEK_CUR) {
		file_offset_t tmp_offset = offset;
		/*
		 * This is the case that the file offset will be
		 * repositioned within the current io buffer.
		 */
		if (whence == SEEK_CUR)
			tmp_offset += gf->offset + gf->p;

		if (gf->offset <= tmp_offset
		    && tmp_offset <= gf->offset + gf->length){
			/*
			 * We don't have to clear
			 * GFS_FILE_MODE_CALC_DIGEST bit here, because
			 * this is no problem to calculate checksum
			 * for write-only or read-only case.
			 * This is also ok on switching from writing
			 * to reading.
			 * This is not ok on switching from reading to
			 * writing, but gfs_pio_flush() clears the bit
			 * at that case.
			 */
			gf->p = tmp_offset - gf->offset;
			if (resultp != NULL)
				*resultp = tmp_offset;

			e = NULL;
			goto finish;
		}
	} else if (whence != SEEK_END) {
		e = gf->error = GFARM_ERR_INVALID_ARGUMENT;
		goto finish;
	}

	gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
	    
	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = gfs_pio_flush(gf);
		if (e != NULL)
			goto finish;
	}
	gf->error = NULL; /* purge EOF/error state */
	gfs_pio_purge(gf);
	e = (*gf->ops->view_seek)(gf, offset, whence, &where);
	if (e != NULL) {
		gf->error = e;
		goto finish;
	}
	gf->offset = gf->io_offset = where;
	if (resultp != NULL)
		*resultp = where;

	e = NULL;
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_seek_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_pio_read(GFS_File gf, void *buffer, int size, int *np)
{
	char *e;
	char *p = buffer;
	int n = 0;
	int length;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (e);

	CHECK_READABLE(gf);

	while (size > 0) {
		if ((e = gfs_pio_fillbuf(gf)) != NULL)
			break;
		if (gf->error != NULL) /* EOF or error */
			break;
		length = gf->length - gf->p;
		if (length > size)
			length = size;
		memcpy(p, gf->buffer + gf->p, length);
		p += length;
		n += length;
		size -= length;
		gf->p += length;
	}
	if (e != NULL && n == 0)
		goto finish;
	*np = n;

	e = NULL;
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_read_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_pio_write(GFS_File gf, const void *buffer, int size, int *np)
{
	char *e;
	size_t written;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (e);

	CHECK_WRITABLE(gf);

	if (size + gf->p > GFS_FILE_BUFSIZE) {
		/*
		 * gf->buffer[gf->p .. GFS_FILE_BUFSIZE-1] will be overridden
		 * by buffer.
		 */
		gf->length = gf->p;
		e = gfs_pio_flush(gf);
		if (e != NULL)
			goto finish;
	}
	if (size >= GFS_FILE_BUFSIZE) {
		/* shortcut to avoid unnecessary memory copy */
		assert(gf->p == 0); /* gfs_pio_flush() was called above */
		gf->length = 0;
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;

		e = do_write(gf, buffer, size, &written);
		if (e != NULL && written == 0)
			goto finish;
		gf->offset += written;
		*np = written; /* XXX - size_t vs int */

		e = NULL;
		goto finish;
	}
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	memcpy(gf->buffer + gf->p, buffer, size);
	gf->p += size;
	if (gf->p > gf->length)
		gf->length = gf->p;
	*np = size;
	e = NULL;
	if (gf->p >= GFS_FILE_BUFSIZE)
		e = gfs_pio_flush(gf);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_write_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

int
gfs_pio_getc(GFS_File gf)
{
	char *e = gfs_pio_check_view_default(gf);

	if (e != NULL) {
		gf->error = e;
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (gf->p >= gf->length) {
		if (gfs_pio_fillbuf(gf) != NULL)
			return (EOF); /* can get reason via gfs_pio_error() */
		if (gf->error != NULL)
			return (EOF);
	}
	return (((unsigned char *)gf->buffer)[gf->p++]);
}

int
gfs_pio_ungetc(GFS_File gf, int c)
{
	char *e = gfs_pio_check_view_default(gf);

	if (e != NULL) {
		gf->error = e;
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (c != EOF) {
		if (gf->p == 0) { /* cannot unget - XXX should permit this? */
			gf->error = GFARM_ERR_NO_SPACE;
			return (EOF);
		}
		/* We do not mark this buffer dirty here. */
		gf->buffer[--gf->p] = c;
	}
	return (c);
}

char *
gfs_pio_putc(GFS_File gf, int c)
{
	char *e = gfs_pio_check_view_default(gf);

	if (e != NULL)
		return (e);

	CHECK_WRITABLE(gf);

	if (gf->p >= GFS_FILE_BUFSIZE) {
		char *e = gfs_pio_flush(gf);

		if (e != NULL)
			return (e);
	}
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	gf->buffer[gf->p++] = c;
	if (gf->p > gf->length)
		gf->length = gf->p;
	if (gf->p >= GFS_FILE_BUFSIZE)
		return (gfs_pio_flush(gf));
	return (NULL);
}

/* mostly compatible with fgets(3) */
char *
gfs_pio_puts(GFS_File gf, const char *s)
{
	char *e = gfs_pio_check_view_default(gf);

	if (e != NULL)
		return (e);

	CHECK_WRITABLE(gf);

	while (*s != '\0') {
		char *e = gfs_pio_putc(gf, *(unsigned char *)s);

		if (e != NULL)
			return (e);
		s++;
	}
	return (NULL);
}

/* mostly compatible with fgets(3), but EOF check is done by *s == '\0' */
char *
gfs_pio_gets(GFS_File gf, char *s, size_t size)
{
	char *e = gfs_pio_check_view_default(gf);
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		return (gf->error);
	}
	--size; /* for '\0' */
	for (; size > 0 && (c = gfs_pio_getc(gf)) != EOF; --size) {
		*p++ = c;
		if (c == '\n')
			break;
	}
	*p++ = '\0';

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_gets_time??? */
	gfs_profile(gfs_pio_getline_time += gfarm_timerval_sub(&t2, &t1));

	return (NULL);
}

char *
gfs_pio_getline(GFS_File gf, char *s, size_t size, int *eofp)
{
	char *e = gfs_pio_check_view_default(gf);
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		return (gf->error);
	}
	--size; /* for '\0' */
	for (; size > 0 && (c = gfs_pio_getc(gf)) != EOF; --size) {
		if (c == '\n')
			break;
		*p++ = c;
	}
	*p++ = '\0';
	if (p == s + 1 && c == EOF) {
		*eofp = 1;
		return (GFS_PIO_ERROR(gf));
	}
	*eofp = 0;

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_getline_time += gfarm_timerval_sub(&t2, &t1));

	return (NULL);
}

char *
gfs_pio_putline(GFS_File gf, const char *s)
{
	char *e = gfs_pio_check_view_default(gf);

	if (e != NULL)
		return (e);

	CHECK_WRITABLE(gf);

	e = gfs_pio_puts(gf, s);
	if (e != NULL)
		return (e);
	return (gfs_pio_putc(gf, '\n'));
}

#define ALLOC_SIZE_INIT	220

/*
 * mostly compatible with getline(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
char *
gfs_pio_readline(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp)
{
	char *e = gfs_pio_check_view_default(gf);
	char *buf = *bufp, *p;
	size_t size = *sizep, len = 0;
	int c;
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		buf = realloc(buf, size);
		if (buf == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	for (;;) {
		c = gfs_pio_getc(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			p = realloc(buf, size + size);
			if (p == NULL) {
				*bufp = buf;
				*sizep = size;
				return (GFARM_ERR_NO_MEMORY);
			}
			buf = p;
			size += size;
		}
		buf[len++] = c;
		if (c == '\n')
			break;
	}
	if (size <= len) {
		p = realloc(buf, size + size);
		if (p == NULL) {
			*bufp = buf;
			*sizep = size;
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readline_time??? */
	gfs_profile(gfs_pio_getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;
	

	return (NULL);
}

/*
 * mostly compatible with getdelim(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
char *
gfs_pio_readdelim(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp,
	const char *delim, size_t delimlen)
{
	char *e = gfs_pio_check_view_default(gf);
	char *buf = *bufp, *p;
	size_t size = *sizep, len = 0;
	int c, delimtail;
	static const char empty_line[] = "\n\n";
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (delim == NULL) { /* special case 1 */
		delimtail = 0; /* workaround gcc warning */
	} else {
		if (delimlen == 0) { /* special case 2 */
			delim = empty_line;
			delimlen = 2;
		}
		delimtail = delim[delimlen - 1];
	}
	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		buf = realloc(buf, size);
		if (buf == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	for (;;) {
		c = gfs_pio_getc(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			p = realloc(buf, size + size);
			if (p == NULL) {
				*bufp = buf;
				*sizep = size;
				return (GFARM_ERR_NO_MEMORY);
			}
			buf = p;
			size += size;
		}
		buf[len++] = c;
		if (delim == NULL) /* special case 1: no delimiter */
			continue;
		if (len >= delimlen && c == delimtail &&
		    memcmp(&buf[len - delimlen], delim, delimlen) == 0) {
			if (delim == empty_line) { /* special case 2 */
				for (;;) {
					c = gfs_pio_getc(gf);
					if (c == EOF)
						break;
					if (c != '\n') {
						gfs_pio_ungetc(gf, c);
						break;
					}
					if (size <= len) {
						p = realloc(buf, size + size);
						if (p == NULL) {
							*bufp = buf;
							*sizep = size;
							return (
							  GFARM_ERR_NO_MEMORY);
						}
						buf = p;
						size += size;
					}
					buf[len++] = c;
				}
			}
			break;
		}
	}
	if (size <= len) {
		p = realloc(buf, size + size);
		if (p == NULL) {
			*bufp = buf;
			*sizep = size;
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readdelim_time??? */
	gfs_profile(gfs_pio_getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;

	return (NULL);
}

void
gfs_display_timers()
{
	fprintf(stderr, "gfs_pio_create  : %g sec\n", gfs_pio_create_time);
	fprintf(stderr, "gfs_pio_open    : %g sec\n", gfs_pio_open_time);
	fprintf(stderr, "gfs_pio_close   : %g sec\n", gfs_pio_close_time);
	fprintf(stderr, "gfs_pio_seek    : %g sec\n", gfs_pio_seek_time);
	fprintf(stderr, "gfs_pio_read    : %g sec\n", gfs_pio_read_time);
	fprintf(stderr, "gfs_pio_write   : %g sec\n", gfs_pio_write_time);
	fprintf(stderr, "gfs_pio_getline : %g sec\n", gfs_pio_getline_time);
	fprintf(stderr, "gfs_pio_set_view_section : %g sec\n",
		gfs_pio_set_view_section_time);
	fprintf(stderr, "gfs_stat        : %g sec\n", gfs_stat_time);
	fprintf(stderr, "gfs_unlink      : %g sec\n", gfs_unlink_time);
}

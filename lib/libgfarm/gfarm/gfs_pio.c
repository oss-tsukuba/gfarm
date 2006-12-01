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
#include <unistd.h>	/* [FRWX]_OK */
#include <errno.h>

#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "config.h"	/* gfs_profile */
#include "gfs_proto.h"
#include "gfs_pio.h"
#include "gfs_misc.h"

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

void
gfs_pio_set_calc_digest(GFS_File gf)
{
	gf->mode |= GFS_FILE_MODE_CALC_DIGEST;
}

void
gfs_pio_unset_calc_digest(GFS_File gf)
{
	gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
}

int
gfs_pio_check_calc_digest(GFS_File gf)
{
	return ((gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0);
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
	GFS_File gf;
	char *buffer;

	GFARM_MALLOC(gf);
	GFARM_MALLOC_ARRAY(buffer, GFS_FILE_BUFSIZE);
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

static void
gfs_pio_open_initialize_mode_flags(GFS_File gf, int flags)
{
#if 0
	/*
	 * This is obsolete after the revision 1.59 of
	 * gfs_pio_section.c on Feb 26, 2006.  We do not recalculate
	 * chack sum in any case.
	 */
	/*
	 * It may be necessary to calculate checksum of the
	 * whole file when closing on either random access case
	 * or even sequential access without truncation,
	 * which requires a read mode.
	 */
	if ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY &&
	    (flags & (GFARM_FILE_TRUNC|GFARM_FILE_SEQUENTIAL)) !=
	    (GFARM_FILE_TRUNC|GFARM_FILE_SEQUENTIAL))
		flags = (flags & ~GFARM_FILE_ACCMODE) | GFARM_FILE_RDWR;
#endif
	gf->open_flags = flags;

	if (((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR)
	    || ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDONLY))
		gf->mode |= GFS_FILE_MODE_READ;
	if (((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_RDWR)
	    || ((flags & GFARM_FILE_ACCMODE) == GFARM_FILE_WRONLY))
		gf->mode |= GFS_FILE_MODE_WRITE;
}

static char *
gfs_pio_open_check_perm(GFS_File gf)
{
	char *e;
	int check= 0;

	if ((gf->mode & GFS_FILE_MODE_READ) != 0)
		check |= R_OK;
	if ((gf->mode & GFS_FILE_MODE_WRITE) != 0 ||
	    (gf->open_flags & GFARM_FILE_TRUNC) != 0)
		check |= W_OK;
	e = gfarm_path_info_access(&gf->pi, check);
	if (e != NULL)
		return (e);
	if (!GFARM_S_ISREG(gf->pi.status.st_mode)) {
		if (GFARM_S_ISDIR(gf->pi.status.st_mode))
			return (GFARM_ERR_IS_A_DIRECTORY);
		else
			return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}
	return (NULL);
}

static double gfs_pio_create_time;
static double gfs_pio_open_time;
static double gfs_pio_close_time;
static double gfs_pio_seek_time;
static double gfs_pio_truncate_time;
static double gfs_pio_read_time;
static double gfs_pio_write_time;
static double gfs_pio_sync_time;
static double gfs_pio_datasync_time;
static double gfs_pio_getline_time;

char *
gfs_pio_create(const char *url, int flags, gfarm_mode_t mode, GFS_File *gfp)
{
	char *e, *pathname;
	GFS_File gf;
	char *user;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	user = gfarm_get_global_username();
	if (user == NULL) {
		e = "gfs_pio_create(): programming error, "
		    "gfarm library isn't properly initialized";
		goto finish;
	}

	e = gfarm_url_make_path_for_creation(url, &pathname);
	if (e != NULL)
		goto finish;
	e = gfs_file_alloc(&gf);
	if (e != NULL) {
		free(pathname);
		goto finish;
	}

	flags |= GFARM_FILE_CREATE;

	gfs_pio_open_initialize_mode_flags(gf, flags);

	e = gfarm_path_info_get(pathname, &gf->pi);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		free(pathname);
		goto free_gf;
	}
	if (e == NULL) {
		free(pathname);
		if ((flags & GFARM_FILE_EXCLUSIVE) != 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
			goto free_gf_pi;
		}
		e = gfs_pio_open_check_perm(gf);
		if (e != NULL)
			goto free_gf_pi;
		/*
		 * XXX should check the follows:
		 * - the mode is consistent among same job
		 * - creator of the metainfo has same job id
		 * - O_TRUNC / !O_TRUNC case
		 */
		if ((flags & GFARM_FILE_TRUNC) == 0)
			gf->mode |= GFS_FILE_MODE_NSEGMENTS_FIXED;
	} else {
		mode_t mask;
		struct timeval now;

		mask = umask(0);
		umask(mask);

		gettimeofday(&now, NULL);
		gf->pi.pathname = pathname;
		gf->pi.status.st_mode =
		    (GFARM_S_IFREG | (mode & ~mask & GFARM_S_ALLPERM));
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
		gf->mode |= GFS_FILE_MODE_FILE_WAS_CREATED;
	}
	*gfp = gf;

	e = NULL;
	goto finish;
free_gf_pi:
	gfarm_path_info_free(&gf->pi);
free_gf:
	gfs_file_free(gf);
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
	file_offset_t s;
	int nsec;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if (flags & GFARM_FILE_CREATE) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto finish;
	}
	e = gfarm_url_make_path(url, &pathname);
	if (e != NULL)
		goto finish;
	e = gfs_file_alloc(&gf);
	if (e != NULL) {
		free(pathname);
		goto finish;
	}

	/* GFARM_FILE_EXCLUSIVE is a NOP with gfs_pio_open(). */
	flags &= ~GFARM_FILE_EXCLUSIVE;

	gfs_pio_open_initialize_mode_flags(gf, flags);

	e = gfarm_path_info_get(pathname, &gf->pi);
	free(pathname);
	if (e != NULL)
		goto free_gf;
	e = gfs_pio_open_check_perm(gf);
	if (e != NULL)
		goto free_gf_pi;
	if ((flags & GFARM_FILE_TRUNC) == 0)
		gf->mode |= GFS_FILE_MODE_NSEGMENTS_FIXED;
	/*
	 * XXX - Add GFARM_FILE_TRUNC when writing a file with size 0
	 * to avoid re-calculation of checksum on close.
	 */
	if ((gf->mode & GFS_FILE_MODE_WRITE) != 0 &&
	    gfs_stat_size_canonical_path(gf->pi.pathname, &s, &nsec) == NULL &&
	    s == 0)
		gf->open_flags |= GFARM_FILE_TRUNC;

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

static char *
gfs_pio_update_time_weak(GFS_File gf, struct gfarm_timespec *tp)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	tp->tv_sec = now.tv_sec;
	tp->tv_nsec = now.tv_usec * 1000;
	/* but not add GFS_FILE_MODE_FILE_WAS_ACCESSED flag */
	return (NULL);
}

static char *
gfs_pio_update_time(GFS_File gf, struct gfarm_timespec *tp)
{
	gfs_pio_update_time_weak(gf, tp);
	gf->mode |= GFS_FILE_MODE_FILE_WAS_ACCESSED;
	return (NULL);
}

/* common routine of gfs_pio_close_internal() and gfs_pio_close() */
static char *
gfs_pio_close_common(GFS_File gf)
{
	char *e, *e_save = NULL;

	if (gf->view_context != NULL) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = gfs_pio_flush(gf);

		e = (*gf->ops->view_close)(gf);
		if (e_save == NULL)
			e_save = e;
	}
	/*
	 * When there is inconsistency, do not update/overwrite the
	 * metadata. This inconsistency may come from the update by
	 * other process or oneself such as 'nvi'.
	 *
	 * XXX Maybe we should check whether another process already
	 * updated this metadata or not.  There are race conditions
	 * that we cannot fix at least until gfarm v2, though.
	 * Should we pay some effort even if it cannot be complete?
	 */
	if (e_save == NULL && (gf->mode & GFS_FILE_MODE_FILE_WAS_ACCESSED))
		e_save = gfarm_path_info_replace(gf->pi.pathname, &gf->pi);

	gfarm_path_info_free(&gf->pi);
	gfs_file_free(gf);

	return (e_save);
}

char *
gfs_pio_close(GFS_File gf)
{
	char *e, *e_save;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	/*
	 * If we won't call gfs_pio_clearerr() here,
	 * gfs_pio_check_view_default() might return error immediately.
	 */
	gfs_pio_clearerr(gf);
	/*
	 * need to set default view, otherwise the following code sequence
	 * will be broken. i.e. the file won't be created:
	 *   e = gfs_pio_create(file, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	 *       &gf);
	 *   if (e == NULL)
	 *	e = gfs_pio_close(gf);
	 */
	e_save = gfs_pio_check_view_default(gf);

	e = gfs_pio_close_common(gf);
	if (e_save == NULL)
		e_save = e;

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_close_time += gfarm_timerval_sub(&t2, &t1));

	return (e_save);
}

/*
 * gfs_pio_close_internal() is private interface, and only called from
 * global view processing.
 *
 * gfs_pio_close_internal() is almost similar to gfs_pio_close(), but
 * won't call gfs_pio_check_view_default() to try to set its view to
 * the default view. because:
 * - global view procedures always set its fragment_gf to index view,
 *   so we don't have to try to set the view to the default.
 * - if we try to set the view to the default by gfs_pio_check_view_default(),
 *   the following infinite recursive call may be caused by a file which
 *   causes gfs_pio_set_view_index() fail:
 *	gfs_pio_close(gf)
 *	-> gfs_pio_check_view_default(gf)
 *	-> gfs_pio_set_view_global(gf, 0)
 *	-> gfs_pio_view_global_move_to(gf, 0)
 *	-> gfs_pio_create or gfs_pio_open(..., &new_fragment) (-> success) +
 *	   gfs_pio_set_view_index(new_fragment, ...) (-> fail) +
 *	   gfs_pio_close(new_fragment)
 *	   and this gfs_pio_close(new_fragment) may causes
 *	   the infinite recursive call.
 */
char *
gfs_pio_close_internal(GFS_File gf)
{
	char *e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_close_common(gf);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_close_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_fchmod(GFS_File gf, gfarm_mode_t mode)
{
	return (*gf->ops->view_chmod)(gf, mode);
}

static char *
gfs_pio_purge(GFS_File gf)
{
	gf->offset += gf->p;
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
gfs_pio_fillbuf(GFS_File gf, size_t size)
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
	} else
		gfs_pio_purge(gf);

	assert(gf->io_offset == gf->offset);

	e = (*gf->ops->view_read)(gf, gf->buffer, size, &len);
	if (e != NULL) {
		gf->error = e;
		return (e);
	}
	gf->length = len;
	gf->io_offset += len;
	if (len == 0)
		gf->error = GFS_FILE_ERROR_EOF;

	if (gfarm_record_atime)
		gfs_pio_update_time(gf, &gf->pi.status.st_atimespec);
	else
		gfs_pio_update_time_weak(gf, &gf->pi.status.st_atimespec);
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
		gfs_pio_unset_calc_digest(gf);
		e = (*gf->ops->view_seek)(gf, gf->offset, SEEK_SET, NULL);
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
		gfs_pio_update_time(gf, &gf->pi.status.st_mtimespec);
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
	if (gf->p >= gf->length)
		gfs_pio_purge(gf);

	return (NULL);
}

char *
gfs_pio_seek(GFS_File gf, file_offset_t offset, int whence,
	     file_offset_t *resultp)
{
	char *e;
	file_offset_t where;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
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

	gfs_pio_unset_calc_digest(gf);

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = gfs_pio_flush(gf);
		if (e != NULL) {
			gf->error = e;
			goto finish;
		}
	}

	if (whence == SEEK_CUR)
		/* modify offset based on io_offset */
		offset += gf->offset + gf->p - gf->io_offset;

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
gfs_pio_truncate(GFS_File gf, file_offset_t length)
{
	char *e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		goto finish;

	CHECK_WRITABLE(gf);

	gfs_pio_unset_calc_digest(gf);

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = gfs_pio_flush(gf);
		if (e != NULL)
			goto finish;
	}

	gf->error = NULL; /* purge EOF/error state */
	gfs_pio_purge(gf);

	e = (*gf->ops->view_ftruncate)(gf, length);
	if (e != NULL)
		gf->error = e;
	else
		gfs_pio_update_time(gf, &gf->pi.status.st_mtimespec);
finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_truncate_time += gfarm_timerval_sub(&t2, &t1));

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

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (e);

	CHECK_READABLE(gf);

	while (size > 0) {
		if ((e = gfs_pio_fillbuf(gf,
		    ((gf->open_flags & GFARM_FILE_UNBUFFERED) &&
		    size < GFS_FILE_BUFSIZE) ? size : GFS_FILE_BUFSIZE))
		    != NULL)
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

	if (gf->open_flags & GFARM_FILE_UNBUFFERED)
		gfs_pio_purge(gf);
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

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
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
		e = gfs_pio_flush(gf); /* this does purge too */
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
	if (gf->open_flags & GFARM_FILE_UNBUFFERED ||
	    gf->p >= GFS_FILE_BUFSIZE) {
		e = gfs_pio_flush(gf);
		if (gf->open_flags & GFARM_FILE_UNBUFFERED)
			gfs_pio_purge(gf);
	}
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_write_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_fstat(GFS_File gf, struct gfs_stat *status)
{
	char *e;
	file_offset_t size;

	e = gfs_pio_check_view_default(gf);
	if (e != NULL)
		return (e);

	e = (*gf->ops->view_stat)(gf, status);
	if (e != NULL)
		return (e);
	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0 &&
	    status->st_size < (size = gf->offset + gf->length))
		status->st_size = size;

	/*
	 * XXX Maybe we should check whether another process already
	 * updated these time data or not.
	 * Should we pay some effort even if it cannot be complete?
	 */
	status->st_atimespec = gf->pi.status.st_atimespec;
	status->st_mtimespec = gf->pi.status.st_mtimespec;
	return (GFARM_ERR_NO_ERROR);
}

static char *
sync_internal(GFS_File gf, int operation, double *time)
{
	char *e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_flush(gf);
	if (e != NULL)
		goto finish;

	e = (*gf->ops->view_fsync)(gf, operation);
	if (e != NULL)
		gf->error = e;
finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(*time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

char *
gfs_pio_sync(GFS_File gf)
{
	return (sync_internal(gf, GFS_PROTO_FSYNC_WITH_METADATA,
			      &gfs_pio_sync_time));
}

char *
gfs_pio_datasync(GFS_File gf)
{
	return (sync_internal(gf, GFS_PROTO_FSYNC_WITHOUT_METADATA,
			      &gfs_pio_datasync_time));
}

int
gfs_pio_getc(GFS_File gf)
{
	char *e = gfs_pio_check_view_default(gf);
	int c;

	if (e != NULL) {
		gf->error = e;
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (gf->p >= gf->length) {
		if (gfs_pio_fillbuf(gf,
		    gf->open_flags & GFARM_FILE_UNBUFFERED ?
		    1 : GFS_FILE_BUFSIZE) != NULL)
			return (EOF); /* can get reason via gfs_pio_error() */
		if (gf->error != NULL)
			return (EOF);
	}
	c = ((unsigned char *)gf->buffer)[gf->p++];
	if (gf->open_flags & GFARM_FILE_UNBUFFERED)
		gfs_pio_purge(gf);
	return (c);
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
		char *e = gfs_pio_flush(gf); /* this does purge too */

		if (e != NULL)
			return (e);
	}
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	gf->buffer[gf->p++] = c;
	if (gf->p > gf->length)
		gf->length = gf->p;
	if (gf->open_flags & GFARM_FILE_UNBUFFERED ||
	    gf->p >= GFS_FILE_BUFSIZE) {
		e = gfs_pio_flush(gf);
		if (gf->open_flags & GFARM_FILE_UNBUFFERED)
			gfs_pio_purge(gf);
		return (e);
	}
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

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

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

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	c = EOF;
#endif
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

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
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0;
	int c;
	size_t alloc_size;
	int overflow = 0;
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	CHECK_READABLE(gf);

	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	for (;;) {
		c = gfs_pio_getc(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			alloc_size = gfarm_size_add(&overflow, size, size);
			if (!overflow)
				GFARM_REALLOC_ARRAY(p, buf, alloc_size);
			if (overflow || p == NULL) {
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
		alloc_size = gfarm_size_add(&overflow, size, size);
		if (!overflow)
			GFARM_REALLOC_ARRAY(p, buf, alloc_size);
		if (overflow || p == NULL) {
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
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0;
	int c, delimtail;
	static const char empty_line[] = "\n\n";
	size_t alloc_size;
	int overflow = 0;
	gfarm_timerval_t t1, t2;

	if (e != NULL)
		return (e);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

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
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	for (;;) {
		c = gfs_pio_getc(gf);
		if (c == EOF)
			break;
		if (size <= len) {
			alloc_size = gfarm_size_add(&overflow, size, size);
			if (!overflow)
				GFARM_REALLOC_ARRAY(p, buf, alloc_size);
			if (overflow || p == NULL) {
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
						alloc_size = gfarm_size_add(
							&overflow, size, size);
						if (!overflow)
							GFARM_REALLOC_ARRAY(p,
							    buf, alloc_size);
						if (overflow || p == NULL) {
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
		alloc_size = gfarm_size_add(&overflow, size, size);
		if (!overflow)
			GFARM_REALLOC_ARRAY(p, buf, alloc_size);
		if (overflow || p == NULL) {
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
gfs_pio_display_timers(void)
{
	gflog_info("gfs_pio_create  : %g sec\n", gfs_pio_create_time);
	gflog_info("gfs_pio_open    : %g sec\n", gfs_pio_open_time);
	gflog_info("gfs_pio_close   : %g sec\n", gfs_pio_close_time);
	gflog_info("gfs_pio_seek    : %g sec\n", gfs_pio_seek_time);
	gflog_info("gfs_pio_truncate : %g sec\n", gfs_pio_truncate_time);
	gflog_info("gfs_pio_read    : %g sec\n", gfs_pio_read_time);
	gflog_info("gfs_pio_write   : %g sec\n", gfs_pio_write_time);
	gflog_info("gfs_pio_sync    : %g sec\n", gfs_pio_sync_time);
	gflog_info("gfs_pio_getline : %g sec\n", gfs_pio_getline_time);
}

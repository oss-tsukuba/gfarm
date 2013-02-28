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
#include <pthread.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"
#include "queue.h"
#include "thrsubr.h"

#include "context.h"
#include "liberror.h"
#include "filesystem.h"
#include "gfs_profile.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfs_proto.h"	/* GFS_PROTO_FSYNC_* */
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfp_xdr.h"
#include "gfs_failover.h"
#include "gfs_file_list.h"

#define staticp	(gfarm_ctxp->gfs_pio_static)

struct gfarm_gfs_pio_static {
	double create_time;
	double open_time;
	double close_time;
	double seek_time;
	double truncate_time;
	double read_time;
	double write_time;
	double sync_time;
	double datasync_time;
	double getline_time;
	double getc_time;
	double putc_time;
};

gfarm_error_t
gfarm_gfs_pio_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_pio_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->create_time =
	s->open_time =
	s->close_time =
	s->seek_time =
	s->truncate_time =
	s->read_time =
	s->write_time =
	s->sync_time =
	s->datasync_time =
	s->getline_time =
	s->getc_time =
	s->putc_time = 0;

	ctxp->gfs_pio_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_pio_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_pio_static);
}

struct gfs_file_list {
	GFARM_HCIRCLEQ_HEAD(gfs_file) files;
	pthread_mutex_t mutex;
};

/*
 * GFARM_ERRMSG_GFS_PIO_IS_EOF is used as mark of EOF,
 * and shouldn't be returned to caller functions.
 */

int
gfs_pio_eof(GFS_File gf)
{
	return (gf->error == GFARM_ERRMSG_GFS_PIO_IS_EOF);
}

#define GFS_PIO_ERROR(gf) \
	((gf)->error != GFARM_ERRMSG_GFS_PIO_IS_EOF ? \
	 (gf)->error : GFARM_ERR_NO_ERROR)

gfarm_error_t
gfs_pio_error(GFS_File gf)
{
	return (GFS_PIO_ERROR(gf));
}

void
gfs_pio_clearerr(GFS_File gf)
{
	gf->error = GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
gfs_pio_is_view_set(GFS_File gf)
{
	return (gf->view_context != NULL);
}

gfarm_error_t
gfs_pio_set_view_default(GFS_File gf)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;

	if (gfs_pio_is_view_set(gf)) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = gfs_pio_flush(gf);
		e = (*gf->ops->view_close)(gf);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	gf->ops = NULL;
	gf->view_context = NULL;
#if 0 /* not yet in gfarm v2 */
	gf->view_flags = 0;
#endif /* not yet in gfarm v2 */
	gf->error = e_save;

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001292,
			"gfs_pio_isview_set() is false or "
			"view_close() failed: %s",
			gfarm_error_string(e_save));
	}

	return (e_save);
}

static gfarm_error_t
gfs_pio_check_view_default(GFS_File gf)
{
	gfarm_error_t e;

	e = GFS_PIO_ERROR(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001293,
			"GFS_PIO_ERROR: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (!gfs_pio_is_view_set(gf)) /* view isn't set yet */
#if 0 /* not yet in gfarm v2 */
		return (gfs_pio_set_view_global(gf, 0));
#else /* not yet in gfarm v2 */
		return (gfs_pio_internal_set_view_section(gf, NULL));
#endif /* not yet in gfarm v2 */
	return (GFARM_ERR_NO_ERROR);
}

struct gfm_connection *
gfs_pio_metadb(GFS_File gf)
{
	return (gf->gfm_server);
}

/* gfs_pio_fileno returns a network-wide file descriptor in Gfarm v2 */
int
gfs_pio_fileno(GFS_File gf)
{
	return (gf == NULL ? GFARM_DESCRIPTOR_INVALID : gf->fd);
}

char *
gfs_pio_url(GFS_File gf)
{
	return (gf == NULL ? NULL : gf->url);
}

#ifndef NDEBUG
static int
check_connection_in_file_list(GFS_File gf, void *closure)
{
	struct gfm_connection *gfm_server = closure;

	/*
	 * all gfm_connection related to GFS_File in opened file list MUST be
	 * the same instance to execute failover process against all opened
	 * files at the same time in gfs_pio_failover().
	 */
	assert(gf->gfm_server == gfm_server);
	return (1);
}
#endif

static gfarm_error_t
gfs_file_alloc(struct gfm_connection *gfm_server, gfarm_int32_t fd, int flags,
	char *url, gfarm_ino_t ino, GFS_File *gfp)
{
	GFS_File gf;
	char *buffer;
	struct gfs_file_list *gfl;

	GFARM_MALLOC(gf);
	GFARM_MALLOC_ARRAY(buffer, gfarm_ctxp->client_file_bufsize);
	if (buffer == NULL || gf == NULL) {
		if (buffer != NULL)
			free(buffer);
		if (gf != NULL)
			free(gf);
		gflog_debug(GFARM_MSG_1001294,
			"allocation of GFS_File or it's buffer failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	memset(gf, 0, sizeof(*gf));
	gf->gfm_server = gfm_server;
	gf->fd = fd;
	gf->mode = 0;
	switch (flags & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:
		gf->mode |= GFS_FILE_MODE_READ;
		break;
	case GFARM_FILE_WRONLY:
		gf->mode |= GFS_FILE_MODE_WRITE;
		break;
	case GFARM_FILE_RDWR:
		gf->mode |= GFS_FILE_MODE_READ|GFS_FILE_MODE_WRITE;
		break;
	}

	gf->open_flags = flags;
	gf->error = GFARM_ERR_NO_ERROR;
	gf->io_offset = 0;

	gf->buffer = buffer;
	gf->bufsize = gfarm_ctxp->client_file_bufsize;
	gf->p = 0;
	gf->length = 0;
	gf->offset = 0;
	gf->ino = ino;
	gf->url = url;

	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);

	gfl = gfarm_filesystem_opened_file_list(
	    gfarm_filesystem_get_by_connection(gfm_server));
#ifndef NDEBUG
	gfs_pio_file_list_foreach(gfl, check_connection_in_file_list,
	    gfm_server);
#endif
	gfs_pio_file_list_add(gfl, gf);

	*gfp = gf;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfs_file_free(GFS_File gf)
{
	free(gf->buffer);
	free(gf->url);
	/* do not touch gf->pi here */
	free(gf);
}


gfarm_error_t
gfs_pio_create(const char *url, int flags, gfarm_mode_t mode, GFS_File *gfp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_timerval_t t1, t2;
	char *real_url;
	/* for gfarm_file_trace */
	int src_port;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((e = gfm_create_fd(url, flags, mode, &gfm_server, &fd, &type,
	    &inum, &gen, &real_url)) == GFARM_ERR_NO_ERROR) {
		if (type != GFS_DT_REG) {
			e = type == GFS_DT_DIR ? GFARM_ERR_IS_A_DIRECTORY :
			    type == GFS_DT_LNK ? GFARM_ERR_IS_A_SYMBOLIC_LINK :
			    GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = gfs_file_alloc(gfm_server, fd, flags, real_url,
			    inum, gfp);
		if (e != GFARM_ERR_NO_ERROR) {
			(void)gfm_close_fd(gfm_server, fd); /* ignore result */
			gfm_client_connection_free(gfm_server);
			gflog_debug(GFARM_MSG_1001295,
				"creation of pio for URL (%s) failed: %s",
				url,
				gfarm_error_string(e));
		}
	} else {
		gflog_debug(GFARM_MSG_1001296,
			"creation of file descriptor for URL (%s): %s",
			url,
			gfarm_error_string(e));
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->create_time += gfarm_timerval_sub(&t2, &t1));

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		gfm_client_source_port(gfm_server, &src_port);
		gflog_trace(GFARM_MSG_1003267,
		    "%s/%s/%s/%d/CREATE/%s/%d/%lld/%lld///\"%s\"///",
		    gfarm_get_local_username(),
		    gfm_client_username(gfm_server),
		    gfarm_host_get_self_name(), src_port,
		    gfm_client_hostname(gfm_server),
		    gfm_client_port(gfm_server),
		    (unsigned long long)inum, (unsigned long long)gen, url);
	}

	return (e);
}

gfarm_error_t
gfs_pio_open(const char *url, int flags, GFS_File *gfp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int fd, type;
	gfarm_timerval_t t1, t2;
	gfarm_ino_t ino;
	char *real_url = NULL;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((e = gfm_open_fd_with_ino(url, flags, &gfm_server, &fd, &type,
	    &real_url, &ino)) == GFARM_ERR_NO_ERROR) {
		if (type != GFS_DT_REG) {
			e = type == GFS_DT_DIR ? GFARM_ERR_IS_A_DIRECTORY :
			    type == GFS_DT_LNK ? GFARM_ERR_IS_A_SYMBOLIC_LINK :
			    GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else
			e = gfs_file_alloc(gfm_server, fd, flags, real_url, ino,
			    gfp);
		if (e != GFARM_ERR_NO_ERROR) {
			free(real_url);
			(void)gfm_close_fd(gfm_server, fd); /* ignore result */
			gfm_client_connection_free(gfm_server);
			gflog_debug(GFARM_MSG_1001297,
				"open operation on pio for URL (%s) failed: %s",
				url,
				gfarm_error_string(e));
		}
	} else {
		gflog_debug(GFARM_MSG_1001298,
			"open operation on file descriptor for URL (%s) "
			"failed: %s",
			url,
			gfarm_error_string(e));
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->open_time += gfarm_timerval_sub(&t2, &t1));
	return (e);
}

#if 0 /* not yet in gfarm v2 */

gfarm_error_t
gfs_pio_get_nfragment(GFS_File gf, int *nfragmentsp)
{
	if (GFS_FILE_IS_PROGRAM(gf))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((gf->mode & GFS_FILE_MODE_NSEGMENTS_FIXED) == 0)
		return (GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE);
	*nfragmentsp = gf->pi.status.st_nsections;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* not yet in gfarm v2 */

gfarm_error_t
gfs_pio_close(GFS_File gf)
{
	gfarm_error_t e, e_save;
	gfarm_timerval_t t1, t2;
	struct gfarm_filesystem *fs = gfarm_filesystem_get_by_connection(
		gf->gfm_server);

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	/*
	 * no need to check and set the default file view here
	 * because neither gfs_pio_flush nor view_close is not
	 * needed unless the file view is specified by some
	 * operation.
	 */
	e_save = GFARM_ERR_NO_ERROR;
	if (gfs_pio_is_view_set(gf)) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e_save = gfs_pio_flush(gf);
		e = (*gf->ops->view_close)(gf);
		if (e == GFARM_ERR_GFMD_FAILED_OVER) {
			gflog_error(GFARM_MSG_1003268,
			    "ignore %s error at pio close operation",
			    gfarm_error_string(e));
			gfarm_filesystem_set_failover_detected(fs, 1);
			e = GFARM_ERR_NO_ERROR;
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}

	gfs_pio_file_list_remove(gfarm_filesystem_opened_file_list(fs), gf);

	/*
	 * even if gfsd detectes gfmd failover,
	 * gfm_connection is possibily still alive in client.
	 *
	 * retrying gfm_close_fd is not necessary because fd is
	 * closed in gfmd when the connection is closed.
	 */
	if (gf->fd != GFARM_DESCRIPTOR_INVALID)
		(void)gfm_close_fd(gf->gfm_server, gf->fd);

	gfm_client_connection_free(gf->gfm_server);
	gfs_file_free(gf);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->close_time += gfarm_timerval_sub(&t2, &t1));

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001299,
			"close operation on pio failed: %s",
			gfarm_error_string(e_save));
	}

	return (e_save);
}

static gfarm_error_t
gfs_pio_purge(GFS_File gf)
{
	gf->offset += gf->p;
	gf->p = gf->length = 0;
	return (GFARM_ERR_NO_ERROR);
}

#define CHECK_WRITABLE(gf) { \
	if (((gf)->mode & GFS_FILE_MODE_WRITE) == 0) \
		return (gfarm_errno_to_error(EBADF)); \
	else if ((gf)->error == GFARM_ERRMSG_GFS_PIO_IS_EOF) \
		(gf)->error = GFARM_ERR_NO_ERROR; \
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

static gfarm_error_t
gfs_pio_fillbuf(GFS_File gf, size_t size)
{
	gfarm_error_t e;
	size_t len;

	CHECK_READABLE(gf);

	if (gf->error != GFARM_ERR_NO_ERROR) { /* error or EOF? */
		if (GFS_PIO_ERROR(gf) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001300,
				"CHECK_READABLE failed: %s",
				gfarm_error_string(GFS_PIO_ERROR(gf)));
		}
		return (GFS_PIO_ERROR(gf));
	}
	if (gf->p < gf->length)
		return (GFARM_ERR_NO_ERROR);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = gfs_pio_flush(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001301,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	} else
		gfs_pio_purge(gf);

	if (gf->io_offset != gf->offset) {
		gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
		gf->io_offset = gf->offset;
	}

	e = (*gf->ops->view_pread)(gf, gf->buffer, size, gf->io_offset, &len);
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001302,
			"view_pread() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gf->length = len;
	gf->io_offset += len;
	if (len == 0)
		gf->error = GFARM_ERRMSG_GFS_PIO_IS_EOF;
	return (GFARM_ERR_NO_ERROR);
}

/* unlike other functions, this returns `*writtenp' even if an error happens */
static gfarm_error_t
do_write(GFS_File gf, const char *buffer, size_t length,
	size_t *writtenp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	size_t written, len;

	if (length == 0) {
		*writtenp = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	if (gf->io_offset != gf->offset) {
		gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
		gf->io_offset = gf->offset;
	}
	for (written = 0; written < length; written += len) {
		/* in case of GFARM_FILE_APPEND, io_offset is ignored */
		e = (*gf->ops->view_pwrite)(
			gf, buffer + written, length - written, gf->io_offset,
			&len);
		if (e != GFARM_ERR_NO_ERROR) {
			gf->error = e;
			gflog_debug(GFARM_MSG_1001303,
				"view_pwrite() failed: %s",
				gfarm_error_string(e));
			break;
		}
		gf->io_offset += len;
	}
	*writtenp = written;

	return (e);
}

gfarm_error_t
gfs_pio_flush(GFS_File gf)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);
	size_t written;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001304,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	if ((gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) != 0) {
		e = do_write(gf, gf->buffer, gf->length, &written);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001305,
				"do_write() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;
	}
	if (gf->p >= gf->length)
		gfs_pio_purge(gf);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_seek(GFS_File gf, gfarm_off_t offset, int whence, gfarm_off_t *resultp)
{
	gfarm_error_t e;
	gfarm_off_t where;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001306,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	if (gf->error == GFARM_ERRMSG_GFS_PIO_IS_EOF)
		gf->error = GFARM_ERR_NO_ERROR;

	switch (whence) {
	case GFARM_SEEK_SET:
		where = offset;
		break;
	case GFARM_SEEK_CUR:
		where = offset + gf->offset + gf->p;
		break;
	case GFARM_SEEK_END:
		/* XXX FIXME: ask the file size to gfsd. */
		e = gf->error = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
		gflog_debug(GFARM_MSG_1001307,
			"GFARM_SEEK_END option is not supported: %s",
			gfarm_error_string(e));
		goto finish;
	default:
		e = gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001308,
			"invalid argument whence(%d): %s",
			whence,
			gfarm_error_string(e));
		goto finish;
	}
	if (where < 0) {
		e = gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001309,
			"invalid argument: %s",
			gfarm_error_string(e));
		goto finish;
	}

	/*
	 * This is the case that the file offset will be repositioned
	 * within the current io buffer.  In case of GFARM_FILE_APPEND,
	 * reposition is not allowed if the buffer is dirty.
	 */
	if (((gf->open_flags & GFARM_FILE_APPEND) == 0 ||
	     (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0) &&
	    gf->offset <= where && where <= gf->offset + gf->length) {
		/*
		 * We don't have to clear GFS_FILE_MODE_CALC_DIGEST bit here,
		 * because this is no problem to calculate checksum for
		 * write-only or read-only case.
		 * This is also ok on switching from writing to reading.
		 * This is not ok on switching from reading to writing,
		 * but gfs_pio_flush() clears the bit at that case.
		 */
		gf->p = where - gf->offset;
		if (resultp != NULL)
			*resultp = where;

		e = GFARM_ERR_NO_ERROR;
		goto finish;
	}

	gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = gfs_pio_flush(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gf->error = e;
			gflog_debug(GFARM_MSG_1001310,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}

	e = gf->error = GFARM_ERR_NO_ERROR; /* purge EOF/error state */
	gfs_pio_purge(gf);
	gf->offset = gf->io_offset = where;
	if (resultp != NULL)
		*resultp = where;

 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->seek_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_truncate(GFS_File gf, gfarm_off_t length)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001311,
			"gfs_pio_check_view_default() failed %s",
			gfarm_error_string(e));
		goto finish;
	}

	CHECK_WRITABLE(gf);

	gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;

	if (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) {
		e = gfs_pio_flush(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001312,
				"gfs_pio_flush(): %s",
				gfarm_error_string(e));
			goto finish;
		}
	}

	gf->error = GFARM_ERR_NO_ERROR; /* purge EOF/error state */
	gfs_pio_purge(gf);

	e = (*gf->ops->view_ftruncate)(gf, length);
	if (e != GFARM_ERR_NO_ERROR)
		gf->error = e;
finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->truncate_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_read(GFS_File gf, void *buffer, int size, int *np)
{
	gfarm_error_t e;
	char *p = buffer;
	int n = 0;
	int length;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001313,
			"Check view default for pio failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_READABLE(gf);

	while (size > 0) {
		if ((e = gfs_pio_fillbuf(gf,
		    ((gf->open_flags & GFARM_FILE_UNBUFFERED) &&
		    size < gf->bufsize) ? size : gf->bufsize))
		    != GFARM_ERR_NO_ERROR) {
			/* XXX call reconnect, when failover for writing
			 *     is supported
			 */
			if ((gf->mode & GFS_FILE_MODE_READ) == 0 ||
			    (gf->mode & GFS_FILE_MODE_WRITE) != 0 ||
			    !IS_CONNECTION_ERROR(e))
				break;
			if ((e = gfs_pio_reconnect(gf))
			    != GFARM_ERR_NO_ERROR)
				break;
			continue;
		}
		if (gf->error != GFARM_ERR_NO_ERROR) /* EOF or error */
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
	if (e != GFARM_ERR_NO_ERROR && n == 0) {
		gflog_debug(GFARM_MSG_1001314,
			"gfs_pio_fillbuf() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	if (gf->open_flags & GFARM_FILE_UNBUFFERED)
		gfs_pio_purge(gf);
	*np = n;

	e = GFARM_ERR_NO_ERROR;
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->read_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_write(GFS_File gf, const void *buffer, int size, int *np)
{
	gfarm_error_t e;
	size_t written;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001315,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	if (size + gf->p > gf->bufsize) {
		/*
		 * gf->buffer[gf->p .. gf->bufsize-1] will be overridden
		 * by buffer.
		 */
		gf->length = gf->p;
		e = gfs_pio_flush(gf); /* this does purge too */
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001316,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}
	if (size >= gf->bufsize) {
		/* shortcut to avoid unnecessary memory copy */
		assert(gf->p == 0); /* gfs_pio_flush() was called above */
		gf->length = 0;
		gf->mode &= ~GFS_FILE_MODE_BUFFER_DIRTY;

		e = do_write(gf, buffer, size, &written);
		if (e != GFARM_ERR_NO_ERROR && written == 0) {
			gflog_debug(GFARM_MSG_1001317,
				"do_write() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
		gf->offset += written;
		*np = written; /* XXX - size_t vs int */

		e = GFARM_ERR_NO_ERROR;
		goto finish;
	}
	/* purge the buffer for reading in case of GFARM_FILE_APPEND */
	if ((gf->open_flags & GFARM_FILE_APPEND) &&
	    (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0)
		gfs_pio_purge(gf);
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	memcpy(gf->buffer + gf->p, buffer, size);
	gf->p += size;
	if (gf->p > gf->length)
		gf->length = gf->p;
	*np = size;
	e = GFARM_ERR_NO_ERROR;
	if (gf->open_flags & GFARM_FILE_UNBUFFERED ||
	    gf->p >= gf->bufsize) {
		e = gfs_pio_flush(gf);
		if (gf->open_flags & GFARM_FILE_UNBUFFERED)
			gfs_pio_purge(gf);
	}
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->write_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
sync_internal(GFS_File gf, int operation, double *time)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_flush(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001318,
			"gfs_pio_flush() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	e = (*gf->ops->view_fsync)(gf, operation);
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001319,
			"view_fsync() failed: %s",
			gfarm_error_string(e));
	}
finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(*time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_sync(GFS_File gf)
{
	return (sync_internal(gf, GFS_PROTO_FSYNC_WITH_METADATA,
			      &staticp->sync_time));
}

gfarm_error_t
gfs_pio_datasync(GFS_File gf)
{
	return (sync_internal(gf, GFS_PROTO_FSYNC_WITHOUT_METADATA,
			      &staticp->datasync_time));
}

int
gfs_pio_getc(GFS_File gf)
{
	gfarm_error_t e;
	int c;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001320,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (gf->p >= gf->length) {
		if (gfs_pio_fillbuf(gf,
		    gf->open_flags & GFARM_FILE_UNBUFFERED ?
		    1 : gf->bufsize) != GFARM_ERR_NO_ERROR) {
			c = EOF; /* can get reason via gfs_pio_error() */
			gflog_debug(GFARM_MSG_1001321,
				"gfs_pio_fillbuf() failed: %s",
				gfarm_error_string(GFS_PIO_ERROR(gf)));
			goto finish;
		}
		if (gf->error != GFARM_ERR_NO_ERROR) {
			c = EOF;
			gflog_debug(GFARM_MSG_1001322,
				"gfs_pio_fillbuf() failed: %s",
				gfarm_error_string(gf->error));
			goto finish;
		}
	}
	c = ((unsigned char *)gf->buffer)[gf->p++];
	if (gf->open_flags & GFARM_FILE_UNBUFFERED)
		gfs_pio_purge(gf);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->getc_time += gfarm_timerval_sub(&t2, &t1));
	return (c);
}

int
gfs_pio_ungetc(GFS_File gf, int c)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);

	if (e != GFARM_ERR_NO_ERROR) {
		gf->error = e;
		gflog_debug(GFARM_MSG_1001323,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (EOF);
	}

	CHECK_READABLE_EOF(gf);

	if (c != EOF) {
		if (gf->p == 0) { /* cannot unget - XXX should permit this? */
			gf->error = GFARM_ERR_NO_SPACE;
			gflog_debug(GFARM_MSG_1001324,
				"gfs_pio_ungetc(): %s",
				gfarm_error_string(GFARM_ERR_NO_SPACE));
			return (EOF);
		}
		/* We do not mark this buffer dirty here. */
		gf->buffer[--gf->p] = c;
	}
	return (c);
}

gfarm_error_t
gfs_pio_putc(GFS_File gf, int c)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_check_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001325,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	if (gf->p >= gf->bufsize) {
		gfarm_error_t e = gfs_pio_flush(gf); /* this does purge too */

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001326,
				"gfs_pio_flush() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}
	/* purge the buffer for reading in case of GFARM_FILE_APPEND */
	if ((gf->open_flags & GFARM_FILE_APPEND) &&
	    (gf->mode & GFS_FILE_MODE_BUFFER_DIRTY) == 0)
		gfs_pio_purge(gf);
	gf->mode |= GFS_FILE_MODE_BUFFER_DIRTY;
	gf->buffer[gf->p++] = c;
	if (gf->p > gf->length)
		gf->length = gf->p;
	if (gf->open_flags & GFARM_FILE_UNBUFFERED || gf->p >= gf->bufsize) {
		e = gfs_pio_flush(gf);
		if (gf->open_flags & GFARM_FILE_UNBUFFERED)
			gfs_pio_purge(gf);
	}
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->putc_time += gfarm_timerval_sub(&t2, &t1));
	return (e);
}

/* mostly compatible with fgets(3) */
gfarm_error_t
gfs_pio_puts(GFS_File gf, const char *s)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001327,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	while (*s != '\0') {
		gfarm_error_t e = gfs_pio_putc(gf, *(unsigned char *)s);

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001328,
				"gfs_pio_putc() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		s++;
	}
	return (GFARM_ERR_NO_ERROR);
}

/* mostly compatible with fgets(3), but EOF check is done by *s == '\0' */
gfarm_error_t
gfs_pio_gets(GFS_File gf, char *s, size_t size)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001329,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001330,
			"invalid argument, size (%d) <= 1: %s",
			(int)size,
			gfarm_error_string(gf->error));
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
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_getline(GFS_File gf, char *s, size_t size, int *eofp)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);
	char *p = s;
	int c;
	gfarm_timerval_t t1, t2;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001331,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (size <= 1) {
		gf->error = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1001332,
			"invalid argument, size(%d) <= 1: %s",
			(int)size,
			gfarm_error_string(gf->error));
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
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_putline(GFS_File gf, const char *s)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001333,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	CHECK_WRITABLE(gf);

	e = gfs_pio_puts(gf, s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001334,
			"gfs_pio_puts() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	return (gfs_pio_putc(gf, '\n'));
}

#define ALLOC_SIZE_INIT	220

/*
 * mostly compatible with getline(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
gfarm_error_t
gfs_pio_readline(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0;
	int c;
	size_t alloc_size;
	int overflow = 0;
	gfarm_timerval_t t1, t2;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001335,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	c = EOF;
#endif
	CHECK_READABLE(gf);

	if (buf == NULL || size <= 1) {
		if (size <= 1)
			size = ALLOC_SIZE_INIT;
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL) {
			gflog_debug(GFARM_MSG_1001336,
				"allocation of buf for pio_getc failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
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
				gflog_debug(GFARM_MSG_1001337,
					"allocation of buf for pio_getc "
					"failed or size overflow: %s",
					gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
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
			gflog_debug(GFARM_MSG_1001338,
				"allocation of buf for pio_getc failed "
				"or size overflow: %s",
				gfarm_error_string(
					GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readline_time??? */
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;

	return (GFARM_ERR_NO_ERROR);
}

/*
 * mostly compatible with getdelim(3) in glibc,
 * but there are the following differences:
 * 1. on EOF, *lenp == 0
 * 2. on error, *lenp isn't touched.
 */
gfarm_error_t
gfs_pio_readdelim(GFS_File gf, char **bufp, size_t *sizep, size_t *lenp,
	const char *delim, size_t delimlen)
{
	gfarm_error_t e = gfs_pio_check_view_default(gf);
	char *buf = *bufp, *p = NULL;
	size_t size = *sizep, len = 0, alloc_size;
	int c, delimtail, overflow;
	static const char empty_line[] = "\n\n";
	gfarm_timerval_t t1, t2;

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001339,
			"gfs_pio_check_view_default() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
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
		GFARM_REALLOC_ARRAY(buf, buf, size);
		if (buf == NULL) {
			gflog_debug(GFARM_MSG_1001340,
				"allocation of buf for pio_getc failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
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
				gflog_debug(GFARM_MSG_1001341,
					"allocation of buf for pio_getc failed"
					" or size overflow: %s",
					gfarm_error_string(
						GFARM_ERR_NO_MEMORY));
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
							gflog_debug(
							  GFARM_MSG_1001342,
							  "allocation of buf "
							  "for pio_getc failed"
							  "or size overflow: "
							  "%s",
							  gfarm_error_string(
							    GFARM_ERR_NO_MEMORY
								));
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
			gflog_debug(GFARM_MSG_1001343,
				"allocation of buf for pio_getc failed "
				"or size overflow: %s",
				gfarm_error_string(
					GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		buf = p;
		size += size;
	}
	buf[len] = '\0';

	gfs_profile(gfarm_gettimerval(&t2));
	/* XXX should introduce gfs_pio_readdelim_time??? */
	gfs_profile(staticp->getline_time += gfarm_timerval_sub(&t2, &t1));

	*bufp = buf;
	*sizep = size;
	*lenp = len;

	return (GFARM_ERR_NO_ERROR);
}

/*
 * fstat
 */

gfarm_error_t
gfs_pio_stat(GFS_File gf, struct gfs_stat *st)
{
	gfarm_error_t e;

	e = gfs_fstat(gf, st);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001344,
			"gfs_fstat() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (gfs_pio_is_view_set(gf)) {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0) {
			/* XXX call reconnect, when failover for writing
			 *     is supported
			 */
			if ((e = gfs_pio_flush(gf)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002655,
				    "gfs_pio_flush() failed: %s",
				    gfarm_error_string(e));
			} else if ((e = (*gf->ops->view_fstat)(gf, st))
			    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002656,
				    "view_fstat() failed: %s",
				    gfarm_error_string(e));
			}
		} else if ((e = (*gf->ops->view_fstat)(gf, st))
		     != GFARM_ERR_NO_ERROR && IS_CONNECTION_ERROR(e)) {
			if ((e = gfs_pio_reconnect(gf)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002657,
				    "gfs_pio_reconnect() failed: %s",
				    gfarm_error_string(e));
			} else if ((e = (*gf->ops->view_fstat)(gf, st))
				    != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1002658,
				    "view_stat() failed: %s",
				    gfarm_error_string(e));
			}
		} else if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001345,
				"view_fstat() failed: %s",
				gfarm_error_string(e));
			gf->error = e;
		}
	}
	return (e);
}

#define GFS_FILE_LIST_MUTEX "gfs_file_list.mutex"

struct gfs_file_list *
gfs_pio_file_list_alloc()
{
	struct gfs_file_list *gfl;

	GFARM_MALLOC(gfl);
	if (gfl == NULL)
		return (NULL);
	GFARM_HCIRCLEQ_INIT(gfl->files, hcircleq);
	gfarm_mutex_init(&gfl->mutex, "gfs_pio_file_list_alloc",
	    GFS_FILE_LIST_MUTEX);

	return (gfl);
}

void
gfs_pio_file_list_free(struct gfs_file_list *gfl)
{
	if (gfl == NULL)
		return;
	gfarm_mutex_destroy(&gfl->mutex, "gfs_pio_file_list_free",
	    GFS_FILE_LIST_MUTEX);
	free(gfl);
}

void
gfs_pio_file_list_add(struct gfs_file_list *gfl, GFS_File gf)
{
	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_add",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_INSERT_TAIL(gfl->files, gf, hcircleq);

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_add",
	    GFS_FILE_LIST_MUTEX);
}

void
gfs_pio_file_list_remove(struct gfs_file_list *gfl, GFS_File gf)
{
	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_remove",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_REMOVE(gf, hcircleq);

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_remove",
	    GFS_FILE_LIST_MUTEX);
}

void
gfs_pio_file_list_foreach(struct gfs_file_list *gfl,
	int (*func)(struct gfs_file *, void *), void *closure)
{
	GFS_File gf;

	gfarm_mutex_lock(&gfl->mutex, "gfs_pio_file_list_foreach",
	    GFS_FILE_LIST_MUTEX);

	GFARM_HCIRCLEQ_FOREACH(gf, gfl->files, hcircleq) {
		if (func(gf, closure) == 0)
			break;
	}

	gfarm_mutex_unlock(&gfl->mutex, "gfs_pio_file_list_foreach",
	    GFS_FILE_LIST_MUTEX);
}

void
gfs_pio_display_timers(void)
{
	gflog_info(GFARM_MSG_1000095,
	    "gfs_pio_create  : %g sec", staticp->create_time);
	gflog_info(GFARM_MSG_1000096,
	    "gfs_pio_open    : %g sec", staticp->open_time);
	gflog_info(GFARM_MSG_1000097,
	    "gfs_pio_close   : %g sec", staticp->close_time);
	gflog_info(GFARM_MSG_1000098,
	    "gfs_pio_seek    : %g sec", staticp->seek_time);
	gflog_info(GFARM_MSG_1000099,
	    "gfs_pio_truncate : %g sec", staticp->truncate_time);
	gflog_info(GFARM_MSG_1000100,
	    "gfs_pio_read    : %g sec", staticp->read_time);
	gflog_info(GFARM_MSG_1000101,
	    "gfs_pio_write   : %g sec", staticp->write_time);
	gflog_info(GFARM_MSG_1000102,
	    "gfs_pio_sync    : %g sec", staticp->sync_time);
	gflog_info(GFARM_MSG_1000103,
	    "gfs_pio_getline : %g sec (this calls getc)",
			staticp->getline_time);
	gflog_info(GFARM_MSG_1000104,
	    "gfs_pio_getc : %g sec", staticp->getc_time);
	gflog_info(GFARM_MSG_1000105,
	    "gfs_pio_putc : %g sec", staticp->putc_time);
}

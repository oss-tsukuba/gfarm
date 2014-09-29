/*
 * pio operations for remote fragment
 *
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <sys/time.h>
#include <sys/socket.h> /* struct sockaddr */
#include <openssl/evp.h>
#include <gfarm/gfarm.h>

#include "queue.h"
#include "timer.h"

#include "host.h"
#include "config.h"
#include "gfs_proto.h"	/* GFS_PROTO_FSYNC_* */
#include "gfs_client.h"
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_profile.h"

static double gfs_pio_remote_write_time;
static double gfs_pio_remote_read_time;

static gfarm_error_t
gfs_pio_remote_storage_close(GFS_File gf)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	/*
	 * Do not close remote file from a child process because its
	 * open file count is not incremented.
	 * XXX - This behavior is not the same as expected, but better
	 * than closing the remote file.
	 */
	if (vc->pid != getpid())
		return (GFARM_ERR_NO_ERROR);
	e = gfs_client_close(gfs_server, gf->fd);
	vc->storage_context = NULL;
	gfs_client_connection_free(gfs_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001373,
			"gfs_client_close() failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
gfs_pio_remote_storage_pwrite(GFS_File gf,
	const char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * buffer beyond GFS_PROTO_MAX_IOSIZE are just ignored by gfsd,
	 * we don't perform such GFS_PROTO_WRITE request, because it's
	 * inefficient.
	 * Note that upper gfs_pio layer should care this partial write.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	e = gfs_client_pwrite(gfs_server, gf->fd, buffer, size, offset,
	    lengthp);
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_remote_write_time += gfarm_timerval_sub(&t2, &t1));
	return (e);
}

static gfarm_error_t
gfs_pio_remote_storage_write(GFS_File gf,
	const char *buffer, size_t size, size_t *lengthp,
	gfarm_off_t *offsetp, gfarm_off_t *total_sizep)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	/*
	 * buffer beyond GFS_PROTO_MAX_IOSIZE are just ignored by gfsd,
	 * we don't perform such GFS_PROTO_WRITE request, because it's
	 * inefficient.
	 * Note that upper gfs_pio layer should care this partial write.
	 */
	if (size > GFS_PROTO_MAX_IOSIZE)
		size = GFS_PROTO_MAX_IOSIZE;
	return (gfs_client_write(gfs_server, gf->fd, buffer, size,
	    lengthp, offsetp, total_sizep));
}

static gfarm_error_t
gfs_pio_remote_storage_pread(GFS_File gf,
	char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	/*
	 * Unlike gfs_pio_remote_storage_write(), we don't care
	 * buffer size here, because automatic i/o size truncation
	 * performed by gfsd isn't inefficient for read case.
	 * Note that upper gfs_pio layer should care the partial read.
	 */
	e = gfs_client_pread(gfs_server, gf->fd, buffer, size, offset,
	    lengthp);
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_remote_read_time += gfarm_timerval_sub(&t2, &t1));
	return (e);
}

static gfarm_error_t
gfs_pio_remote_storage_ftruncate(GFS_File gf, gfarm_off_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_ftruncate(gfs_server, gf->fd, length));
}

static gfarm_error_t
gfs_pio_remote_storage_fsync(GFS_File gf, int operation)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_fsync(gfs_server, gf->fd, operation));
}

static gfarm_error_t
gfs_pio_remote_storage_fstat(GFS_File gf, struct gfs_stat *st)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_fstat(gfs_server, gf->fd,
	    &st->st_size, &st->st_atimespec.tv_sec, &st->st_atimespec.tv_nsec,
	    &st->st_mtimespec.tv_sec, &st->st_mtimespec.tv_nsec));
}

static gfarm_error_t
gfs_pio_remote_storage_cksum(GFS_File gf, const char *type,
	char *cksum, size_t size, size_t *lenp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_cksum(gfs_server, gf->fd, type, cksum, size, lenp));
}

static gfarm_error_t
gfs_pio_remote_storage_reopen(GFS_File gf)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	if ((e = gfs_client_open(gfs_server, gf->fd)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003379,
		    "gfs_client_open_local: %s", gfarm_error_string(e));
	return (e);
}

static int
gfs_pio_remote_storage_fd(GFS_File gf)
{
	/*
	 * Unlike Gfarm version 1, we tell the caller that
	 * gfs_client_connection_fd() isn't actually usable.
	 */
	return (-1);
}

struct gfs_storage_ops gfs_pio_remote_storage_ops = {
	gfs_pio_remote_storage_close,
	gfs_pio_remote_storage_fd,
	gfs_pio_remote_storage_pread,
	gfs_pio_remote_storage_pwrite,
	gfs_pio_remote_storage_ftruncate,
	gfs_pio_remote_storage_fsync,
	gfs_pio_remote_storage_fstat,
	gfs_pio_remote_storage_reopen,
	gfs_pio_remote_storage_write,
	gfs_pio_remote_storage_cksum,
};

gfarm_error_t
gfs_pio_open_remote_section(GFS_File gf, struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc = gf->view_context;

	e = gfs_client_open(gfs_server, gf->fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001374,
			"gfs_client_open() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	vc->ops = &gfs_pio_remote_storage_ops;
	vc->storage_context = gfs_server;
	vc->fd = -1; /* not used */
	vc->pid = getpid();
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_pio_remote_display_timers(void)
{
	gflog_info(GFARM_MSG_UNFIXED,
	    "remote read     : %g sec", gfs_pio_remote_read_time);
	gflog_info(GFARM_MSG_UNFIXED,
	    "remote write    : %g sec", gfs_pio_remote_write_time);
}

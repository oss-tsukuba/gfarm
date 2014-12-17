/*
 * pio operations for local section
 *
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h> /* gfs_client.h needs socklen_t */
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>

#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "timer.h"

#include "gfm_proto.h"	/* GFM_PROTO_CKSUM_MAXLEN in gfs_io.h */
#include "gfs_proto.h"	/* GFS_PROTO_FSYNC_* */
#include "gfs_client.h"
#define GFARM_USE_GFS_PIO_INTERNAL_CKSUM_INFO
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_pio_impl.h"
#include "schedule.h"
#include "gfs_profile.h"
#include "context.h"
#ifdef __KERNEL__
#include <linux/fs.h>  /* just for SEEK_XXX */
#endif

#define staticp	(gfarm_ctxp->gfs_pio_local_static)

struct gfarm_gfs_pio_local_static {
	double write_time, read_time;
	unsigned long long write_size, read_size;
	unsigned long long write_count, read_count;
};

gfarm_error_t
gfarm_gfs_pio_local_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_pio_local_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->write_time =
	s->read_time = 0;
	s->write_size =
	s->read_size =
	s->write_count =
	s->read_count = 0;

	ctxp->gfs_pio_local_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_pio_local_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_pio_local_static);
}

#if 0 /* not yet in gfarm v2 */

int gfarm_node = -1;
int gfarm_nnode = -1;

gfarm_error_t
gfs_pio_set_local(int node, int nnode)
{
	if (node < 0 || node >= nnode || nnode < 0)
		return (GFARM_ERR_INVALID_ARGUMENT);

	gfarm_node = node;
	gfarm_nnode = nnode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_set_local_check(void)
{
	if (gfarm_node < 0 || gfarm_nnode <= 0)
		return ("gfs_pio_set_local() is not correctly called");
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_get_node_rank(int *node)
{
	gfarm_error_t e = gfs_pio_set_local_check();

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*node = gfarm_node;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_get_node_size(int *nnode)
{
	gfarm_error_t e = gfs_pio_set_local_check();

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*nnode = gfarm_nnode;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_pio_set_view_local(GFS_File gf, int flags)
{
	gfarm_error_t e;
	char *arch;

	if (GFS_FILE_IS_PROGRAM(gf)) {
		e = gfarm_host_get_self_architecture(&arch);
		if (e != GFARM_ERR_NO_ERROR)
			return (gf->error = e);
		return (gfs_pio_set_view_section(gf, arch, NULL, flags));
	}
	e = gfs_pio_set_local_check();
	if (e != GFARM_ERR_NO_ERROR)
		return (gf->error = e);
	return (gfs_pio_set_view_index(gf, gfarm_nnode, gfarm_node,
				       NULL, flags));
}

#endif /* not yet in gfarm v2 */

/***********************************************************************/

static gfarm_error_t
gfs_pio_local_storage_close(GFS_File gf)
{
	gfarm_error_t e, e2;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	if (close(vc->fd) == -1)
		e = gfarm_errno_to_error(errno);
	else
		e = GFARM_ERR_NO_ERROR;
#ifndef __KERNEL__
	/*
	 * Do not close remote file from a child process because its
	 * open file count is not incremented.
	 * XXX - This behavior is not the same as expected, but better
	 * than closing the remote file.
	 */
	if (vc->pid != getpid()) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001362,
				"close operation on view context "
				"file descriptor failed: %s",
				gfarm_error_string(e));
		}
		return (e);
	}
#endif /* __KERNEL__ */
	if ((gf->mode & GFS_FILE_MODE_MODIFIED) != 0)
		e2 = gfs_client_close_write(
		    gfs_server, gf->fd, GFS_PROTO_CLOSE_FLAG_MODIFIED);
	else
		e2 = gfs_client_close(gfs_server, gf->fd);
	gfarm_schedule_host_unused(
	    gfs_client_hostname(gfs_server),
	    gfs_client_port(gfs_server),
	    gfs_client_username(gfs_server),
	    gf->scheduled_age);

	gfs_client_connection_free(gfs_server);

	if (e != GFARM_ERR_NO_ERROR || e2 != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001363,
			"Close operation on local storage failed: %s",
			gfarm_error_string(
				e != GFARM_ERR_NO_ERROR ? e : e2));
	}

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

static gfarm_error_t
gfs_pio_local_storage_pwrite(GFS_File gf,
	const char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv, save_errno;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	if (gf->open_flags & GFARM_FILE_APPEND)
		rv = write(vc->fd, buffer, size);
	else
		rv = pwrite(vc->fd, buffer, size, offset);
	if (rv == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1003822, "pwrite: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	*lengthp = rv;
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->write_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->write_size += rv);
	gfs_profile(staticp->write_count++);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_pio_local_storage_write(GFS_File gf,
	const char *buffer, size_t size, size_t *lengthp,
	gfarm_off_t *offsetp, gfarm_off_t *total_sizep)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv, save_errno;

	rv = write(vc->fd, buffer, size);
	if (rv == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1003690,
			"write() on view context file descriptor failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	*lengthp = rv;
	*offsetp = lseek(vc->fd, 0, SEEK_CUR) - rv;
	*total_sizep = lseek(vc->fd, 0, SEEK_END);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_pio_local_storage_pread(GFS_File gf,
	char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv, save_errno;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));
	rv = pread(vc->fd, buffer, size, offset);
	if (rv == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1003823, "pread: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	*lengthp = rv;
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->read_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->read_size += rv);
	gfs_profile(staticp->read_count++);
	return (GFARM_ERR_NO_ERROR);
}

/* GFS_PROTO_MAX_IOSIZE is somewhat too large, and is not a power of 2 */
#define COPYFILE_BUFSIZE	65536

/*
 * *writtenp: set even if an error happens.
 */
static gfarm_error_t
gfs_pio_local_copyfile(int r_fd, gfarm_off_t r_off,
	int w_fd, gfarm_off_t w_off, gfarm_off_t len,
	EVP_MD_CTX *md_ctx, gfarm_off_t *writtenp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int until_eof = len < 0;
	int read_mode_unknown = 1, read_mode_thread_safe = 1;
	int write_mode_unknown = 1, write_mode_thread_safe = 1;
	size_t to_read;
	ssize_t rv, i, to_write;
	gfarm_off_t written = 0;
	char buffer[COPYFILE_BUFSIZE];

	if (until_eof || len > 0) {
		for (;;) {
			to_read = until_eof ? sizeof buffer :
			    len < sizeof buffer ?
			    len : sizeof buffer;
			if (read_mode_unknown) {
				read_mode_unknown = 0;
				rv = pread(r_fd, buffer, to_read, r_off);
				if (rv == -1 && errno == ESPIPE &&
				    r_off <= 0) {
					read_mode_thread_safe = 0;
					rv = read(r_fd, buffer, to_read);
				}
			} else if (read_mode_thread_safe) {
				rv = pread(r_fd, buffer, to_read, r_off);
			} else {
				rv = read(r_fd, buffer, to_read);
			}
			if (rv == 0)
				break;
			if (rv == -1) {
				e = gfarm_errno_to_error(errno);
				break;
			}
			r_off += rv;
			if (!until_eof)
				len -= rv;

			for (to_write = rv, i = 0; i < to_write; i += rv) {
				if (write_mode_unknown) {
					write_mode_unknown = 0;
					rv = pwrite(w_fd,
					    buffer + i, to_write - i, w_off);
					if (rv == -1 && errno == ESPIPE &&
					    w_off <= 0) {
						write_mode_thread_safe = 0;
						rv = write(w_fd,
						    buffer + i, to_write - i);
					}
				} else if (write_mode_thread_safe) {
					rv = pwrite(w_fd,
					    buffer + i, to_write - i, w_off);
				} else {
					rv = write(w_fd,
					    buffer + i, to_write - i);
				}
				if (rv == 0) {
					/*
					 * pwrite(2) never returns 0,
					 * so this is just warm fuzzy.
					 */
					e = GFARM_ERR_NO_SPACE;
					break;
				}
				if (rv == -1) {
					e = gfarm_errno_to_error(errno);
					break;
				}
				w_off += rv;
				written += rv;
				if (md_ctx != NULL)
					EVP_DigestUpdate(
					    md_ctx, buffer + i, rv);
			}
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	if (writtenp != NULL)
		*writtenp = written;
	return (e);
}

static gfarm_error_t
gfs_pio_local_storage_recvfile(GFS_File gf, gfarm_off_t r_off,
	int w_fd, gfarm_off_t w_off, gfarm_off_t len,
	EVP_MD_CTX *md_ctx, gfarm_off_t *recvp)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return (gfs_pio_local_copyfile(vc->fd, r_off, w_fd, w_off, len,
	    md_ctx, recvp));
}

static gfarm_error_t
gfs_pio_local_storage_sendfile(GFS_File gf, gfarm_off_t w_off,
	int r_fd, gfarm_off_t r_off, gfarm_off_t len,
	EVP_MD_CTX *md_ctx, gfarm_off_t *sentp)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return (gfs_pio_local_copyfile(r_fd, r_off, vc->fd, w_off, len,
	    md_ctx, sentp));
}

static gfarm_error_t
gfs_pio_local_storage_ftruncate(GFS_File gf, gfarm_off_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv;
#ifdef __KERNEL__
	if (gfarm_ctxp->call_rpc_instead_syscall) {
		struct gfs_connection *gfs_server = vc->storage_context;
		rv =  gfs_client_ftruncate(gfs_server, gf->fd, length);
	} else
#endif /* __KERNEL__ */
	rv = ftruncate(vc->fd, length);
	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001368,
			"ftruncate() on view context file descriptor "
			"failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_pio_local_storage_fsync(GFS_File gf, int operation)
{
	struct gfs_file_section_context *vc = gf->view_context;
	int rv;

	switch (operation) {
	case GFS_PROTO_FSYNC_WITHOUT_METADATA:
#ifdef HAVE_FDATASYNC
		rv = fdatasync(vc->fd);
		break;
#else
		/*FALLTHROUGH*/
#endif
	case GFS_PROTO_FSYNC_WITH_METADATA:
		rv = fsync(vc->fd);
		break;
	default:
		gflog_debug(GFARM_MSG_1001369,
			"Invalid operation (%d): %s",
			operation,
			gfarm_error_string(GFARM_ERR_INVALID_ARGUMENT));
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001370,
			"fsync() or fdatasync() on view context "
			"file descriptor failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_pio_local_storage_fstat(GFS_File gf, struct gfs_stat *st)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct stat sb;

	if (fstat(vc->fd, &sb) == 0) {
		st->st_size = sb.st_size;
		st->st_atimespec.tv_sec = sb.st_atime;
		st->st_atimespec.tv_nsec = gfarm_stat_atime_nsec(&sb);
		st->st_mtimespec.tv_sec = sb.st_mtime;
		st->st_mtimespec.tv_nsec = gfarm_stat_mtime_nsec(&sb);
	} else {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001371,
			"fstat() on view context file descriptor failed : %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_pio_local_storage_cksum(GFS_File gf, const char *type,
	char *cksum, size_t size, size_t *lenp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	return (gfs_client_cksum(gfs_server, gf->fd, type, cksum, size, lenp));
}

static gfarm_error_t
gfs_pio_local_storage_reopen(GFS_File gf)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *gfs_server = vc->storage_context;

	if (close(vc->fd) == -1) {
		/* this shouldn't happen */
		gflog_error_errno(GFARM_MSG_1003961,
		    "closing obsolete local fd during gfmd failover");
	}
	vc->fd = -1;
	if ((e = gfs_client_open_local(gfs_server, gf->fd, &vc->fd)) !=
	    GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003378,
		    "gfs_client_open_local: %s", gfarm_error_string(e));
	return (e);
}

static int
gfs_pio_local_storage_fd(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return (vc->fd);
}

struct gfs_storage_ops gfs_pio_local_storage_ops = {
	gfs_pio_local_storage_close,
	gfs_pio_local_storage_fd,
	gfs_pio_local_storage_pread,
	gfs_pio_local_storage_pwrite,
	gfs_pio_local_storage_ftruncate,
	gfs_pio_local_storage_fsync,
	gfs_pio_local_storage_fstat,
	gfs_pio_local_storage_reopen,
	gfs_pio_local_storage_write,
	gfs_pio_local_storage_cksum,
	gfs_pio_local_storage_recvfile,
	gfs_pio_local_storage_sendfile,
};

gfarm_error_t
gfs_pio_open_local_section(GFS_File gf, struct gfs_connection *gfs_server)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e;

	e = gfs_client_open_local(gfs_server, gf->fd, &vc->fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001372,
			"gfs_client_open_local() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	vc->ops = &gfs_pio_local_storage_ops;
	vc->storage_context = gfs_server;
	vc->pid = getpid();
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_pio_local_display_timers(void)
{
	gflog_info(GFARM_MSG_1003824,
	    "local read time   : %g sec", staticp->read_time);
	gflog_info(GFARM_MSG_1003825,
	    "local read size   : %llu", staticp->read_size);
	gflog_info(GFARM_MSG_1003826,
	    "local read count  : %llu", staticp->read_count);
	gflog_info(GFARM_MSG_1003827,
	    "local write time  : %g sec", staticp->write_time);
	gflog_info(GFARM_MSG_1003828,
	    "local write size  : %llu", staticp->write_size);
	gflog_info(GFARM_MSG_1003829,
	    "local write count : %llu", staticp->write_count);
}

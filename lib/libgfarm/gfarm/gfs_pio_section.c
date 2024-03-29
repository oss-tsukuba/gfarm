/*
 * pio operations for file fragments or programs
 *
 * $Id$
 */

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/evp.h>
#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"
#include "queue.h"
#define GFARM_USE_OPENSSL
#include "msgdigest.h"

#include "context.h"
#include "liberror.h"
#include "gfs_profile.h"
#include "host.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "gfm_schedule.h"
#include "gfs_client.h"
#include "gfs_proto.h"
#define GFARM_USE_GFS_PIO_INTERNAL_CKSUM_INFO
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_pio_impl.h"
#include "schedule.h"
#include "filesystem.h"
#include "gfs_failover.h"

#define staticp	(gfarm_ctxp->gfs_pio_section_static)

struct gfarm_gfs_pio_section_static {
	double set_view_section_time;
	unsigned long long open_local_count;
	unsigned long long open_remote_count;
};

gfarm_error_t
gfarm_gfs_pio_section_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_pio_section_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->set_view_section_time = 0;
	s->open_local_count =
	s->open_remote_count = 0;

	ctxp->gfs_pio_section_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_pio_section_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_pio_section_static);
}

static gfarm_error_t
gfs_pio_view_section_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e_save = GFARM_ERR_NO_ERROR;

	e = (*vc->ops->storage_close)(gf);
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;

	free(vc);
	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001346,
			"storage_close() failed: %s",
			gfarm_error_string(e_save));
	}

	return (e_save);
}

static gfarm_error_t
gfs_pio_view_section_pwrite(GFS_File gf,
	const char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = (*vc->ops->storage_pwrite)(gf,
	    buffer, size, offset, lengthp);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001347,
			"storage_pwrite() failed: %s",
			gfarm_error_string(e));
	} else if (*lengthp > 0) {
		gf->mode |= GFS_FILE_MODE_MODIFIED;
		/*
		 * gf->md.filesize may be incorrect, if this file is
		 * simultaneously written via multiple descriptors,
		 * but we don't have to care such case, because
		 * the cksum will be invalidated by gfmd.
		 */
		if ((gf->open_flags & GFARM_FILE_APPEND) != 0)
			offset = gf->md.filesize;
		if (gf->md.filesize < offset + *lengthp)
			gf->md.filesize = offset + *lengthp;
		if ((gf->mode &
		    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
		    (GFS_FILE_MODE_DIGEST_CALC)) {
			if (gf->md_offset != offset) {
				gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
			} else {
				EVP_DigestUpdate(
				    gf->md_ctx, buffer, *lengthp);
				gf->md_offset += *lengthp;
			}
		}
	}
	return (e);
}

static gfarm_error_t
gfs_pio_view_section_write(GFS_File gf,
	const char *buffer, size_t size, size_t *lengthp,
	gfarm_off_t *offsetp, gfarm_off_t *total_sizep)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = (*vc->ops->storage_write)(gf,
	    buffer, size, lengthp, offsetp, total_sizep);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003689,
			"storage_write() failed: %s",
			gfarm_error_string(e));
	} else if (*lengthp > 0) {
		gf->mode |= GFS_FILE_MODE_MODIFIED;
		if (gf->md.filesize < *offsetp + *lengthp)
			gf->md.filesize = *offsetp + *lengthp;
		if ((gf->mode &
		    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
		    (GFS_FILE_MODE_DIGEST_CALC)) {
			if (gf->md_offset != *offsetp) {
				gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
			} else {
				EVP_DigestUpdate(
				    gf->md_ctx, buffer, *lengthp);
				gf->md_offset += *lengthp;
			}
		}
	}

	return (e);
}

static gfarm_error_t
gfs_pio_view_section_pread(GFS_File gf,
	char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = (*vc->ops->storage_pread)(gf,
	    buffer, size, offset, lengthp);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001348,
			"storage_pread failed: %s",
			gfarm_error_string(e));
	} else if (*lengthp > 0 && (gf->mode &
	    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
	    (GFS_FILE_MODE_DIGEST_CALC)) {
		if (gf->md_offset != offset) {
			gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
		} else {
			/*
			 * There is alternative strategy that we calls
			 * gfs_pio_md_finish() only if
			 * the GFS_FILE_MODE_DIGEST_AVAIL flags is set.
			 * With that strategy, we may be able to set
			 * new checksum, if the actual filesize is larger
			 * than the metadata due to file modification by
			 * other processes.
			 * But that's a rare case and we don't check the
			 * AVAIL flags for consistency with the gfsd
			 * implementation.
			 */
			EVP_DigestUpdate(gf->md_ctx, buffer, *lengthp);
			gf->md_offset += *lengthp;
			if (gf->md_offset == gf->md.filesize && (gf->mode &
			    GFS_FILE_MODE_MODIFIED) == 0)
				e = gfs_pio_md_finish(gf);
		}
	}

	return (e);
}

static gfarm_error_t
gfs_pio_view_section_recvfile(GFS_File gf, gfarm_off_t r_off,
	int w_fd, gfarm_off_t w_off, gfarm_off_t len, gfarm_off_t *recvp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	EVP_MD_CTX *md_ctx = NULL;
	gfarm_error_t e;
	gfarm_off_t recv = 0;

	if ((gf->mode &
	    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
	    (GFS_FILE_MODE_DIGEST_CALC)) {
		if (gf->md_offset != r_off) {
			gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
		} else {
			md_ctx = gf->md_ctx;
		}
	}
	e = (*vc->ops->storage_recvfile)(gf, r_off, w_fd, w_off, len,
	    md_ctx, &recv);

	/* recv is set even if e != GFARM_ERR_NO_ERROR */
	if (md_ctx != NULL) {
		gf->md_offset += recv;
		if (e == GFARM_ERR_NO_ERROR &&
		    gf->md_offset == gf->md.filesize && (gf->mode &
		    (GFS_FILE_MODE_MODIFIED|GFS_FILE_MODE_DIGEST_AVAIL))
		    == GFS_FILE_MODE_DIGEST_AVAIL)
			e = gfs_pio_md_finish(gf);
	}
	if (recvp != NULL)
		*recvp = recv;

	return (e);
}

static gfarm_error_t
gfs_pio_view_section_sendfile(GFS_File gf, gfarm_off_t w_off,
	int r_fd, gfarm_off_t r_off, gfarm_off_t len, gfarm_off_t *sentp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	EVP_MD_CTX *md_ctx = NULL;
	gfarm_error_t e;
	gfarm_off_t sent = 0;

	if ((gf->mode &
	    (GFS_FILE_MODE_DIGEST_CALC|GFS_FILE_MODE_DIGEST_FINISH)) ==
	    (GFS_FILE_MODE_DIGEST_CALC)) {
		/*
		 * gf->md.filesize may be incorrect, if this file is
		 * simultaneously written via multiple descriptors,
		 * but we don't have to care such case, because
		 * the cksum will be invalidated by gfmd.
		 */
		if ((gf->open_flags & GFARM_FILE_APPEND) != 0)
			w_off = gf->md.filesize;
		if (gf->md_offset != w_off) {
			gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
		} else {
			md_ctx = gf->md_ctx;
		}
	}
	e = (*vc->ops->storage_sendfile)(gf, w_off, r_fd, r_off, len,
	    md_ctx, &sent);

	/* sent is set even if e != GFARM_ERR_NO_ERROR */
	if (sent > 0) {
		gf->mode |= GFS_FILE_MODE_MODIFIED;
		if (gf->md.filesize < w_off + sent)
			gf->md.filesize = w_off + sent;
	}
	if (md_ctx != NULL)
		gf->md_offset += sent;
	if (sentp != NULL)
		*sentp = sent;

	return (e);
}

static gfarm_error_t
gfs_pio_view_section_ftruncate(GFS_File gf, gfarm_off_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	e = (*vc->ops->storage_ftruncate)(gf, length);
	if (e == GFARM_ERR_NO_ERROR) {
		gf->mode |= GFS_FILE_MODE_MODIFIED;
		if ((gf->mode & GFS_FILE_MODE_DIGEST_CALC) != 0) {
			if (length == 0) {
				if ((gf->mode & GFS_FILE_MODE_DIGEST_FINISH)
				    == 0) {
					/* to avoid memory leak */
					gfarm_msgdigest_free(
					    gf->md_ctx, md_value);
					gf->mode |=
					    GFS_FILE_MODE_DIGEST_FINISH;
				}
				if (!gfs_pio_md_init(gf->md.cksum_type,
				    &gf->md_ctx, gfs_pio_url(gf))) {
					free(gf->md.cksum_type);
					gf->md.cksum_type = NULL;
					gf->mode &=
					    ~GFS_FILE_MODE_DIGEST_CALC;
				} else {
					gf->mode &=
					    ~GFS_FILE_MODE_DIGEST_FINISH;
					gf->md_offset = 0;
				}
			} else if (length < gf->md_offset)
				gf->mode &= ~GFS_FILE_MODE_DIGEST_CALC;
		}
		gf->md.filesize = length;
	}
	return (e);
}

static gfarm_error_t
gfs_pio_view_section_fsync(GFS_File gf, int operation)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_fsync)(gf, operation));
}

static gfarm_error_t
gfs_pio_view_section_fstat(GFS_File gf, struct gfs_stat *st)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_fstat)(gf, st));
}

static gfarm_error_t
gfs_pio_view_section_cksum(GFS_File gf, const char *type,
	struct gfs_stat_cksum *sum)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char cksum[GFM_PROTO_CKSUM_MAXLEN], *tmp_type, *tmp_cksum;
	size_t size = sizeof cksum, len;
	gfarm_error_t e;

	e = (*vc->ops->storage_cksum)(gf, type, cksum, size, &len);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	tmp_type = strdup(type);
	tmp_cksum = malloc(len + 1);
	if (tmp_type == NULL || tmp_cksum == NULL) {
		free(tmp_type);
		free(tmp_cksum);
		return (GFARM_ERR_NO_MEMORY);
	}
	sum->type = tmp_type;
	sum->len = len;
	sum->cksum = tmp_cksum;
	sum->flags = 0;
	memcpy(sum->cksum, cksum, len);
	sum->cksum[len] = '\0';
	return (e);
}

static gfarm_error_t
gfs_pio_view_section_reopen(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_reopen)(gf));
}

static int
gfs_pio_view_section_fd(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_fd)(gf));
}

struct gfs_pio_ops gfs_pio_view_section_ops = {
	gfs_pio_view_section_close,
	gfs_pio_view_section_fd,
	gfs_pio_view_section_pread,
	gfs_pio_view_section_pwrite,
	gfs_pio_view_section_ftruncate,
	gfs_pio_view_section_fsync,
	gfs_pio_view_section_fstat,
	gfs_pio_view_section_reopen,
	gfs_pio_view_section_write,
	gfs_pio_view_section_cksum,
	gfs_pio_view_section_recvfile,
	gfs_pio_view_section_sendfile,
};


#if 0 /* not yet in gfarm v2 */

static gfarm_error_t
replicate_section_to_local(GFS_File gf, char *section,
	char *src_canonical_hostname, char *src_if_hostname)
{
	gfarm_error_t e;
	int if_hostname_alloced = 0;
	struct gfarm_file_section_info sinfo;

	if (src_if_hostname == NULL) {
		struct sockaddr peer_addr;

		e = gfarm_host_address_get(src_canonical_hostname,
		    gfarm_spool_server_port, &peer_addr, &src_if_hostname);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		if_hostname_alloced = 1;
	}

	/* gf->pi.status.st_size does not have the file size... */
	e = gfarm_file_section_info_get(gf->pi.pathname, section, &sinfo);
	if (e != GFARM_ERR_NO_ERROR)
		goto finish;
	e = gfarm_file_section_replicate_from_to_local_with_locking(
	    &sinfo, gf->pi.status.st_mode,
	    src_canonical_hostname, src_if_hostname, NULL);

	gfarm_file_section_info_free(&sinfo);
finish:
	if (if_hostname_alloced)
		free(src_if_hostname);
	return (e);
}

#endif /* not yet in gfarm v2 */

static gfarm_error_t
gfs_pio_open_section(GFS_File gf, struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	int nretry = GFS_FAILOVER_RETRY_COUNT;
	int is_local = gfs_client_connection_is_local(gfs_server);

retry:
	if ((e = is_local ?
	    gfs_pio_open_local_section(gf, gfs_server) :
	    gfs_pio_open_remote_section(gf, gfs_server))
	    == GFARM_ERR_NO_ERROR) {
		gfs_profile(
			if (is_local)
				++staticp->open_local_count;
			else
				++staticp->open_remote_count);
		return (e);
	}

	gflog_debug(GFARM_MSG_1003953,
	    "gfs_pio_open_%s_section: %s",
	    is_local ? "local" : "remote", gfarm_error_string(e));

	/*
	 * We use gfs_pio_should_failover_at_gfs_open() instead of
	 * gfs_pio_should_failover() here to avoid the following case:
	 * (1) gfs_pio_open(..., &gf1) is called.
	 * (2) gfs_pio_open(..., &gf2) is called.
	 * (3) gfs_pio_read(gf1, ...) is called and
	 *    this client connects gfsd-1.
	 * (4) gfs_pio_read(gf2, ...) is called and gfsd-1 is scheduled.
	 * (5) for some reason, the connection between this client and gfmd
	 *    is lost (e.g. TCP ACK packets from this client are all lost),
	 *    but the connection between gfsd-1 and gfmd is kept.
	 * (6) To deal with (4), this function requests of gfsd-1 that
	 *   it should open gf2->fd, but because gf2->fd is closed
	 *   in the gfmd side due to (5), gfs_pio_open_{local,remote}_section()
	 *   call above fails with GFARM_ERR_BAD_FILE_DESCRIPTOR.
	 */
	if (gfs_pio_should_failover_at_gfs_open(gf, e) && nretry-- > 0) {
		if ((e = gfs_pio_failover(gf)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003954,
			    "gfs_pio_failover: %s", gfarm_error_string(e));
			return (e);
		}
		if (gfarm_filesystem_failover_count(
			gfarm_filesystem_get_by_connection(gf->gfm_server))
		    != gfs_client_connection_failover_count(gfs_server)) {
			/*
			 * gfs_server is not set to any opened file list
			 * in gfarm_filesystem. so gfs_server did not fail
			 * over.
			 */
			gflog_debug(GFARM_MSG_1003955,
			    "reset_process");
			if ((e = gfarm_client_process_reset(gfs_server,
			    gf->gfm_server)) != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_1003956,
				    "gfarm_client_process_reset: %s",
				    gfarm_error_string(e));
				return (e);
			}
		}
		goto retry;
	}
	return (e);
}

static gfarm_error_t
connect_and_open(GFS_File gf, const char *hostname, int port)
{
	gfarm_error_t e;
	struct gfs_connection *gfs_server;
	int nretry = 1;
	gfarm_timerval_t t1, t2, t3;

	GFARM_KERNEL_UNUSE3(t1, t2, t3);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);

	gfs_profile(gfarm_gettimerval(&t1));

retry:
	gfm_client_connection_addref(gf->gfm_server);
	e = gfs_client_connection_and_process_acquire(&gf->gfm_server,
	    hostname, port, &gfs_server, NULL);
	gfm_client_connection_delref(gf->gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001350,
		    "acquirement of client connection failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	gfs_profile(gfarm_gettimerval(&t2));

	if ((e = gfs_pio_open_section(gf, gfs_server)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003957,
		    "gfs_pio_open_section: %s",
		    gfarm_error_string(e));
		gfs_client_connection_free(gfs_server);
		if (gfs_client_is_connection_error(e) && nretry-- > 0)
			goto retry;
	} else {
		gf->scheduled_age = gfarm_schedule_host_used(hostname, port,
		    gfs_client_username(gfs_server));
	}

	gfs_profile(
		gfarm_gettimerval(&t3);
		gflog_debug(GFARM_MSG_1003958,
		    "(connect_and_open) connection_acquire/process_set %f, "
			   "open %f",
			   gfarm_timerval_sub(&t2, &t1),
			   gfarm_timerval_sub(&t3, &t2)));
	return (e);
}

static gfarm_error_t
choose_trivial_one(struct gfarm_host_sched_info *info,
	char **hostp, gfarm_int32_t *portp)
{
	char *host;

	/* no choice */
	host = strdup(info->host);
	if (host == NULL) {
		gflog_debug(GFARM_MSG_1001352,
			"allocation of 'host' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	*hostp = host;
	*portp = info->port;
	return (GFARM_ERR_NO_ERROR);
}

/* *hostp needs to free'ed if succeed */
static gfarm_error_t
gfarm_schedule_file_cache(GFS_File gf, char **hostp, gfarm_int32_t *portp,
			int *ncachep)
{
	gfarm_error_t e;
	int nhosts;
	struct gfarm_host_sched_info *infos;
	char *host = NULL;
	gfarm_int32_t port;
	gfarm_timerval_t t1, t2, t3;
#ifndef __KERNEL__
	int i;
#endif /* __KERNEL__ */
	/*
	 * XXX FIXME: Or, call replicate_section_to_local(), if that's prefered
	 */
	GFARM_KERNEL_UNUSE3(t1, t2, t3);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t3);

	if (ncachep)
		*ncachep = 0;

	gfs_profile(gfarm_gettimerval(&t1));
	e = gfm_schedule_file(gf, &nhosts, &infos);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001353,
			"gfm_schedule_file() failed: %s",
			gfarm_error_string(e));
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = GFARM_ERR_INPUT_OUTPUT;
		return (e);
	}
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gflog_debug(GFARM_MSG_1000109,
	    "schedule_file -> %d hosts", nhosts));
	gfs_profile(
		for (i = 0; i < nhosts; ++i)
			gflog_debug(GFARM_MSG_1000110, "<%s>", infos[i].host));

	if (nhosts == 1)
		e = choose_trivial_one(&infos[0], &host, &port);
	else {
		if (ncachep)
			*ncachep = gfs_client_connection_cache_change(nhosts);
		e = gfarm_schedule_select_host(gf->gfm_server, nhosts, infos,
		    (gf->mode & GFS_FILE_MODE_WRITE) != 0, &host, &port);
	}
	/* in case of no file system node, clear status of connection cache */
	if (e == GFARM_ERR_NO_FILESYSTEM_NODE)
		gfarm_schedule_host_cache_reset(gf->gfm_server, nhosts, infos);
	gfarm_host_sched_info_free(nhosts, infos);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001354, "schedule_select_host: %s",
		    gfarm_error_string(e));

	/* on-demand replication */
	if (gfarm_ctxp->on_demand_replication &&
	    e == GFARM_ERR_NO_ERROR &&
	    !gfm_canonical_hostname_is_local(gf->gfm_server, host)) {
		e = gfs_replicate_to_local(gf, host, port);
		if (e == GFARM_ERR_NO_ERROR) {
			free(host);
			/*
			 * We don't have to check gfmd failover here.
			 * because gfs_replicate_to_local() already called
			 * gfm_host_get_canonical_self_nam(), and the result
			 * was internally cached.
			 */
			e = gfm_host_get_canonical_self_name(
			    gf->gfm_server, &host, &port);
			host = strdup(host);
			if (host == NULL)
				e = GFARM_ERR_NO_MEMORY;
		} else if (e == GFARM_ERR_ALREADY_EXISTS ||
			 e == GFARM_ERR_UNKNOWN_HOST) {
			/*
			 * local host is too busy to select or unknown
			 * host
			 */
			e = GFARM_ERR_NO_ERROR;
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		if (host != NULL)
			free(host);
		gflog_debug(GFARM_MSG_1001355,
			"error occurred in gfarm_schedule_file(): %s",
			gfarm_error_string(e));
		return (e);
	}
	gfs_profile(
		gflog_debug(GFARM_MSG_1000111, "host -> %s", host);
		gfarm_gettimerval(&t3);
		gflog_debug(GFARM_MSG_1000112,
		    "(gfarm_schedule_file) schedule %f, select %f",
			   gfarm_timerval_sub(&t2, &t1),
			   gfarm_timerval_sub(&t3, &t2)));
	*hostp = host;
	*portp = port;

	return (e);
}
gfarm_error_t
gfarm_schedule_file(GFS_File gf, char **hostp, gfarm_int32_t *portp)
{
	return (gfarm_schedule_file_cache(gf, hostp, portp, NULL));
}

static gfarm_error_t
connect_and_open_with_reconnection(GFS_File gf, char *host, gfarm_int32_t port)
{
	gfarm_error_t e;

	e = connect_and_open(gf, host, port);
	if (gfs_client_is_connection_error(e))
		e = connect_and_open(gf, host, port);

	return (e);
}

static gfarm_error_t
schedule_file_loop(GFS_File gf, char *host, gfarm_int32_t port)
{
	struct timeval expiration_time;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int host_assigned = 0;
	int sleep_interval = 1, sleep_max_interval = 512;
	int nc = 0;

	gettimeofday(&expiration_time, NULL);
	expiration_time.tv_sec += gfarm_ctxp->no_file_system_node_timeout;
	for (;;) {
		if (host == NULL) {
			e = gfarm_schedule_file_cache(gf, &host, &port, &nc);
			/* reschedule another host */
			if (e == GFARM_ERR_NO_FILESYSTEM_NODE &&
			    !gfarm_timeval_is_expired(&expiration_time)) {
				gflog_info(GFARM_MSG_1001359,
				    "sleep %d sec: %s", sleep_interval,
				    gfarm_error_string(e));
				gfs_client_connection_cache_change(-nc);
				nc = 0;
				sleep(sleep_interval);
				if (sleep_interval < sleep_max_interval)
					sleep_interval *= 2;
				continue;
			}
			if (e == GFARM_ERR_NO_ERROR)
				host_assigned = 1;
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = connect_and_open_with_reconnection(gf, host, port);

		if (nc > 0) {
			gfs_client_connection_cache_change(-nc);
			nc = 0;
		}

		if (host_assigned) {
			free(host);
			host = NULL;
			host_assigned = 0;
			/*
			 * reschedule another host unless host is
			 * explicitly specified
			 */
			if ((e == GFARM_ERR_NO_FILESYSTEM_NODE ||
			     e == GFARM_ERR_FILE_MIGRATED ||
			     e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE ||
			    gfs_client_is_connection_error(e)) &&
			    !gfarm_timeval_is_expired(&expiration_time)) {
				if (e == GFARM_ERR_FILE_MIGRATED) {
					/* don't have to sleep in this case */
					gflog_debug(GFARM_MSG_1002472,
					    "file migrated");
					continue;
				}
				gflog_info(GFARM_MSG_1001360,
				    "sleep %d sec: %s", sleep_interval,
				    gfarm_error_string(e));
				sleep(sleep_interval);
				if (sleep_interval < sleep_max_interval)
					sleep_interval *= 2;
				continue;
			}
		}
		break;
	}
	return (e);
}

static struct gfs_file_section_context *
gfs_file_section_context_alloc(void)
{
	gfarm_error_t e;
	struct gfs_file_section_context *vc;

	GFARM_MALLOC(vc);
	if (vc == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003959,
		    "allocation of file section context failed: %s",
		    gfarm_error_string(e));
		return (NULL);
	}

	vc->storage_context = NULL;
	vc->pid = 0;

	return (vc);
}

/* internal function: do not call external functions */
gfarm_error_t
gfs_pio_internal_set_view_section(GFS_File gf, char *host)
{
	struct gfs_file_section_context *vc;
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	gfarm_int32_t port = 0;

	GFARM_KERNEL_UNUSE2(t1, t2);
	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_set_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001356,
			"gfs_pio_set_view_default() failed: %s",
			gfarm_error_string(e));
		goto finish;
	}

	if (host != NULL) { /* this is slow, but not usually used */
		struct gfarm_host_info hinfo;
		int nretries = GFS_FAILOVER_RETRY_COUNT;

		for (;;) {
			e = gfm_host_info_get_by_name_alias(gf->gfm_server,
			    host, &hinfo);
			if (e == GFARM_ERR_NO_ERROR) {
				port = hinfo.port;
				gfarm_host_info_free(&hinfo);
				break;
			}
			if (gfm_client_connection_should_failover(
			    gf->gfm_server, e) && nretries-- > 0) {
				if ((e = gfs_pio_failover(gf))
				    == GFARM_ERR_NO_ERROR)
					continue;
				gflog_debug(GFARM_MSG_1003960,
				    "gfs_pio_failover: %s",
				    gfarm_error_string(e));
				goto finish;
			}
			gflog_debug(GFARM_MSG_1001357,
				"gfm_host_info_get_by_name_alias() failed: %s",
				gfarm_error_string(e));
			goto finish;
		}
	}

	vc = gfs_file_section_context_alloc();
	if (vc == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001358,
			"allocation of file section context failed: %s",
			gfarm_error_string(e));
		goto finish;
	}
	gf->view_context = vc;

	if ((e = schedule_file_loop(gf, host, port)) == GFARM_ERR_NO_ERROR) {
		gf->ops = &gfs_pio_view_section_ops;
		gf->p = gf->length = 0;
		gf->io_offset = gf->offset = 0;

#if 1 /* not yet in gfarm v2  */
		goto finish;
#else /* not yet in gfarm v2  */
		gf->mode |= GFS_FILE_MODE_CALC_DIGEST;
		vc->md_ctx = gfarm_msgdigest_alloc(GFS_DEFAULT_DIGEST_MODE);
		if (vc->md_ctx == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1004519,
			    "msgdigest allocation failed: %s",
			    gfarm_error_string(e));
			goto finish;
		}

		if (gf->open_flags & GFARM_FILE_APPEND) {
			e = gfs_pio_seek_unlocked(gf, 0, SEEK_END, NULL);
			if (e == GFARM_ERR_NO_ERROR)
				goto finish;
			(*vc->ops->storage_close)(gf);
		}
#endif /* not yet in gfarm v2 */
	} else {
		gflog_debug(GFARM_MSG_1001361,
			"error occurred in gfs_pio_internal_set_view_"
			"section(): %s",
			gfarm_error_string(e));
	}
	free(vc);

	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);
finish:
	gf->error = e;

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->set_view_section_time
		    += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_reconnect(GFS_File gf)
{
	gfarm_error_t e;
	char *host;
	struct gfs_file_section_context *vc = gf->view_context;
	struct gfs_connection *nsc, *sc = vc->storage_context;

	if ((gf->mode & GFS_FILE_MODE_READ) == 0 ||
	    (gf->mode & GFS_FILE_MODE_WRITE) != 0) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002659,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	if ((e = gfm_client_revoke_gfsd_access_request(gf->gfm_server, gf->fd))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002660,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfm_client_revoke_gfsd_access_result(gf->gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002661,
		    "%s", gfarm_error_string(e));
		return (e);
	}

	host = strdup(gfs_client_hostname(sc));
	if (host == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002662,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	gfarm_schedule_host_cache_purge(sc);
	if ((e = schedule_file_loop(gf, NULL, 0)) != GFARM_ERR_NO_ERROR)
		goto end;
	vc = gf->view_context;
	nsc = vc->storage_context;
	if (strcmp(host, gfs_client_hostname(nsc)) == 0) {
		e = GFARM_ERR_INVALID_FILE_REPLICA;
		gflog_debug(GFARM_MSG_1002663,
		    "%s", gfarm_error_string(e));
	}
	gfs_client_connection_free(sc);
	gf->error = GFARM_ERR_NO_ERROR;
end:
	free(host);

	return (e);
}

#if 0 /* not yet in gfarm v2 */

gfarm_error_t
gfs_pio_set_view_section(GFS_File gf, const char *section,
			 char *if_hostname, int flags)
{
	struct gfs_file_section_context *vc;
	gfarm_error_t e;
	int is_local_host;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_set_view_default(gf);
	if (e != GFARM_ERR_NO_ERROR)
		goto profile_finish;

	GFARM_MALLOC(vc);
	if (vc == NULL) {
		e = gf->error = GFARM_ERR_NO_MEMORY;
		goto profile_finish;
	}

	vc->section = strdup(section);
	if (vc->section == NULL) {
		free(vc);
		e = gf->error = GFARM_ERR_NO_MEMORY;
		goto profile_finish;
	}

	/* determine vc->canonical_hostname, GFS_FILE_MODE_UPDATE_METADATA */
 retry:
	if (if_hostname != NULL) {
		e = gfarm_host_get_canonical_name(if_hostname,
		    &vc->canonical_hostname);
		if (e == GFARM_ERR_UNKNOWN_HOST ||
		    e == GFARM_ERR_INVALID_ARGUMENT /* XXX - gfarm_agent */) {
			/* FT - invalid hostname, delete section copy info */
			(void)gfarm_file_section_copy_info_remove(
				gf->pi.pathname, vc->section, if_hostname);
			e = GFARM_ERR_INCONSISTENT_RECOVERABLE;

			if ((flags & GFARM_FILE_NOT_RETRY) == 0
			    && (gf->open_flags & GFARM_FILE_NOT_RETRY) == 0) {
				if_hostname = NULL;
				goto retry;
			}
			goto finish;
		} else if (e != GFARM_ERR_NO_ERROR)
			goto finish;
		if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) ||
		    (((gf->open_flags & GFARM_FILE_CREATE) ||
		     (gf->mode & GFS_FILE_MODE_WRITE)) &&
		     !gfarm_file_section_info_does_exist(
			gf->pi.pathname, vc->section))) {

			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
			flags |= GFARM_FILE_CREATE;
		} else if ((gf->open_flags & GFARM_FILE_TRUNC) == 0 &&
			   !gfarm_file_section_copy_info_does_exist(
				   gf->pi.pathname, vc->section,
				   vc->canonical_hostname)) {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			goto free_host;
		} else if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
	} else if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) ||
		   (((gf->open_flags & GFARM_FILE_CREATE) ||
		     (gf->mode & GFS_FILE_MODE_WRITE)) &&
		     !gfarm_file_section_info_does_exist(
			gf->pi.pathname, vc->section)) ||
		   (gf->open_flags & GFARM_FILE_TRUNC)) {
		/*
		 * If GFARM_FILE_TRUNC,
		 * we don't have to schedule a host which has a replica.
		 */
		if (gfarm_schedule_write_local_priority() &&
		    gfarm_is_active_fsnode_to_write(0) &&
		    gfarm_host_get_canonical_self_name(&if_hostname) == NULL) {
			vc->canonical_hostname = strdup(if_hostname);
			if (vc->canonical_hostname == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				goto finish;
			}
		} else {
			/*
			 * local host is not a file system node, or
			 * 'gfsd' on a local host is not running.
			 */
			e = gfarm_schedule_search_idle_by_all(1, &if_hostname);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			vc->canonical_hostname = if_hostname;
		}
		gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
		flags |= GFARM_FILE_CREATE;
	} else {
		e = gfarm_file_section_host_schedule_with_priority_to_local(
		    gf->pi.pathname, vc->section, &if_hostname);
		if (e != GFARM_ERR_NO_ERROR)
			goto finish;
		vc->canonical_hostname = if_hostname; /* must be already
							 canonical */
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
	}

	is_local_host = gfarm_canonical_hostname_is_local(gf->gfm_server,
	    vc->canonical_hostname);

	gf->ops = &gfs_pio_view_section_ops;
	gf->view_context = vc;
	gf->view_flags = flags;
	gf->p = gf->length = 0;
	gf->io_offset = gf->offset = 0;

	gf->mode |= GFS_FILE_MODE_CALC_DIGEST;
	vc->md_ctx = gfarm_msgdigest_alloc(GFS_DEFAULT_DIGEST_MODE);
	if (vc->md_ctx == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish;
	}

	if (!is_local_host && gfarm_is_active_file_system_node &&
	    (gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
	    ((((gf->open_flags & GFARM_FILE_REPLICATE) != 0
	       || gfarm_ctxp->on_demand_replication) &&
	      (flags & GFARM_FILE_NOT_REPLICATE) == 0) ||
	     (flags & GFARM_FILE_REPLICATE) != 0)) {
		e = replicate_section_to_local(gf, vc->section,
		    vc->canonical_hostname, if_hostname);
		/* FT - inconsistent metadata has been fixed.  try again. */
		if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE
		    && (flags & GFARM_FILE_NOT_RETRY) == 0
		    && (gf->open_flags & GFARM_FILE_NOT_RETRY) == 0) {
			if_hostname = NULL;
			free(vc->canonical_hostname);
			goto retry;
		}
		if (e != GFARM_ERR_NO_ERROR)
			goto free_host;
		free(vc->canonical_hostname);
		e = gfarm_host_get_canonical_self_name(
		    &vc->canonical_hostname);
		if (e != GFARM_ERR_NO_ERROR)
			goto finish;
		vc->canonical_hostname = strdup(vc->canonical_hostname);
		if (vc->canonical_hostname == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto finish;
		}
		is_local_host = 1;
	}

	if (is_local_host)
		e = gfs_pio_open_local_section(gf, flags);
	else
		e = gfs_pio_open_remote_section(gf, if_hostname, flags);

	/* FT - inconsistent metadata has been fixed.  try again. */
	if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE
	    && (flags & GFARM_FILE_NOT_RETRY) == 0
	    && (gf->open_flags & GFARM_FILE_NOT_RETRY) == 0) {
		if_hostname = NULL;
		free(vc->canonical_hostname);
		goto retry;
	}
	if (e != GFARM_ERR_NO_ERROR)
		goto free_host;

	/* update metadata */
	if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) != 0) {
		e = gfs_set_path_info(gf);
		if (e != GFARM_ERR_NO_ERROR)
			goto storage_close;
	}
	if ((gf->mode & GFS_FILE_MODE_WRITE) ||
	    (gf->open_flags & GFARM_FILE_TRUNC)) {
		/*
		 * if write mode or read-but-truncate mode,
		 * delete every other file copies
		 */
		(void)gfs_pio_view_section_set_busy(gf);
		if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) == 0)
			(void)gfs_unlink_every_other_replicas(
				gf->pi.pathname, vc->section,
				vc->canonical_hostname);
	}
	/* create section copy info */
	if (flags & GFARM_FILE_CREATE) {
		struct gfarm_file_section_copy_info fci;

		fci.hostname = vc->canonical_hostname;
		(void)gfarm_file_section_copy_info_set(
			gf->pi.pathname, vc->section, fci.hostname, &fci);
	}
	/* XXX - need to figure out ignorable error or not */

	if (gf->open_flags & GFARM_FILE_APPEND)
		e = gfs_pio_seek_unlocked(gf, 0, SEEK_END, NULL);

storage_close:
	if (e != GFARM_ERR_NO_ERROR)
		(void)(*vc->ops->storage_close)(gf);
free_digest:
	if (e != NULL)
		md_len = gfarm_msgdigest_free(vc->md_ctx, md_value);
free_host:
	if (e != GFARM_ERR_NO_ERROR)
		free(vc->canonical_hostname);

finish:
	if (e != GFARM_ERR_NO_ERROR) {
		free(vc->section);
		free(vc);
		gf->view_context = NULL;
		gfs_pio_set_view_default(gf);
	}
	gf->error = e;

profile_finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfarm_pio_set_view_section_time
		    += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_pio_set_view_index(GFS_File gf, int nfragments, int fragment_index,
		       char *host, int flags)
{
	char section_string[GFARM_INT32STRLEN + 1];

	if (GFS_FILE_IS_PROGRAM(gf)) {
		gf->error = GFARM_ERR_OPERATION_NOT_PERMITTED;
		return (gf->error);
	}

	if (nfragments == GFARM_FILE_DONTCARE) {
		if ((gf->mode & GFS_FILE_MODE_NSEGMENTS_FIXED) == 0 &&
		    !GFARM_S_IS_PROGRAM(gf->pi.status.st_mode)) {
			/* DONTCARE isn't permitted in this case */
			gf->error = GFARM_ERR_INVALID_ARGUMENT;
			return (gf->error);
		}
	} else {
		if ((gf->mode & GFS_FILE_MODE_NSEGMENTS_FIXED) == 0) {
			if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) == 0 &&
			    gf->pi.status.st_nsections > nfragments) {
				/* GFARM_FILE_TRUNC case */
				int i;

				for (i = nfragments;
				     i < gf->pi.status.st_nsections; i++) {
					sprintf(section_string, "%d", i);
					gfs_unlink_section_internal(
					    gf->pi.pathname, section_string);
				}
			}
			gf->pi.status.st_nsections = nfragments;
			gf->mode |= GFS_FILE_MODE_NSEGMENTS_FIXED;
		} else if (nfragments != gf->pi.status.st_nsections) {
			gf->error = GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH;
			return (gf->error);
		}
		if (fragment_index < 0
		    || fragment_index >= nfragments) {
			gf->error = GFARM_ERR_INVALID_ARGUMENT;
			return (gf->error);
		}
	}

	sprintf(section_string, "%d", fragment_index);

	return (gfs_pio_set_view_section(gf, section_string, host, flags));
}

char *
gfarm_redirect_file(int fd, char *file, GFS_File *gfp)
{
	int nfd;
	char *e;
	GFS_File gf;
	struct gfs_file_section_context *vc;

	if (file == NULL)
		return (NULL);

	e = gfs_pio_create(file, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0644,
	    &gf);
	if (e != NULL)
		return (e);

	e = gfs_pio_set_view_local(gf, 0);
	if (e != NULL)
		return (e);

	nfd = gfs_pio_fileno(gf);
	if (nfd == -1)
		return (gfarm_errno_to_error(errno));

	/*
	 * XXX This assumes the file fragment is created in the local spool.
	 */
	if (dup2(nfd, fd) == -1)
		e = gfarm_errno_to_error(errno);

	/* XXX - apparently violating the layer */
	assert(gf->ops == &gfs_pio_view_section_ops);
	vc = gf->view_context;
	vc->fd = fd;

	gfs_pio_unset_calc_digest(gf);

	close(nfd);

	*gfp = gf;
	return (NULL);
}

#endif /* not yet in gfarm v2 */

struct gfs_profile_list section_profile_items[] = {
	{ "set_view_section", "gfs_pio_set_view_section  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_pio_section_static, set_view_section_time)
	},
	{ "open_local_count", "gfs_pio_open_local_count  : %lld", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_section_static, open_local_count) },
	{ "open_remote_count", "gfs_pio_open_remote_count : %lld", "%llu", 'l',
	  offsetof(struct gfarm_gfs_pio_section_static, open_remote_count) },
};

void
gfs_pio_section_display_timers(void)
{
	int n = GFARM_ARRAY_LENGTH(section_profile_items);

	gfs_profile_display_timers(n, section_profile_items, staticp);
}

gfarm_error_t
gfs_pio_section_profile_value(const char *name, char *value, size_t *sizep)
{
	int n = GFARM_ARRAY_LENGTH(section_profile_items);

	return (gfs_profile_value(name, n, section_profile_items,
		    staticp, value, sizep));
}

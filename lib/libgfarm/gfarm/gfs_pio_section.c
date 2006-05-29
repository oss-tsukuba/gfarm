/*
 * pio operations for file fragments or programs
 *
 * $Id$
 */

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

#include "gfs_profile.h"
#include "host.h"
#include "config.h"
#include "gfm_client.h"
#include "gfs_client.h"
#include "gfs_proto.h"
#include "gfs_io.h"
#include "gfs_pio.h"
#include "schedule.h"

static gfarm_error_t
gfs_pio_view_section_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e_save = GFARM_ERR_NO_ERROR;

#if 0 /* not yet in gfarm v2 */
	int md_calculated = 1;
	gfarm_off_t filesize;
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	/* calculate checksum */
	if ((gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0) {
		if (((gf->mode & GFS_FILE_MODE_WRITE) != 0 &&
		     (gf->open_flags & GFARM_FILE_TRUNC) == 0) ||
		    ((gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
		     (gf->error != GFARM_ERRMSG_GFS_PIO_IS_EOF) &&
		     (gf->mode & GFS_FILE_MODE_UPDATE_METADATA) != 0)) {
			/* we have to read rest of the file in this case */
#if 0
			char message[] = "gfarm: writing without truncation"
			    " isn't supported yet\n";
			write(2, message, sizeof(message) - 1);
			abort(); /* XXX - not supported for now */
#endif
			/* re-read whole file to calculate digest value */
			e = (*vc->ops->storage_calculate_digest)(gf,
			    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
			    &md_len, md_value, &filesize);
			if (e != GFARM_ERR_NO_ERROR) {
				md_calculated = 0;
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
			}
		} else if ((gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
		    (gf->error != GFARM_ERRMSG_GFS_PIO_IS_EOF)) {
			/*
			 * sequential and read-only case, but
			 * either error occurred or gf doesn't reach EOF,
			 * we don't confirm checksum in this case.
			 */
			md_calculated = 0;
		} else {
			unsigned int len;

			EVP_DigestFinal(&vc->md_ctx, md_value, &len);
			md_len = len;
			filesize = gf->offset + gf->length;
		}
	} else {
		if ((gf->mode & GFS_FILE_MODE_UPDATE_METADATA) == 0) {
			/*
			 * random-access and read-only case,
			 * we don't confirm checksum for this case,
			 * because of its high overhead.
			 */
			md_calculated = 0;
		} else {
			/*
			 * re-read whole file to calculate digest value
			 * for writing.
			 * note that this effectively breaks file offset.
			 */
			e = (*vc->ops->storage_calculate_digest)(gf,
			    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
			    &md_len, md_value, &filesize);
			if (e != GFARM_ERR_NO_ERROR) {
				md_calculated = 0;
				if (e_save == GFARM_ERR_NO_ERROR)
					e_save = e;
			}
		}
	}

	if (md_calculated) {
		int i;
		char md_value_string[EVP_MAX_MD_SIZE * 2 + 1];
		struct gfarm_file_section_info fi;
		struct gfarm_file_section_copy_info fci;

		for (i = 0; i < md_len; i++)
			sprintf(&md_value_string[i + i], "%02x",
				md_value[i]);

		if (gf->mode & GFS_FILE_MODE_UPDATE_METADATA) {
			fi.filesize = filesize;
			fi.checksum_type = GFS_DEFAULT_DIGEST_NAME;
			fi.checksum = md_value_string;

			e = gfarm_file_section_info_set(
				gf->pi.pathname, vc->section, &fi);
			if (e == GFARM_ERR_ALREADY_EXISTS)
				e = gfarm_file_section_info_replace(
				    gf->pi.pathname, vc->section, &fi);
			if (e == GFARM_ERR_NO_ERROR) {
				fci.hostname = vc->canonical_hostname;
				e = gfarm_file_section_copy_info_set(
				    gf->pi.pathname, vc->section,
				    fci.hostname, &fci);
				if (e == GFARM_ERR_ALREADY_EXISTS)
					e = GFARM_ERR_NO_ERROR;
			}
		} else {
			e = gfarm_file_section_info_get(
			    gf->pi.pathname, vc->section, &fi);
			if (e == GFARM_ERR_NO_ERROR) {
				if (gfs_check_section_busy_by_finfo(&fi)
				    == GFARM_ERR_NO_ERROR) {
					if (filesize != fi.filesize)
						e = "filesize mismatch";
					else if (strcasecmp(fi.checksum_type,
					  GFS_DEFAULT_DIGEST_NAME) != 0 ||
					  strcasecmp(
					    fi.checksum, md_value_string) != 0)
						e = "checksum mismatch";
				}
				gfarm_file_section_info_free(&fi);
			}
		}
	}
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;

#endif /* not yet in gfarm v2 */

	e = (*vc->ops->storage_close)(gf);
	if (e_save == GFARM_ERR_NO_ERROR)
		e_save = e;

	free(vc);
	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);
	return (e_save);
}

static gfarm_error_t
gfs_pio_view_section_pwrite(GFS_File gf,
	const char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = (*vc->ops->storage_pwrite)(gf,
	    buffer, size, offset, lengthp);

	if (e == GFARM_ERR_NO_ERROR && *lengthp > 0 &&
	    (gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0)
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static gfarm_error_t
gfs_pio_view_section_pread(GFS_File gf,
	char *buffer, size_t size, gfarm_off_t offset, size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	gfarm_error_t e = (*vc->ops->storage_pread)(gf,
	    buffer, size, offset, lengthp);

	if (e == GFARM_ERR_NO_ERROR && *lengthp > 0 &&
	    (gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0)
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static gfarm_error_t
gfs_pio_view_section_ftruncate(GFS_File gf, gfarm_off_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_ftruncate)(gf, length));
}

static gfarm_error_t
gfs_pio_view_section_fsync(GFS_File gf, int operation)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_fsync)(gf, operation));
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
gfm_schedule_file(gfarm_int32_t fd,
	int *nhostsp, struct gfarm_host_sched_info **infosp)
{
	gfarm_error_t e;
	int nhosts;
	struct gfarm_host_sched_info *infos;

	if ((e = gfm_client_compound_begin_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_request(gfarm_metadb_server, fd))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_schedule_file_request(gfarm_metadb_server, "")
	    ) != GFARM_ERR_NO_ERROR)
		gflog_warning("schedule_file request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_schedule_file_result(gfarm_metadb_server,
	    &nhosts, &infos)) != GFARM_ERR_NO_ERROR)
		gflog_warning("schedule_file result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(gfarm_metadb_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));
		gfarm_host_sched_info_free(nhosts, infos);

	} else {
		*nhostsp = nhosts;
		*infosp = infos;
	}
	return (e);
}

static gfarm_error_t
schedule_and_open(GFS_File gf, gfarm_int32_t fd)
{
	gfarm_error_t e;
	int nhosts;
	struct gfarm_host_sched_info *infos;
	char *host;
	gfarm_int32_t port;
	int is_local_host;
	struct sockaddr peer_addr;
	struct gfs_connection *gfs_server;

	/*
	 * XXX FIXME: Or, call replicate_section_to_local(), if that's prefered
	 */
	e = gfm_schedule_file(gf->fd, &nhosts, &infos);
/*gflog_info("schedule_file: %s", gfarm_error_string(e));*/
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
/*gflog_info("schedule_file -> %d hosts", nhosts);*/
	e = gfarm_schedule_select_host(nhosts, infos,
	    (gf->mode & GFS_FILE_MODE_WRITE) != 0, &host, &port);
	gfarm_host_sched_info_free(nhosts, infos);
/*gflog_info("select_host: %s",  gfarm_error_string(e));*/
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
/*gflog_info("host -> %s",  host);*/

	if ((e = gfarm_host_address_get(host, port, &peer_addr, NULL))
	    == GFARM_ERR_NO_ERROR &&
	    (e = gfs_client_connection_acquire(host, port, &peer_addr,
	    &gfs_server)) == GFARM_ERR_NO_ERROR) {
{
		if (gfs_client_pid(gfs_server) == 0)
			e = gfarm_client_process_set(gfs_server);
/*gflog_info("gfarm_client_process_set: %s",  gfarm_error_string(e));*/
}
		if (e == GFARM_ERR_NO_ERROR) {
			is_local_host =
			    gfs_client_connection_is_local(gfs_server);
			if (is_local_host)
				e = gfs_pio_open_local_section(gf, gfs_server);
			else
				e = gfs_pio_open_remote_section(gf,gfs_server);
/*gflog_info("gfs_pio_open_section, local=%d: %s", is_local_host, gfarm_error_string(e));*/
		}
		if (e != GFARM_ERR_NO_ERROR)
			gfs_client_connection_free(gfs_server);
	}
	free(host);

	/* XXX FIXME: if failed, try to reschedule another host */
	return (e);
}

double gfs_pio_set_view_section_time;

gfarm_error_t
gfs_pio_internal_set_view_section(GFS_File gf)
{
	struct gfs_file_section_context *vc;
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_set_view_default(gf);
	if (e == GFARM_ERR_NO_ERROR) {
		vc = malloc(sizeof(*vc));
		if (vc != NULL) {
			gf->view_context = vc;
			e = schedule_and_open(gf, gf->fd);
			if (e == GFARM_ERR_NO_ERROR) {
				gf->ops = &gfs_pio_view_section_ops;
				gf->p = gf->length = 0;
				gf->io_offset = gf->offset = 0;

#if 1 /* not yet in gfarm v2  */
				goto finish;
#else /* not yet in gfarm v2  */
				gf->mode |= GFS_FILE_MODE_CALC_DIGEST;
				EVP_DigestInit(&vc->md_ctx,
				    GFS_DEFAULT_DIGEST_MODE);

				if (gf->open_flags & GFARM_FILE_APPEND) {
					e = gfs_pio_seek(gf, 0,SEEK_END, NULL);
					if (e == GFARM_ERR_NO_ERROR)
						goto finish;
					(*vc->ops->storage_close)(gf);
				}
#endif /* not yet in gfarm v2 */
			}
			free(vc);
		}
		gf->view_context = NULL;
		gfs_pio_set_view_default(gf);
	}
finish:
	gf->error = e;

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_pio_set_view_section_time
		    += gfarm_timerval_sub(&t2, &t1));

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

	vc = malloc(sizeof(struct gfs_file_section_context));
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
		if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) ||
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
	} else if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) ||
		    (((gf->open_flags & GFARM_FILE_CREATE) ||
		     (gf->mode & GFS_FILE_MODE_WRITE)) &&
		     !gfarm_file_section_info_does_exist(
			gf->pi.pathname, vc->section))) {
		if (gfarm_is_active_file_system_node &&
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

	is_local_host = gfarm_canonical_hostname_is_local(
	    vc->canonical_hostname);

	gf->ops = &gfs_pio_view_section_ops;
	gf->view_context = vc;
	gf->view_flags = flags;
	gf->p = gf->length = 0;
	gf->io_offset = gf->offset = 0;

	gf->mode |= GFS_FILE_MODE_CALC_DIGEST;
	EVP_DigestInit(&vc->md_ctx, GFS_DEFAULT_DIGEST_MODE);

	if (!is_local_host && gfarm_is_active_file_system_node &&
	    (gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
	    ((((gf->open_flags & GFARM_FILE_REPLICATE) != 0
	       || gf_on_demand_replication ) &&
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
		(void)gfs_set_section_busy(gf->pi.pathname, vc->section);
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
		e = gfs_pio_seek(gf, 0, SEEK_END, NULL);

storage_close:
	if (e != GFARM_ERR_NO_ERROR)
		(void)(*vc->ops->storage_close)(gf);
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
	gfs_profile(gfs_pio_set_view_section_time
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
			if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) == 0 &&
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

#endif /* not yet in gfarm v2 */

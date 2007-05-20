/*
 * pio operations for file fragments or programs
 *
 * $Id$
 */

#include <assert.h>
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

#include "host.h"
#include "config.h"
#include "schedule.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_misc.h"
#include "gfs_pio.h"

/*
 * switch to another replica, when a connection-related error happened.
 * This only works with read-only mode, because:
 * - If it's write mode, we don't have a way to access already written data
 *   (at least when the connection error is not resolved yet).
 *   XXX It is possible to handle disk full error, though.
 * - If it's write mode or GFARM_FILE_TRUNC is specified,
 *   other replicas has been already removed by gfs_pio_set_view_section().
 *   See the call of gfs_unlink_every_other_replicas() there.
 */
static char *
gfs_pio_view_section_try_to_switch_replica(GFS_File gf)
{
	char *e;
	struct gfs_file_section_context nvc, *ovc = gf->view_context;

	if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) != 0 ||
	    (gf->mode & GFS_FILE_MODE_WRITE) != 0 ||
	    (gf->open_flags & GFARM_FILE_TRUNC) != 0)
		return ("cannot switch to another replica");
	if ((gf->view_flags & GFARM_FILE_NOT_RETRY) != 0 ||
	    (gf->open_flags & GFARM_FILE_NOT_RETRY) != 0)
		return ("retry is prohibited");

	nvc = *ovc;
	gf->view_context = &nvc;

	for (;;) {
		e = gfarm_file_section_host_schedule_with_priority_to_local(
		    gf->pi.pathname, nvc.section, &nvc.canonical_hostname);
		if (e != NULL)
			break;

		if (gfarm_canonical_hostname_is_local(nvc.canonical_hostname))
			e = gfs_pio_open_local_section(gf, gf->view_flags);
		else
			e = gfs_pio_open_remote_section(gf,
			    nvc.canonical_hostname, gf->view_flags);

		if (e != NULL) {
			free(nvc.canonical_hostname);

			if (gfs_client_is_connection_error(e))
				continue;
			/*
			 * FT - inconsistent metadata has been fixed.
			 * try again.
			 */
			if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE)
				continue;
		}
		break;
	}
	if (e == NULL) {
		e = (*nvc.ops->storage_seek)(gf, gf->io_offset,SEEK_SET, NULL);
		if (e == NULL) {
			gf->view_context = ovc;
			(*ovc->ops->storage_close)(gf);
			free(ovc->canonical_hostname);
			*ovc = nvc;
			return (NULL);
		}
		(*nvc.ops->storage_close)(gf);
		free(nvc.canonical_hostname);
	}
	gf->view_context = ovc;
	return (e);
}

static char *
gfs_pio_view_section_set_status(GFS_File gf,
	char *(*modify)(char *, char *, file_offset_t))
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct stat st;
	char *e;

	e = (*vc->ops->storage_fstat)(gf, &st);
	if (e == NULL)
		e = (*modify)(gf->pi.pathname, vc->section, st.st_size);
	return (e);

}

static char *
gfs_pio_view_section_set_busy(GFS_File gf)
{
	return (gfs_pio_view_section_set_status(gf,
	    gfs_file_section_set_busy));
}

static char *
gfs_pio_view_section_set_checksum_unknown(GFS_File gf)
{
	return (gfs_pio_view_section_set_status(gf,
	    gfs_file_section_set_checksum_unknown));
}

/*  */

static char *
gfs_pio_view_section_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = NULL, *e_save = NULL;
	int md_calculated = 0;
	file_offset_t filesize;
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	char md_value_string[EVP_MAX_MD_SIZE * 2 + 1];
	struct gfarm_file_section_info fi, fi1;
	unsigned int len;
	int i;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	filesize = 0;
	md_len = 0;
#endif
	/* calculate checksum */
	/*
	 * EVP_DigestFinal should be called always to clean up
	 * allocated memory by EVP_DigestInit.
	 */
	EVP_DigestFinal(&vc->md_ctx, md_value, &len);

	if (gfs_pio_check_calc_digest(gf)) {
		if (((gf->mode & GFS_FILE_MODE_WRITE) != 0 &&
		     (gf->open_flags & GFARM_FILE_TRUNC) == 0) ||
		    ((gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
		     (gf->error != GFS_FILE_ERROR_EOF) &&
		     (gf->mode & GFS_FILE_MODE_UPDATE_METADATA) != 0)) {
			/* we have to read rest of the file in this case */
#if 0
			char message[] = "gfarm: writing without truncation"
			    " isn't supported yet\n";
			write(2, message, sizeof(message) - 1);
			abort(); /* XXX - not supported for now */
#endif
#if 0
			/* re-read whole file to calculate digest value */
			e = (*vc->ops->storage_calculate_digest)(gf,
			    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
			    &md_len, md_value, &filesize);
			if (gfs_client_is_connection_error(e) &&
			    gfs_pio_view_section_try_to_switch_replica(gf) ==
			    NULL) {
				e = (*vc->ops->storage_calculate_digest)(gf,
				    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
				    &md_len, md_value, &filesize);
			}
			if (e != NULL) {
				md_calculated = 0;
				if (e_save == NULL)
					e_save = e;
			}
			md_calculated = 1;
#endif
		} else if ((gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
		    (gf->error != GFS_FILE_ERROR_EOF)) {
			/*
			 * sequential and read-only case, but
			 * either error occurred or gf doesn't reach EOF,
			 * we don't confirm checksum in this case.
			 */
			md_calculated = 0;
		} else {
			md_len = len;
			filesize = gf->offset + gf->length;
			md_calculated = 1;
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
#if 0
			/*
			 * re-read whole file to calculate digest value
			 * for writing.
			 * note that this effectively breaks file offset.
			 */
			e = (*vc->ops->storage_calculate_digest)(gf,
			    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
			    &md_len, md_value, &filesize);
			if (gfs_client_is_connection_error(e) &&
			    gfs_pio_view_section_try_to_switch_replica(gf) ==
			    NULL) {
				e = (*vc->ops->storage_calculate_digest)(gf,
				    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
				    &md_len, md_value, &filesize);
			}
			if (e != NULL) {
				md_calculated = 0;
				if (e_save == NULL)
					e_save = e;
			}
			md_calculated = 1;
#endif
		}
	}

	if (md_calculated == 1) {
		for (i = 0; i < md_len; i++)
			sprintf(&md_value_string[i + i], "%02x", md_value[i]);
	}

	if (gf->mode & GFS_FILE_MODE_UPDATE_METADATA) {
		if (md_calculated == 1) {
			fi1.filesize = filesize;
			fi1.checksum_type = GFS_DEFAULT_DIGEST_NAME;
			fi1.checksum = md_value_string;

			e = gfarm_file_section_info_replace(
				gf->pi.pathname, vc->section, &fi1);
		}
		else
			e = gfs_pio_view_section_set_checksum_unknown(gf);
	}
	else if (md_calculated == 1 &&
		 (e = gfarm_file_section_info_get(
			  gf->pi.pathname, vc->section, &fi)) == NULL) {
		if (gfs_file_section_info_check_busy(&fi))
			/* skip check*/;
		else if (gfs_file_section_info_check_checksum_unknown(&fi)) {
			fi1.filesize = filesize;
			fi1.checksum_type = GFS_DEFAULT_DIGEST_NAME;
			fi1.checksum = md_value_string;

			e = gfarm_file_section_info_replace(
				gf->pi.pathname, vc->section, &fi1);
		} else {
			if (filesize != fi.filesize)
				e = "filesize mismatch";
			else if (strcasecmp(fi.checksum_type,
					    GFS_DEFAULT_DIGEST_NAME) != 0 ||
				 strcasecmp(fi.checksum, md_value_string) != 0)
				e = "checksum mismatch";
		}
		gfarm_file_section_info_free(&fi);
	}
	if (e_save == NULL)
		e_save = e;

	e = (*vc->ops->storage_close)(gf);
	if (e_save == NULL)
		e_save = e;

	free(vc->canonical_hostname);
	free(vc->section);
	free(vc);
	gf->view_context = NULL;
	gf->mode &= ~GFS_FILE_MODE_UPDATE_METADATA;
	gfs_pio_set_view_default(gf);
	return (e_save);
}

static char *
gfs_pio_view_section_write(GFS_File gf, const char *buffer, size_t size,
			   size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = (*vc->ops->storage_write)(gf, buffer, size, lengthp);

	if (e == NULL && *lengthp > 0 && gfs_pio_check_calc_digest(gf))
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static char *
gfs_pio_view_section_read(GFS_File gf, char *buffer, size_t size,
			  size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = (*vc->ops->storage_read)(gf, buffer, size, lengthp);

	if (gfs_client_is_connection_error(e) &&
	    gfs_pio_view_section_try_to_switch_replica(gf) == NULL) {
		e = (*vc->ops->storage_read)(gf, buffer,size, lengthp);
	}
	if (e == NULL && *lengthp > 0 && gfs_pio_check_calc_digest(gf))
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static char *
gfs_pio_view_section_seek(GFS_File gf, file_offset_t offset, int whence,
			  file_offset_t *resultp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = (*vc->ops->storage_seek)(gf, offset, whence, resultp);

	if (gfs_client_is_connection_error(e) &&
	    gfs_pio_view_section_try_to_switch_replica(gf) == NULL) {
		e = (*vc->ops->storage_seek)(gf, offset, whence, resultp);
	}
	if (e == NULL)
		gfs_pio_unset_calc_digest(gf);
	return (e);
}

static char *
gfs_pio_view_section_ftruncate(GFS_File gf, file_offset_t length)
{
	struct gfs_file_section_context *vc = gf->view_context;

	return ((*vc->ops->storage_ftruncate)(gf, length));
}

static char *
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

static char *
gfs_pio_view_section_stat(GFS_File gf, struct gfs_stat *status)
{
	struct gfs_file_section_context *vc = gf->view_context;
	struct stat st;
	unsigned long ino;
	char *e;

	e = gfs_get_ino(gf->pi.pathname, &ino);
	if (e != NULL)
		return (e);

	*status = gf->pi.status;
	status->st_ino = ino;
	status->st_user = strdup(status->st_user);
	if (status->st_user == NULL)
		return (GFARM_ERR_NO_MEMORY);
	status->st_group = strdup(status->st_group);
	if (status->st_group == NULL) {
		free(status->st_user);
		return (GFARM_ERR_NO_MEMORY);
	}

	e = (*vc->ops->storage_fstat)(gf, &st);
	if (gfs_client_is_connection_error(e) &&
	    gfs_pio_view_section_try_to_switch_replica(gf) == NULL) {
		e = (*vc->ops->storage_fstat)(gf, &st);
	}
	if (e != NULL) {
		free(status->st_user);
		free(status->st_group);
		return (e);
	}
	status->st_size = st.st_size;
	status->st_nsections = 1;

	return (NULL);
}

static char *
gfs_pio_view_section_chmod(GFS_File gf, gfarm_mode_t mode)
{
	char *e, *changed_section;
	struct gfs_file_section_context *vc = gf->view_context;

	e = gfs_chmod_internal(&gf->pi, mode, &changed_section);
	if (changed_section != NULL) {
		if (e == NULL) {
			free(vc->section);
			vc->section = changed_section;
		}
		else
			free(changed_section);
	}
	return (e);
}

struct gfs_pio_ops gfs_pio_view_section_ops = {
	gfs_pio_view_section_close,
	gfs_pio_view_section_write,
	gfs_pio_view_section_read,
	gfs_pio_view_section_seek,
	gfs_pio_view_section_ftruncate,
	gfs_pio_view_section_fsync,
	gfs_pio_view_section_fd,
	gfs_pio_view_section_stat,
	gfs_pio_view_section_chmod
};

static char *
replicate_section_to_local(GFS_File gf, char *section,
	char *src_canonical_hostname, char *src_if_hostname)
{
	char *e;
	int if_hostname_alloced = 0;
	struct gfarm_file_section_info sinfo;

	if (src_if_hostname == NULL) {
		struct sockaddr peer_addr;

		e = gfarm_host_address_get(src_canonical_hostname,
		    gfarm_spool_server_port, &peer_addr, &src_if_hostname);
		if (e != NULL)
			return (e);
		if_hostname_alloced = 1;
	}

	/* gf->pi.status.st_size does not have the file size... */
	e = gfarm_file_section_info_get(gf->pi.pathname, section, &sinfo);
	if (e != NULL)
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

static double gfs_pio_set_view_section_time;

static char *
gfs_set_path_info(GFS_File gf)
{
	struct gfarm_path_info pi;
	char *e;

	e = gfarm_path_info_set(gf->pi.pathname, &gf->pi);
	if (e == GFARM_ERR_ALREADY_EXISTS &&
	    (gf->open_flags & GFARM_FILE_EXCLUSIVE) != 0) {
		return (e);
	}
	if (e == GFARM_ERR_ALREADY_EXISTS &&
	    (e = gfarm_path_info_get(gf->pi.pathname, &pi)) == NULL) {
		if (GFS_FILE_IS_PROGRAM(gf) !=
		    GFARM_S_IS_PROGRAM(pi.status.st_mode))
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		if (e == NULL && !GFS_FILE_IS_PROGRAM(gf)) {
			if (gf->pi.status.st_nsections !=
			    pi.status.st_nsections) {
				e = GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH;
			} else {
#if 0
				assert(gf->pi.status.st_mode &
				       GFS_FILE_MODE_NSEGMENTS_FIXED);
#endif
			}
		}
		if (e != NULL) {
			gfarm_path_info_free(&pi);
		} else {
			gfarm_path_info_free(&gf->pi);
			gf->pi = pi;
		}
		/*
		 * XXX should check the follows:
		 * - creator of the metainfo has same job id
		 * - mode is consistent among same job
		 * - nfragments is consistent among same job
		 */
	}
	return (e);
}

char *
gfs_pio_set_view_section(GFS_File gf, const char *section,
			 char *if_hostname, int flags)
{
	struct gfs_file_section_context *vc;
	char *e;
	int is_local_host;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_set_view_default(gf);
	if (e != NULL)
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
		} else if (e != NULL)
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
			 * we don't give priority to local host, or
			 * local host is not a file system node, or
			 * 'gfsd' on a local host is not running, or
			 * local host is nearly disk full.
			 */
			char *domain = gfarm_schedule_write_target_domain();

			if (domain == NULL)
				e = GFARM_ERR_NO_SUCH_OBJECT;
			else
				e = gfarm_schedule_search_idle_by_domainname_to_write(
				    domain, 1, &if_hostname);
			if (e != NULL)
				e = gfarm_schedule_search_idle_by_all_to_write(
				    1, &if_hostname);
			if (e != NULL)
				goto finish;
			vc->canonical_hostname = if_hostname;
		}
		gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
		flags |= GFARM_FILE_CREATE;
	} else {
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			e = gfarm_file_section_host_schedule_with_priority_to_local_to_write(
			    gf->pi.pathname, vc->section, &if_hostname);
		else
			e = gfarm_file_section_host_schedule_with_priority_to_local(
			    gf->pi.pathname, vc->section, &if_hostname);
		if (e != NULL)
			goto finish;
		/* if_hostname must be already canonical here */
		vc->canonical_hostname = if_hostname;
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

	gfs_pio_set_calc_digest(gf);
	EVP_DigestInit(&vc->md_ctx, GFS_DEFAULT_DIGEST_MODE);

	if (!is_local_host && gfarm_is_active_fsnode_to_write(0) &&
	    (gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
	    ((((gf->open_flags & GFARM_FILE_REPLICATE) != 0
	       || gf_on_demand_replication ) &&
	      (flags & GFARM_FILE_NOT_REPLICATE) == 0) ||
	     (flags & GFARM_FILE_REPLICATE) != 0) &&
	    (gf->open_flags & GFARM_FILE_TRUNC) == 0) {
		char *canonical_self_name;

		e = replicate_section_to_local(gf, vc->section,
		    vc->canonical_hostname, if_hostname);
		/* FT - inconsistent metadata has been fixed.  try again. */
		if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE
		    && (flags & GFARM_FILE_NOT_RETRY) == 0
		    && (gf->open_flags & GFARM_FILE_NOT_RETRY) == 0) {
			EVP_DigestFinal(&vc->md_ctx, md_value, &md_len);
			if_hostname = NULL;
			free(vc->canonical_hostname);
			goto retry;
		}
		if (e != NULL)
			goto free_digest;
		e = gfarm_host_get_canonical_self_name(&canonical_self_name);
		if (e != NULL)
			goto free_digest;
		canonical_self_name = strdup(canonical_self_name);
		if (canonical_self_name == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto free_digest;
		}
		free(vc->canonical_hostname);
		vc->canonical_hostname = canonical_self_name;
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
		EVP_DigestFinal(&vc->md_ctx, md_value, &md_len);
		if_hostname = NULL;
		free(vc->canonical_hostname);
		goto retry;
	}
	if (e != NULL)
		goto free_digest;

	/* update metadata */
	if ((gf->mode & GFS_FILE_MODE_FILE_WAS_CREATED) != 0) {
		e = gfs_set_path_info(gf);
		if (e != NULL)
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
		e = gfs_pio_seek(gf, 0, SEEK_END, NULL);

storage_close:
	if (e != NULL)
		(void)(*vc->ops->storage_close)(gf);
free_digest:
	if (e != NULL)
		EVP_DigestFinal(&vc->md_ctx, md_value, &md_len);
free_host:
	if (e != NULL)
		free(vc->canonical_hostname);

finish:
	if (e != NULL) {
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

char *
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

void
gfs_pio_section_display_timers(void)
{
	gflog_info("gfs_pio_set_view_section : %g sec",
		gfs_pio_set_view_section_time);
}

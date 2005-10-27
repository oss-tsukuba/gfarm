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

#include "host.h"
#include "config.h"
#include "schedule.h"
#include "gfs_client.h"
#include "gfs_proto.h"
#include "gfs_misc.h"
#include "gfs_lock.h"
#include "gfs_pio.h"

/*  */

#define SECTION_BUSY "SECTION BUSY"

static char *
gfs_set_section_busy(char *pathname, char *section)
{
	struct gfarm_file_section_info fi;
	char *e;

	fi.filesize = 0;
	fi.checksum_type = GFS_DEFAULT_DIGEST_NAME;
	fi.checksum = SECTION_BUSY;

	e = gfarm_file_section_info_set(pathname, section, &fi);
	if (e == GFARM_ERR_ALREADY_EXISTS)
		e = gfarm_file_section_info_replace(pathname, section, &fi);
	return (e);
}

char *
gfs_check_section_busy_by_finfo(struct gfarm_file_section_info *fi)
{
	if (strncmp(fi->checksum, SECTION_BUSY, sizeof(SECTION_BUSY) - 1) == 0)
		return (GFARM_ERR_TEXT_FILE_BUSY);
	return (NULL);
}


char *
gfs_check_section_busy(char *pathname, char *section)
{
	struct gfarm_file_section_info fi;
	char *e;

	e = gfarm_file_section_info_get(pathname, section, &fi);
	if (e != NULL)
		return (e);
	e = gfs_check_section_busy_by_finfo(&fi);
	gfarm_file_section_info_free(&fi);
	return (e);
}

/*  */

static char *
gfs_pio_view_section_close(GFS_File gf)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = NULL, *e_save = NULL;
	int md_calculated = 1;
	file_offset_t filesize;
	size_t md_len;
	unsigned char md_value[EVP_MAX_MD_SIZE];

	/* calculate checksum */
	if ((gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0) {
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
			/* re-read whole file to calculate digest value */
			e = (*vc->ops->storage_calculate_digest)(gf,
			    GFS_DEFAULT_DIGEST_NAME, sizeof(md_value),
			    &md_len, md_value, &filesize);
			if (e != NULL) {
				md_calculated = 0;
				if (e_save == NULL)
					e_save = e;
			}
		} else if ((gf->mode & GFS_FILE_MODE_WRITE) == 0 &&
		    (gf->error != GFS_FILE_ERROR_EOF)) {
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
			if (e != NULL) {
				md_calculated = 0;
				if (e_save == NULL)
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
			if (e == NULL) {
				fci.hostname = vc->canonical_hostname;
				e = gfarm_file_section_copy_info_set(
				    gf->pi.pathname, vc->section,
				    fci.hostname, &fci);
				if (e == GFARM_ERR_ALREADY_EXISTS)
					e = NULL;
			}
		} else {
			e = gfarm_file_section_info_get(
			    gf->pi.pathname, vc->section, &fi);
			if (e == NULL) {
				if (gfs_check_section_busy_by_finfo(&fi)
				    == NULL) {
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

	if (e == NULL && *lengthp > 0 &&
	    (gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0)
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static char *
gfs_pio_view_section_read(GFS_File gf, char *buffer, size_t size,
			  size_t *lengthp)
{
	struct gfs_file_section_context *vc = gf->view_context;
	char *e = (*vc->ops->storage_read)(gf, buffer, size, lengthp);

	if (e == NULL && *lengthp > 0 &&
	    (gf->mode & GFS_FILE_MODE_CALC_DIGEST) != 0)
		EVP_DigestUpdate(&vc->md_ctx, buffer, *lengthp);
	return (e);
}

static char *
gfs_pio_view_section_seek(GFS_File gf, file_offset_t offset, int whence,
			  file_offset_t *resultp)
{
	struct gfs_file_section_context *vc = gf->view_context;

	gf->mode &= ~GFS_FILE_MODE_CALC_DIGEST;
	return ((*vc->ops->storage_seek)(gf, offset, whence, resultp));
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
	struct gfarm_file_section_info sinfo;
	long ino;
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

	status->st_size = 0;
	status->st_nsections = 1;
	e = gfarm_file_section_info_get(gf->pi.pathname, vc->section, &sinfo);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* this section is created but not closed yet. */
		return (NULL);
	}
	else if (e != NULL) {
		free(status->st_user);
		free(status->st_group);
		return (e);
	}
	status->st_size = sinfo.filesize;
	gfarm_file_section_info_free(&sinfo);

	return (NULL);
}

struct gfs_pio_ops gfs_pio_view_section_ops = {
	gfs_pio_view_section_close,
	gfs_pio_view_section_write,
	gfs_pio_view_section_read,
	gfs_pio_view_section_seek,
	gfs_pio_view_section_ftruncate,
	gfs_pio_view_section_fsync,
	gfs_pio_view_section_fd,
	gfs_pio_view_section_stat
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

double gfs_pio_set_view_section_time;

char *
gfs_pio_set_view_section(GFS_File gf, const char *section,
			 char *if_hostname, int flags)
{
	struct gfs_file_section_context *vc;
	char *e;
	int is_local_host;
	gfarm_timerval_t t1, t2;

	gfs_profile(gfarm_gettimerval(&t1));

	e = gfs_pio_set_view_default(gf);
	if (e != NULL)
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
		} else if (e != NULL)
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
			if (e != NULL)
				goto finish;
			vc->canonical_hostname = if_hostname;
		}
		gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
		flags |= GFARM_FILE_CREATE;
	} else {
		e = gfarm_file_section_host_schedule_with_priority_to_local(
		    gf->pi.pathname, vc->section, &if_hostname);
		if (e != NULL)
			goto finish;
		vc->canonical_hostname = if_hostname; /* must be already
							 canonical */
		if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
	}

	is_local_host = gfarm_canonical_hostname_is_local(
	    vc->canonical_hostname);

	if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) != 0) {
		struct gfarm_path_info pi;

		e = gfarm_path_info_set(gf->pi.pathname, &gf->pi);
		if (e == GFARM_ERR_ALREADY_EXISTS &&
		    (gf->open_flags & GFARM_FILE_EXCLUSIVE) != 0) {
			e = GFARM_ERR_ALREADY_EXISTS;
			goto free_host;
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
		if (e != NULL)
			goto free_host;
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
		if (e != NULL)
			goto free_host;
		free(vc->canonical_hostname);
		e = gfarm_host_get_canonical_self_name(
		    &vc->canonical_hostname);
		if (e != NULL)
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

	if (e == NULL && (gf->open_flags & GFARM_FILE_APPEND))
		e = gfs_pio_seek(gf, 0, SEEK_END, NULL);

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

/*
 * pio operations for file fragments or programs
 *
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include "gfs_pio.h"
#include "host.h"
#include "config.h"
#include "schedule.h"
#include "gfs_client.h"
#include "gfs_proto.h"
#include "timer.h"
#include "gfs_lock.h"

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
			if (filesize != fi.filesize)
				e = "filesize mismatch";
			else if (strcasecmp(fi.checksum_type,
			    GFS_DEFAULT_DIGEST_NAME) != 0 ||
			    strcasecmp(fi.checksum, md_value_string) != 0)
				e = "checksum mismatch";
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
	gfs_pio_view_section_fd,
	gfs_pio_view_section_stat
};


static char *
replicate_section_to_local_internal(
	char *pathname, char *section, gfarm_mode_t st_mode,
	char *local_path, char *path_section, char *peer_hostname)
{
	struct sockaddr peer_addr;
	struct gfs_connection *peer_conn;
	struct gfarm_file_section_copy_info ci;
	int fd, peer_fd, saved_errno;
	char *e, *my_hostname;

	e = gfarm_host_address_get(peer_hostname, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		return (e);

	e = gfs_client_connection(peer_hostname, &peer_addr, &peer_conn);
	if (e != NULL)
		return (e);

	e = gfs_client_open(peer_conn, path_section, GFARM_FILE_RDONLY, 0,
	    &peer_fd);
	/* FT - source file should be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		/* Delete the section copy info */
		if (gfarm_file_section_copy_info_remove(pathname,
			section, peer_hostname) == NULL)
			e = GFARM_ERR_INCONSISTENT_RECOVERABLE;
	if (e != NULL)
		return (e);

	fd = open(local_path, O_WRONLY|O_CREAT|O_TRUNC, st_mode);
	saved_errno = errno;
	/* FT - the parent directory may be missing */
	if (fd == -1
	    && gfs_proto_error_string(saved_errno)
		== GFARM_ERR_NO_SUCH_OBJECT) {
		if (gfs_pio_local_mkdir_parent_canonical_path(
			    pathname) == NULL) {
			fd = open(local_path, O_WRONLY|O_CREAT|O_TRUNC,
				  st_mode);
			saved_errno = errno;
		}
	}
	if (fd < 0) {
		e = gfs_proto_error_string(saved_errno);
		goto finish_peer_close;
	}

	/* XXX FIXME: this should honor sync_rate */
	e = gfs_client_copyin(peer_conn, peer_fd, fd, 0);
	/* XXX - copyin() should return the digest value */
	close(fd);
#if 0   /* section info is already set. no need to call this here. */
	if (e == NULL)
		e = gfs_pio_set_fragment_info_local(local_path,
		    pathname, section);    
#endif
	/* instead, just set a section copy info */
	if (e == NULL)
		e = gfarm_host_get_canonical_self_name(&my_hostname);
	if (e == NULL)
		e = gfarm_file_section_copy_info_set(
			pathname, section, my_hostname, &ci);
finish_peer_close:
	gfs_client_close(peer_conn, peer_fd);
	return (e);
}

static char *
replicate_section_to_local(GFS_File gf, char *section, char *peer_hostname)
{
	char *e;
	char *path_section, *local_path, *my_hostname;
	int metadata_exist, localfile_exist, replication_needed = 0;
	struct stat sb;

	e = gfarm_host_get_canonical_self_name(&my_hostname);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(gf->pi.pathname, section, &path_section);
	if (e != NULL) 
		return (e);

	e = gfarm_path_localize(path_section, &local_path);
	if (e != NULL)
		goto finish_free_path_section;

	/* critical section starts */
	gfs_lock_local_path_section(local_path);

	/* FT - check existence of the local file and its metadata */
	metadata_exist =
		gfarm_file_section_copy_info_does_exist(
			gf->pi.pathname, section, my_hostname);
	localfile_exist = !stat(local_path, &sb);

	if (metadata_exist && localfile_exist
		&& gf->pi.status.st_size == sb.st_size) {
		/* already exist */
		/* XXX - need integrity check by checksum */
	}
	else {
		replication_needed = 1;
		if (localfile_exist) {
			/* FT - unknown local file.  delete it */
			unlink(local_path);
		}
		if (metadata_exist) {
			/* FT - local file is missing.  delete the metadata */
			(void)gfarm_file_section_copy_info_remove(
				gf->pi.pathname, section, my_hostname);
		}
	}

	if (replication_needed)
		e = replicate_section_to_local_internal(
			gf->pi.pathname, section, gf->pi.status.st_mode,
			local_path, path_section, peer_hostname);

	gfs_unlock_local_path_section(local_path);
	/* critical section ends */

	free(local_path);
finish_free_path_section:
	free(path_section);
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
		if (e == GFARM_ERR_UNKNOWN_HOST) {
			/* FT - invalid hostname, delete section copy info */
			if (gfarm_file_section_copy_info_remove(
				    gf->pi.pathname, vc->section, if_hostname)
			    == NULL)
				e = GFARM_ERR_INCONSISTENT_RECOVERABLE;

			if (e == GFARM_ERR_INCONSISTENT_RECOVERABLE
			    && (flags & GFARM_FILE_NOT_RETRY) == 0
			    && (gf->open_flags & GFARM_FILE_NOT_RETRY) == 0) {
				if_hostname = NULL;
				goto retry;
			}
			goto finish;
		} else if (e != NULL)
			goto finish;
		if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) ||
		    (gf->open_flags & GFARM_FILE_TRUNC) ||
		    ((gf->open_flags & GFARM_FILE_CREATE) &&
		     !gfarm_file_section_info_does_exist(
			gf->pi.pathname, vc->section))) {
			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
		} else if (!gfarm_file_section_copy_info_does_exist(
		    gf->pi.pathname, vc->section, vc->canonical_hostname)) {
			e = GFARM_ERR_NO_SUCH_OBJECT;
			goto free_host;
		} else if ((gf->mode & GFS_FILE_MODE_WRITE) != 0)
			gf->mode |= GFS_FILE_MODE_UPDATE_METADATA;
	} else if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) ||
		   (gf->open_flags & GFARM_FILE_TRUNC) ||
		   ((gf->open_flags & GFARM_FILE_CREATE) &&
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

	if (gf->open_flags & GFARM_FILE_TRUNC) {
		/* with truncation flag, delete all file copies */
		/*
		 * XXX - in this case, it is necessary to add O_CREAT
		 * when opening a physical file since it is already unlinked.
		 * This is taken care in gfs_open_flags_localize().
		 */
		(void)gfs_unlink_section(gf->pi.pathname, vc->section);
	} else if (gf->mode & GFS_FILE_MODE_WRITE) {
		/* otherwise, if write mode, delete every other file copies */
		(void)gfs_unlink_every_other_replicas(
			gf->pi.pathname, vc->section,
			vc->canonical_hostname);
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
		    vc->canonical_hostname);
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
					gfs_unlink_section(gf->pi.pathname,
					    section_string);
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

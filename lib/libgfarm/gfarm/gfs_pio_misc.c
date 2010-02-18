/*
 * $Id$
 */

#include <sys/time.h> /* for gfs_utime() */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <gfarm/gfarm.h>

#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_misc.h"

char *
gfs_access(const char *gfarm_url, int mode)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"creation of path from URL (%s) failed: %s",
			gfarm_url,
			gfarm_error_string(e));
		return (e);
	}
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_path_info_get(%s) failed: %s",
			gfarm_file,
			gfarm_error_string(e));
		goto free_gfarm_file;
	}

	e = gfarm_path_info_access(&pi, mode);
	gfarm_path_info_free(&pi);

free_gfarm_file:
	free(gfarm_file);
	return (e);
}


char *
gfs_utimes(const char *gfarm_url, const struct gfarm_timespec *tsp)
{
	char *e, *gfarm_file, *user;
	struct gfarm_path_info pi;
	struct timeval now;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"creation of path from URL (%s) failed: %s",
			gfarm_url,
			gfarm_error_string(e));
		return (e);
	}
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_path_info_get(%s) failed: %s",
			gfarm_file,
			gfarm_error_string(e));
		return (e);
	}
	user = gfarm_get_global_username();
	if (user == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfs_utimes(): programming error, "
			"gfarm library isn't properly initialized");
		return ("gfs_utimes(): programming error, "
			"gfarm library isn't properly initialized");
	}
	if (strcmp(pi.status.st_user, user) != 0)
		goto finish_free_path_info;

	gettimeofday(&now, NULL);
	if (tsp == NULL) {
		pi.status.st_atimespec.tv_sec =
		pi.status.st_mtimespec.tv_sec = now.tv_sec;
		pi.status.st_atimespec.tv_nsec =
		pi.status.st_mtimespec.tv_nsec = now.tv_usec * 1000;
	} else {
		pi.status.st_atimespec = tsp[0];
		pi.status.st_mtimespec = tsp[1];
	}
	pi.status.st_ctimespec.tv_sec = now.tv_sec;
	pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
	e = gfarm_path_info_replace(pi.pathname, &pi);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"replacement of path info (%s) failed: %s",
			pi.pathname,
			gfarm_error_string(e));
	}
 finish_free_path_info:
	gfarm_path_info_free(&pi);
	return (e);
}

/*
 *
 */

static char *
digest_calculate(char *filename,
		 char **digest_type, char *digest_string, size_t *md_len_p,
		 file_offset_t *filesizep)
{
	int fd, i, rv;
	EVP_MD_CTX md_ctx;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	char buffer[GFS_LOCAL_FILE_BUFSIZE];

	if ((fd = open(filename, O_RDONLY)) == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_UNFIXED,
			"open() on file(%s) failed: %s",
			filename,
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	EVP_DigestInit(&md_ctx, GFS_DEFAULT_DIGEST_MODE);
	rv = gfs_digest_calculate_local(fd, buffer, sizeof buffer,
		GFS_DEFAULT_DIGEST_MODE,
		&md_ctx, md_len_p, md_value, filesizep);
	close(fd);
	if (rv != 0) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"calculation of local digest failed: %s",
			gfarm_error_string(gfarm_errno_to_error(rv)));
		return (gfarm_errno_to_error(rv));
	}

	for (i = 0; i < *md_len_p; i++)
		sprintf(&digest_string[i + i], "%02x",
			md_value[i]);

	*digest_type = GFS_DEFAULT_DIGEST_NAME;
	return (NULL);
}

/*
 * Register a gfarm fragment to a Meta DB.  This function is intended
 * to be used with legacy applications to register a new file.
 */

char *
gfs_pio_set_fragment_info_local(char *filename,
	char *gfarm_file, char *section)
{
	char *digest_type;
	char digest_value_string[EVP_MAX_MD_SIZE * 2 + 1];
	size_t digest_len;
	file_offset_t filesize;
	char *e = NULL;
	struct gfarm_file_section_info fi;
	struct gfarm_file_section_copy_info fci;

#ifdef __GNUC__ /* workaround gcc warning: 'digest_type' may be used uninitialized */
	digest_type = NULL;
#endif
	/* Calculate checksum. */
	e = digest_calculate(filename, &digest_type, digest_value_string,
			     &digest_len, &filesize);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"calculation of digest for file(%s) failed: %s",
			filename,
			gfarm_error_string(e));
		return (e);
	}

	/* Update the filesystem metadata. */
	e = gfarm_file_section_info_get(gfarm_file, section, &fi);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		fi.filesize = filesize;
		fi.checksum_type = digest_type;
		fi.checksum = digest_value_string;

		e = gfarm_file_section_info_set(gfarm_file, section, &fi);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfarm_file_section_info_set(%s) failed: %s",
				gfarm_file,
				gfarm_error_string(e));
		}
	}
	else if (e == NULL) {
		if (gfs_file_section_info_check_checksum_unknown(&fi)
		    || gfs_file_section_info_check_busy(&fi)) {
			struct gfarm_file_section_info fi1;

			fi1.filesize = filesize;
			fi1.checksum_type = GFS_DEFAULT_DIGEST_NAME;
			fi1.checksum = digest_value_string;

			e = gfarm_file_section_info_replace(
				gfarm_file, section, &fi1);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"replacement of file section info "
					"(%s) failed: %s",
					gfarm_file,
					gfarm_error_string(e));
			}
		}
		else {
			if (fi.filesize != filesize)
				e = "file size mismatch";
			if (strcasecmp(fi.checksum_type, digest_type) != 0)
				e = "checksum type mismatch";
			if (strcasecmp(fi.checksum, digest_value_string) != 0)
				e = "check sum mismatch";
			if (e != NULL)
				gflog_debug(GFARM_MSG_UNFIXED, "%s", e);
		}
		gfarm_file_section_info_free(&fi);
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
			"Cannot get file section info (%s): %s",
			gfarm_file,
			gfarm_error_string(e));
	}
	if (e != NULL)
		return (e);

	e = gfarm_host_get_canonical_self_name(&fci.hostname);
	if (e == NULL) {
		e = gfarm_file_section_copy_info_set(
			gfarm_file, section, fci.hostname, &fci);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfarm_host_get_canonical_self_name(%s) "
				"failed: %s",
				gfarm_file,
				gfarm_error_string(e));
		}
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_host_get_canonical_self_name() failed: %s",
			gfarm_file,
			gfarm_error_string(e));
	}
	return (e);
}

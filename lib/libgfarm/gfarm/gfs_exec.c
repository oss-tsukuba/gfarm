/*
 * $Id$
 */

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <gfarm/gfarm.h>
#include <openssl/evp.h>/* EVP_MD_CTX */
#include "gfs_pio.h"	/* gfs_profile */
#include "gfs_lock.h"

char *
gfs_execve(const char *filename, char *const argv [], char *const envp[])
{
	char *hostname, *e;
	char *localpath, *url, *arch, *gfarm_file;
	const char *path;
	struct gfs_stat gstat;
	struct stat st;		
	int metadata_exist, localfile_exist, replication_needed = 0;

	e = gfs_realpath(filename, &url);
	if (e != NULL) {
		/* not found in Gfarm file system */
		if (gfarm_is_url(filename))
			/* to avoid an infinite loop for syscall hooks. */
			return (e);
		path = filename;
		goto clean_up_and_exec;
	}
	/* check the permission */
	e = gfs_access(url, X_OK);
	if (e != NULL) {
		free(url);
		return (e);
	}

	/* determine the architecture */
	e = gfarm_host_get_canonical_self_name(&hostname);
	if (e != NULL) {
		free(url);
		return (e);
	}	
	arch = gfarm_host_info_get_architecture_by_host(hostname);
	if (arch == NULL) {
		free(url);
		return ("not file system node");
	}

	/* check the metadata */
	e = gfs_stat_section(url, arch, &gstat);
	if (e != NULL) {
		free(arch);
		free(url);
		return (e);
	}
	if (GFARM_S_ISDIR(gstat.st_mode)) {
		gfs_stat_free(&gstat);
		free(arch);
		free(url);
		return (GFARM_ERR_IS_A_DIRECTORY);
	}
	else if (!GFARM_S_ISREG(gstat.st_mode)) {
		gfs_stat_free(&gstat);
		free(arch);
		free(url);
		return ("unknown format");
	}
	gfs_stat_free(&gstat);

	/* determine the local pathname */
	e = gfarm_url_make_path(url, &gfarm_file);
	if (e != NULL) {
		free(arch);
		free(url);
		return (e);
	}
	e = gfarm_path_localize_file_section(gfarm_file, arch, &localpath);
	if (e != NULL) {
		free(gfarm_file);
		free(arch);
		free(url);
		return (e);
	}

	/* critical section starts */
	gfs_lock_local_path_section(localpath);

	/* replicate the program if needed */
	metadata_exist =
		gfarm_file_section_copy_info_does_exist(
			gfarm_file, arch, hostname);
	localfile_exist = !stat(localpath, &st);

	/* FT - check existence of the local file and its metadata */
	if (metadata_exist && localfile_exist) {
		/* already exist */
		/* XXX - need integrity check */
	}
	else if (localfile_exist) {
		/* FT - unknown local file.  delete it */
		unlink(localpath);
		replication_needed = 1;
	}
	else if (metadata_exist) {
		/* FT - local file is missing.  delete the metadata */
		e = gfarm_file_section_copy_info_remove(
			gfarm_file, arch, hostname);
		if (e == NULL)
			replication_needed = 1;
	}
	else
		replication_needed = 1;

	if (replication_needed)
		e = gfarm_url_section_replicate_to(url, arch, hostname);

	gfs_unlock_local_path_section(localpath);
	/* critical section ends */

	free(gfarm_file);
	free(arch);
	free(url);
	if (e != NULL) {
		free(localpath);
		return (e);
	}
	path = localpath;
 clean_up_and_exec:
	/* clean up the client environment */
	gfs_profile(gf_profile = 0); /* not to display profile statistics */
	(void)gfarm_terminate();

	execve(path, argv, envp);
	return gfarm_errno_to_error(errno);
}

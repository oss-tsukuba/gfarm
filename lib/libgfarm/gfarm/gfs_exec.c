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

char *
gfs_execve(const char *filename, char *const argv [], char *const envp[])
{
	char *hostname, *e;
	char *localpath, *url, *arch, *gfarm_file;
	struct gfs_stat gstat;

	e = gfs_realpath(filename, &url);
	if (e != NULL) {
		execve(filename, argv, envp);
		return gfarm_errno_to_error(errno);
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

	/* replicate the program if needed */
	if (gfarm_file_section_copy_info_does_exist(
		    gfarm_file, arch, hostname)) {
		struct stat st;		
		/*
		 * FT - already exists in metadata, but need check
		 * whether it really exists or not.
		 */
		if (stat(localpath, &st)) {
			/* not exist.  delete the metadata, and try to
			 * replicate the binary */
			e = gfarm_file_section_copy_info_remove(
				gfarm_file, arch, hostname);
			if (e != NULL) {
				free(localpath);
				free(gfarm_file);
				free(arch);
				free(url);
				return (e);
			}
			e = gfarm_url_section_replicate_to(url, arch, hostname);		}
		/* the replica of the program already exists */
		/* XXX - need integrity check */
	}
	else
		e = gfarm_url_section_replicate_to(url, arch, hostname);

	free(gfarm_file);
	free(arch);
	free(url);
	if (e != NULL) {
		free(localpath);
		return (e);
	}

	/* clean up the client environment */
	gfs_profile(gf_profile = 0); /* not to display profile statistics */
	(void)gfarm_terminate();

	execve(localpath, argv, envp);
	return gfarm_errno_to_error(errno);
}

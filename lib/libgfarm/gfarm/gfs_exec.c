/*
 * $Id$
 */

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>
#include <openssl/evp.h>/* EVP_MD_CTX */
#include "gfs_pio.h"	/* gfs_profile */
#include "gfs_lock.h"

static char *
gfs_execve_replicate_to_local(const char *url, char **local_path)
{
	char *hostname, *e;
	char *arch, *gfarm_file, *localpath;
	struct gfs_stat gstat;
	struct stat st;		
	int metadata_exist, localfile_exist, replication_needed = 0;

	*local_path = NULL;

	/* check the permission */
	e = gfs_access(url, X_OK);
	if (e != NULL)
		return (e);

	/* determine the architecture */
	e = gfarm_host_get_canonical_self_name(&hostname);
	if (e == GFARM_ERR_UNKNOWN_HOST)
		return ("not file system node");
	else if (e != NULL)
		return (e);

	arch = gfarm_host_info_get_architecture_by_host(hostname);
	if (arch == NULL)
		return ("not file system node");

	/* check the metadata */
	e = gfs_stat_section(url, arch, &gstat);
	if (e != NULL) {
		free(arch);
		return (e);
	}
	if (GFARM_S_ISDIR(gstat.st_mode)) {
		gfs_stat_free(&gstat);
		free(arch);
		return (GFARM_ERR_IS_A_DIRECTORY);
	}
	else if (!GFARM_S_ISREG(gstat.st_mode)) {
		gfs_stat_free(&gstat);
		free(arch);
		return ("unknown format");
	}
	gfs_stat_free(&gstat);

	/* determine the local pathname */
	e = gfarm_url_make_path(url, &gfarm_file);
	if (e != NULL) {
		free(arch);
		return (e);
	}
	e = gfarm_path_localize_file_section(gfarm_file, arch, &localpath);
	if (e != NULL) {
		free(gfarm_file);
		free(arch);
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
	if (e != NULL) {
		free(localpath);
		return (e);
	}
	*local_path = localpath;
	return (NULL);
}

char *
gfs_execve(const char *filename, char *const argv[], char *const envp[])
{
	char *localpath, *url, *e;
	const char *path;

	e = gfs_realpath(filename, &url);
	if (e != NULL)
		return (e);

	e = gfs_execve_replicate_to_local(url, &localpath);
	free(url);
	if (e != NULL)
		return (e);
	path = localpath;

	/* clean up the client environment */
	gfs_profile(gf_profile = 0); /* not to display profile statistics */
	(void)gfarm_terminate();

	execve(path, argv, envp);
	return gfarm_errno_to_error(errno);
}

/*
 * gfs_execve_legacy() is used to execute a non-gfarm program that does not
 * call both gfarm_initialize() and gfarm_terminate().
 */
char *
gfs_execve_legacy(const char *filename, char *const argv[], char *const envp[])
{
	char *localpath, *url, *e;
	const char *path;
	pid_t pid;
	int status;

	e = gfs_realpath(filename, &url);
	if (e != NULL)
		return (e);

	e = gfs_execve_replicate_to_local(url, &localpath);
	free(url);
	if (e != NULL)
		return (e);
	path = localpath;

	pid = fork();
	if (pid == 0) {
		execve(path, argv, envp);
		_exit(1);
	}
	else
		waitpid(pid, &status, 0);

	e = gfarm_terminate();
	if (e != NULL)
		return (e);
	exit(status);
}

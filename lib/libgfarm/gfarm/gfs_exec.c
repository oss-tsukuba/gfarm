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

#include <stdlib.h>
#include <sys/stat.h>

#include <fcntl.h>

#define LOCKFILE_SUF	":::lock"

static int
gfs_i_lockfile(char *file, char **lockfile)
{
	char *lfile;

	*lockfile = NULL;
	if (file == NULL)
		return (-1);

	lfile = malloc(strlen(file) + sizeof(LOCKFILE_SUF));
	if (lfile == NULL)
		return (-1);
	sprintf(lfile, "%s%s", file, LOCKFILE_SUF);
	*lockfile = lfile;

	return (0);
}

static int
gfs_i_lock(char *file)
{
	struct stat st;
	char *lockfile;
	int fd;
	mode_t saved_mask;

	if (file == NULL)
		return (-2);
	if (gfs_i_lockfile(file, &lockfile))
		return (-2);
	
	saved_mask = umask(0);
	fd = open(lockfile, O_CREAT | O_EXCL, 0644);
	umask(saved_mask);
	free(lockfile);
	if (fd != -1) {
		close(fd);
		return (0);
	}
	else if (errno != EEXIST)
		return (-2); /* other reasons.  cannot lock */

	return (-1);
}

static int
gfs_i_unlock(char *file)
{
	char *lockfile;

	if (gfs_i_lockfile(file, &lockfile))
		return (-1);

	unlink(lockfile);
	free(lockfile);
	return (0);
}

#define USLEEP_INTERVAL	100	/* 100 msec */

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

	/* critical section starts */
	while (gfs_i_lock(localpath) == -1) {
		usleep(USLEEP_INTERVAL);
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

	gfs_i_unlock(localpath);
	/* critical section ends */

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

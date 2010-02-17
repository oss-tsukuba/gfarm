/*
 * $Id$
 */

#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>

#include "gfs_profile.h"
#include "gfs_misc.h"

char *
gfarm_url_execfile_replicate_to_local(const char *url, char **local_pathp)
{
	char *e, *arch = NULL, *gfarm_file, *localpath;
	struct gfs_stat gs;
	gfarm_mode_t gmode;
	struct gfarm_file_section_info sinfo;

	*local_pathp = NULL;

	e = gfs_stat(url, &gs);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfs_stat(%s) failed: %s",
			url,
			gfarm_error_string(e));
		return (e);
	}

#if 0 /* this should be 1? but maybe too dangerous */
	e = gfarm_fabricate_mode_for_replication(&gs, &gmode);
	if (e != NULL) {
		/**/
	} else
#else
	gmode = gs.st_mode;
#endif
	if (GFARM_S_ISDIR(gs.st_mode)) {
		e = GFARM_ERR_IS_A_DIRECTORY;
		gflog_debug(GFARM_MSG_UNFIXED,
			"URL (%s) is a directory: %s",
			url,
			gfarm_error_string(e));
	} else if (!GFARM_S_ISREG(gs.st_mode)) {
		e = "unknown format";
		gflog_debug(GFARM_MSG_UNFIXED,
			"URL (%s) is not regular file",
			url);
	} else if (GFARM_S_IS_FRAGMENTED_FILE(gs.st_mode)) {
		arch = "0";
	} else if (GFARM_S_IS_PROGRAM(gs.st_mode)) {
		if (gfarm_host_get_self_architecture(&arch) != NULL) {
			e = "not a file system node";
			gflog_debug(GFARM_MSG_UNFIXED, e);
		}
	} else {
		e = "unknown format";
		gflog_debug(GFARM_MSG_UNFIXED, e);
	}
	gfs_stat_free(&gs);
	if (e != NULL)
		return (e);

	/* determine the local pathname */
	e = gfarm_url_make_path(url, &gfarm_file);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"Conversion from URL (%s) to a local pathname "
			"failed: %s",
			url,
			gfarm_error_string(e));
		return (e);
	}

	/* check the metadata */
	e = gfarm_file_section_info_get(gfarm_file, arch, &sinfo);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = gfarm_file_section_info_get(gfarm_file, "noarch", &sinfo);
	free(gfarm_file);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"getting file section info (%s) failed: %s",
			gfarm_file,
			gfarm_error_string(e));
		return (e);
	}

	e = gfarm_file_section_replicate_to_local_with_locking(&sinfo, gmode,
	    &localpath);
	gfarm_file_section_info_free(&sinfo);

	if (e == NULL)
		*local_pathp = localpath;
	else
		gflog_debug(GFARM_MSG_UNFIXED,
			"replication of file section to local "
			"with locking failed (%s): %s",
			gfarm_file,
			gfarm_error_string(e));

	return (e);
}

char *
gfarm_url_program_get_local_path(const char *url, char **local_path)
{
	char *e;

	*local_path = NULL;

	/* check the permission */
	e = gfs_access(url, X_OK);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"access to url for X_OK permission (%s) failed: %s",
			url,
			gfarm_error_string(e));
		return (e);
	}

	return (gfarm_url_execfile_replicate_to_local(url, local_path));
}

char *
gfs_execve(const char *filename, char *const argv[], char *const envp[])
{
	char *localpath, *url, *e;
	const char *path;

	e = gfs_realpath(filename, &url);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"failed to get realpath for file (%s): %s",
			filename,
			gfarm_error_string(e));
		return (e);
	}

	e = gfarm_url_program_get_local_path(url, &localpath);
	free(url);
	if (e != NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"Conversion to local path from URL (%s) failed: %s",
			url,
			gfarm_error_string(e));
		return (e);
	}
	path = localpath;
#if 0	/*
	 * gfarm_terminate() should not be called here
	 * because we need to keep the LDAP connection.
	 */
	/* clean up the client environment */
	gfs_profile_unset(); /* not to display profile statistics */
	(void)gfarm_terminate();
#endif
	execve(path, argv, envp);
	return gfarm_errno_to_error(errno);
}

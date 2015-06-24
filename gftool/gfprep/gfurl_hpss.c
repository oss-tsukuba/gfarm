/*
 * $Id$
 */

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <gfarm/gfarm.h>

#include "gfurl.h"
#include "gfarm_cmd.h"

static int
no_stdin(int fd_stdin, void *arg)
{
	close(fd_stdin);
	return (0);
}

static gfarm_error_t
gfurl_hpss_lstat(const char *path, struct gfurl_stat *stp)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}


static gfarm_error_t
gfurl_hpss_exist(const char *path)
{
	int retv;
	char *cmd[] = {"hsi", "-q", "ls", "-d", (char *)path, NULL};

	retv = gfarm_cmd_exec(cmd, no_stdin, NULL, 0, 0); /* hide stderr */
	if (retv == 0)
		return (GFARM_ERR_NO_ERROR);

	return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY); /* may be other error */
}

static gfarm_error_t
gfurl_hpss_lutimens(
	const char *path, struct gfarm_timespec *atimep,
	struct gfarm_timespec *mtimep)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_hpss_chmod(const char *path, int mode)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_hpss_mkdir(const char *path, int mode, int skip_existing)
{
	int retv;
	char *cmd[] = {"hsi", "-q", "mkdir", (char *)path, NULL};

	retv = gfarm_cmd_exec(cmd, no_stdin, NULL, 0, 1);
	if (retv == 0)
		return (GFARM_ERR_NO_ERROR);

	return (GFARM_ERR_ALREADY_EXISTS); /* may be other error */
}

static gfarm_error_t
gfurl_hpss_rmdir(const char *path)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_hpss_readlink(const char *path, char **targetp)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_hpss_symlink(const char *path, char *target)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/* public */

const char GFURL_HPSS_PREFIX[] = "hpss:";

int
gfurl_path_is_hpss(const char *path)
{
	return (path ?
	    memcmp(path, GFURL_HPSS_PREFIX, GFURL_HPSS_PREFIX_LENGTH) == 0 : 0);
}

const struct gfurl_functions gfurl_func_hpss = {
	.lstat = gfurl_hpss_lstat,
	.exist = gfurl_hpss_exist,
	.lutimens = gfurl_hpss_lutimens,
	.chmod = gfurl_hpss_chmod,
	.mkdir = gfurl_hpss_mkdir,
	.rmdir = gfurl_hpss_rmdir,
	.readlink = gfurl_hpss_readlink,
	.symlink = gfurl_hpss_symlink,
};

int
gfurl_hpss_is_available(void)
{
	return (gfurl_hpss_exist(".") == GFARM_ERR_NO_ERROR ? 1 : 0);
}

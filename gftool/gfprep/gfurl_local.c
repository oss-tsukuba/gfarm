/*
 * $Id$
 */

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>

#include <gfarm/gfarm.h>

#include "nanosec.h"

#include "gfmsg.h"
#include "gfurl.h"

static void
gfurl_convert_local_stat(struct stat *from_st, struct gfurl_stat *to_st)
{
	to_st->nlink = from_st->st_nlink;
	to_st->mode = from_st->st_mode;
	to_st->size = from_st->st_size;
	to_st->mtime.tv_sec = from_st->st_mtime;
	to_st->mtime.tv_nsec = gfarm_stat_mtime_nsec(from_st);
}

static gfarm_error_t
gfurl_local_lstat(const char *path, struct gfurl_stat *stp)
{
	int retv, save_errno;
	struct stat st;

	retv = lstat(path, &st);
	if (retv == -1) {
		save_errno = errno;
		if (save_errno != ENOENT)
			gfmsg_debug("lstat(%s): %s",
			     path, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	gfurl_convert_local_stat(&st, stp);
	return (GFARM_ERR_NO_ERROR);
}


static gfarm_error_t
gfurl_local_exist(const char *path)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_local_lutimens(
	const char *path, struct gfarm_timespec *atimep,
	struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;
	int retv, save_errno;
	struct timespec ts[2];

	ts[0].tv_sec = atimep->tv_sec;
	ts[1].tv_sec = mtimep->tv_sec;
	ts[0].tv_nsec = atimep->tv_nsec;
	ts[1].tv_nsec = mtimep->tv_nsec;
	retv = gfarm_local_lutimens(path, ts);
	if (retv == -1) {
		save_errno = errno;
		e = gfarm_errno_to_error(save_errno);
		if (save_errno == EOPNOTSUPP)
			gfmsg_info_e(e, "gfarm_local_lutimens(%s)", path);
		else
			gfmsg_debug_e(e, "gfarm_local_lutimens(%s)", path);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_local_chmod(const char *path, int mode)
{
	gfarm_error_t e;
	int retv;

	retv = chmod(path, (mode_t)mode);
	if (retv == -1) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug_e(e, "chmod(%s, %o)", path, mode);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_local_mkdir(const char *path, int mode, int skip_existing)
{
	gfarm_error_t e;
	int retv;

	retv = mkdir(path, (mode_t)mode);
	if (skip_existing && retv == -1 && errno == EEXIST)
		retv = 0;
	if (retv == -1) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug_e(e, "mkdir(%s)", path);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_local_rmdir(const char *path)
{
	gfarm_error_t e;
	int retv;

	retv = rmdir(path);
	if (retv == -1) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug_e(e, "rmdir(%s)", path);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_local_readlink(const char *path, char **targetp)
{
	gfarm_error_t e;
	size_t bufsize = 4096;
	char buf[bufsize];
	ssize_t len;

	len = readlink(path, buf, bufsize - 1);
	if (len == -1) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug_e(e, "readlink(%s)", path);
		return (e);
	}
	buf[len] = '\0';
	*targetp = strdup(buf);
	if (targetp == NULL) {
		gfmsg_error("readlink(%s): no memory", path);
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_local_symlink(const char *path, char *target)
{
	gfarm_error_t e;
	int retv;

	retv = symlink(target, path);
	if (retv == -1) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug_e(e, "symlink(%s)", path);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

/* public */

const char GFURL_LOCAL_PREFIX[] = "file:";

int
gfurl_path_is_local(const char *path)
{
	return (path ? memcmp(path, GFURL_LOCAL_PREFIX,
	    GFURL_LOCAL_PREFIX_LENGTH) == 0 : 0);
}

const struct gfurl_functions gfurl_func_local = {
	.lstat = gfurl_local_lstat,
	.exist = gfurl_local_exist,
	.lutimens = gfurl_local_lutimens,
	.chmod = gfurl_local_chmod,
	.mkdir = gfurl_local_mkdir,
	.rmdir = gfurl_local_rmdir,
	.readlink = gfurl_local_readlink,
	.symlink = gfurl_local_symlink,
};

/*
 * $Id$
 */

#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include <gfarm/gfarm.h>

#include "gfmsg.h"
#include "gfurl.h"

static void
gfurl_convert_gfs_stat(struct gfs_stat *from_st, struct gfurl_stat *to_st)
{
	to_st->nlink = from_st->st_nlink;
	to_st->mode = from_st->st_mode;
	to_st->size = from_st->st_size;
	to_st->mtime = from_st->st_mtimespec;
}

static gfarm_error_t
gfurl_gfarm_lstat(const char *path, struct gfurl_stat *stp)
{
	struct gfs_stat st;
	gfarm_error_t e;

	e = gfs_lstat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
			gfmsg_debug("gfs_lstat(%s): %s\n",
			    path, gfarm_error_string(e));
		return (e);
	}
	gfmsg_debug("gfs_lstat(%s): OK", path);
	gfurl_convert_gfs_stat(&st, stp);
	gfs_stat_free(&st);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfurl_gfarm_exist(const char *path)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfurl_gfarm_lutimens(
	const char *path, struct gfarm_timespec *atimep,
	struct gfarm_timespec *mtimep)
{
	gfarm_error_t e;
	struct gfarm_timespec gt[2];

	gt[0].tv_sec = atimep->tv_sec;
	gt[1].tv_sec = mtimep->tv_sec;
	gt[0].tv_nsec = atimep->tv_nsec;
	gt[1].tv_nsec = mtimep->tv_nsec;
	e = gfs_lutimes(path, gt);
	gfmsg_debug_e(e, "gfs_lutimes(%s)", path);
	return (e);
}

static gfarm_error_t
gfurl_gfarm_chmod(const char *path, int mode)
{
	gfarm_error_t e;

	e = gfs_lchmod(path, (gfarm_mode_t)mode);
	gfmsg_debug_e(e, "gfs_lchmod(%s, %o)", path, mode);
	return (e);
}

static gfarm_error_t
gfurl_gfarm_mkdir(const char *path, int mode, int skip_existing)
{
	gfarm_error_t e;

	e = gfs_mkdir(path, (gfarm_mode_t)mode);
	if (skip_existing && e == GFARM_ERR_ALREADY_EXISTS)
		e = GFARM_ERR_NO_ERROR;
	gfmsg_debug_e(e, "gfs_mkdir(%s)", path);
	return (e);
}

static gfarm_error_t
gfurl_gfarm_rmdir(const char *path)
{
	gfarm_error_t e;

	e = gfs_rmdir(path);
	gfmsg_debug_e(e, "gfs_rmdir(%s)", path);
	return (e);
}

static gfarm_error_t
gfurl_gfarm_readlink(const char *path, char **targetp)
{
	gfarm_error_t e;

	e = gfs_readlink(path, targetp);
	gfmsg_debug_e(e, "gfs_readlink(%s)", path);
	return (e);
}

static gfarm_error_t
gfurl_gfarm_symlink(const char *path, char *target)
{
	gfarm_error_t e;

	e = gfs_symlink(target, path);
	gfmsg_debug_e(e, "gfs_symlink(%s)", path);
	return (e);
}

/* public */

int
gfurl_path_is_gfarm(const char *path)
{
	return (path ? gfarm_is_url(path) : 0);
}

const struct gfurl_functions gfurl_func_gfarm = {
	.lstat = gfurl_gfarm_lstat,
	.exist = gfurl_gfarm_exist,
	.lutimens = gfurl_gfarm_lutimens,
	.chmod = gfurl_gfarm_chmod,
	.mkdir = gfurl_gfarm_mkdir,
	.rmdir = gfurl_gfarm_rmdir,
	.readlink = gfurl_gfarm_readlink,
	.symlink = gfurl_gfarm_symlink,
};

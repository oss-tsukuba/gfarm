/*
 * fstat
 *
 * $Id$
 */

extern int gfarm_node;

#ifndef FUNC__FXSTAT

int
FUNC___FSTAT(int filedes, STRUCT_STAT *buf)
{
	GFS_File gf;
	char *e;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___FSTAT) "(%d)",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FSTAT(filedes, buf));

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: Hooking " S(FUNC___FSTAT) "(%d)", filedes));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_REG) {
		struct gfs_stat status;

		e = gfs_fstat(gf, &status);
		if (e != NULL)
			goto error;

		memchr(buf, 0, sizeof(*buf));
		buf->st_dev = GFS_DEV;
		buf->st_ino = status.st_ino;
		buf->st_mode = status.st_mode;
		buf->st_nlink = 1;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = status.st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = (status.st_size + STAT_BLKSIZ-1)/ STAT_BLKSIZ;
		buf->st_atime = status.st_atimespec.tv_sec;
		buf->st_mtime = status.st_mtimespec.tv_sec;
		buf->st_ctime = status.st_ctimespec.tv_sec;

		gfs_stat_free(&status);
	} else {
		struct gfs_stat *gsp;

		gsp = gfs_hook_get_gfs_stat(filedes);

		memchr(buf, 0, sizeof(*buf));
		buf->st_dev = GFS_DEV;
		buf->st_ino = gsp->st_ino;
		buf->st_mode = gsp->st_mode;
		buf->st_nlink = GFS_NLINK_DIR;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = gsp->st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = (gsp->st_size + STAT_BLKSIZ-1) / STAT_BLKSIZ;
		buf->st_atime = gsp->st_atimespec.tv_sec;
		buf->st_mtime = gsp->st_mtimespec.tv_sec;
		buf->st_ctime = gsp->st_ctimespec.tv_sec;
	}
	errno = errno_save;
	return (0);

error:
	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___FSTAT) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC__FSTAT) ": %d",
	filedes));
    return (FUNC___FSTAT(filedes, buf));
}

int
FUNC_FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC_FSTAT) ": %d",
	filedes));
    return (FUNC___FSTAT(filedes, buf));
}

#else /* defined(FUNC__FXSTAT) -- SVR4 or Linux */
/*
 * SVR4 and Linux do inline stat() and call _xstat/__xstat() with
 * an additional version argument.
 */

#ifdef __linux__

/*
 * unlike SVR4, stat() on linux seems to be compatible with xstat(STAT_VER,...)
 */

int
FUNC___FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___FSTAT) ": %d", filedes));
    return (FUNC___FXSTAT(_STAT_VER, filedes, buf));
}

int
FUNC_FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC_FSTAT) ": %d",
	filedes));
    return (FUNC___FXSTAT(_STAT_VER, filedes, buf));
}

#else

/*
 * we don't provide stat(), because it is only used for SVR3 compat code.
 */

#endif

int
FUNC___FXSTAT(int ver, int filedes, STRUCT_STAT *buf)
{
	GFS_File gf;
	char *e;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___FXSTAT) "(%d)",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FXSTAT(ver, filedes, buf));

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: Hooking " S(FUNC___FXSTAT) "(%d)", filedes));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_REG) {
		struct gfs_stat status;

		e = gfs_fstat(gf, &status);
		if (e != NULL)
			goto error;

		buf->st_dev = GFS_DEV;
		buf->st_ino = status.st_ino;
		buf->st_mode = status.st_mode;
		buf->st_nlink = 1;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = status.st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = (status.st_size + STAT_BLKSIZ-1)/ STAT_BLKSIZ;
		buf->st_atime = status.st_atimespec.tv_sec;
		buf->st_mtime = status.st_mtimespec.tv_sec;
		buf->st_ctime = status.st_ctimespec.tv_sec;

		gfs_stat_free(&status);
	} else {
		struct gfs_stat *gsp;

		gsp = gfs_hook_get_gfs_stat(filedes);
		buf->st_dev = GFS_DEV;
		buf->st_ino = gsp->st_ino;
		buf->st_mode = gsp->st_mode;
		buf->st_nlink = GFS_NLINK_DIR;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = gsp->st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_blocks = (gsp->st_size + STAT_BLKSIZ-1) / STAT_BLKSIZ;
		buf->st_atime = gsp->st_atimespec.tv_sec;
		buf->st_mtime = gsp->st_mtimespec.tv_sec;
		buf->st_ctime = gsp->st_ctimespec.tv_sec;
	}
	errno = errno_save;
	return (0);

error:
	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___FXSTAT) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__FXSTAT(int ver, int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__FXSTAT) ": %d", filedes));
    return (FUNC___FXSTAT(ver, filedes, buf));
}

#endif /* SVR4 or Linux */

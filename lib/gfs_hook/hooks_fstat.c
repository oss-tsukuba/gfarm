/*
 * fstat
 *
 * $Id$
 */

extern int gfarm_node;

#ifndef _STAT_VER

int
FUNC___FSTAT(int filedes, STRUCT_STAT *buf)
{
	GFS_File gf;
	struct gfs_stat status;
	char *e;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___FSTAT) "(%d)\n",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FSTAT(filedes, buf));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking " S(FUNC___FSTAT)
	    "(%d(%d))\n", filedes, gfs_pio_fileno(gf)));

	e = gfs_fstat(gf, &status);
	if (e != NULL)
		return (-1);

	buf->st_mode = status.st_mode;
	buf->st_nlink = 1;
	buf->st_uid = getuid();
	buf->st_gid = getgid();
	buf->st_size = status.st_size;
	buf->st_atime = status.st_atimespec.tv_sec;
	buf->st_mtime = status.st_mtimespec.tv_sec;
	buf->st_ctime = status.st_ctimespec.tv_sec;

	gfs_stat_free(&status);

	return (0);
}

int
FUNC__FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC__FSTAT) ": %d\n",
	filedes));
    return (FUNC___FSTAT(filedes, buf));
}

int
FUNC_FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_FSTAT) ": %d\n",
	filedes));
    return (FUNC___FSTAT(filedes, buf));
}

#else /* defined(_STAT_VER) -- SVR4 or Linux */
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
	fprintf(stderr, "Hooking " S(FUNC___FSTAT) ": %d\n", filedes));
    return (FUNC___FXSTAT(_STAT_VER, filedes, buf));
}

int
FUNC_FSTAT(int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_FSTAT) ": %d\n",
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
	struct gfs_stat status;
	char *e;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___FXSTAT) "(%d)\n",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FXSTAT(ver, filedes, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___FXSTAT) "(%d)\n", filedes));

	e = gfs_fstat(gf, &status);
	if (e != NULL)
		return (-1);

	buf->st_mode = status.st_mode;
	buf->st_nlink = 1;
	buf->st_uid = getuid();
	buf->st_gid = getgid();
	buf->st_size = status.st_size;
	buf->st_atime = status.st_atimespec.tv_sec;
	buf->st_mtime = status.st_mtimespec.tv_sec;
	buf->st_ctime = status.st_ctimespec.tv_sec;

	gfs_stat_free(&status);

	return (0);
}

int
FUNC__FXSTAT(int ver, int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	fprintf(stderr, "Hooking " S(FUNC__FXSTAT) ": %d\n", filedes));
    return (FUNC___FXSTAT(ver, filedes, buf));
}

int
FUNC_FXSTAT(int ver, int filedes, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	fprintf(stderr, "Hooking " S(FUNC_FXSTAT) ": %d\n", filedes));
    return (FUNC___FXSTAT(ver, filedes, buf));
}

#endif /* SVR4 or Linux */

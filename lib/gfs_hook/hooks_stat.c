/*
 * stat
 *
 * $Id$
 */

extern int gfarm_node;

#ifndef _STAT_VER

int
FUNC___STAT(const char *path, STRUCT_STAT *buf)
{
	const char *e;
	char *url, *sec;
	struct gfs_stat gs;
#if 0
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___STAT) "(%s)\n",
	    path));

	if (!gfs_hook_is_url(path, &url, &sec))
		return (SYSCALL_STAT(path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___STAT) "(%s)\n", path));

#if 1
	if (sec != NULL)
		e = GFS_STAT_SECTION(url, sec, &gs);
	else {
		/* first try in local view. */
		e = GFS_STAT_INDEX(url, gfarm_node, &gs);
		if (e != NULL)
			e = GFS_STAT(url, &gs);
	}
	free(url);
	if (sec != NULL)
		free(sec);
	if (e == NULL) {
		buf->st_mode = gs.st_mode;
		buf->st_nlink = 1;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = gs.st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_atime = gs.st_atimespec.tv_sec;
		buf->st_mtime = gs.st_mtimespec.tv_sec;
		buf->st_ctime = gs.st_ctimespec.tv_sec;
		gfs_stat_free(&gs);

		return (0);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___STAT) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
#else /* Temporary code until gfs_stat() will be implemented. */
	/*
	 * gfs_stat() may not appropriate here, because:
	 * 1. it doesn't/can't fill all necessary field of struct stat.
	 * 2. it returns information of whole gfarm file, rather than
	 *    information of the fragment.
	 */

	e = gfarm_url_make_path(url, &canonic_path);
	free(url);
	if (sec != NULL)
		free(sec);
	if (e != NULL) {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	e = gfarm_path_localize_file_fragment(canonic_path, gfarm_node,
	    &abs_path);
	free(canonic_path);
	if (e != NULL) {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___STAT) " locally: %s\n", abs_path));
	r = SYSCALL_STAT(abs_path, buf);
	save_errno = errno;
	free(abs_path);
	errno = save_errno;
	return (r);
#endif
}

int
FUNC__STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC__STAT) ": %s\n", path));
    return (FUNC___STAT(path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_STAT) ": %s\n", path));
    return (FUNC___STAT(path, buf));
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
FUNC___STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	fprintf(stderr, "Hooking " S(FUNC___STAT) ": %s\n", path));
    return (FUNC___XSTAT(_STAT_VER, path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_STAT) ": %s\n", path));
    return (FUNC___XSTAT(_STAT_VER, path, buf));
}

#else

/*
 * we don't provide stat(), because it is only used for SVR3 compat code.
 */

#endif

int
FUNC___XSTAT(int ver, const char *path, STRUCT_STAT *buf)
{
	const char *e;
	char *url, *sec;
	struct gfs_stat gs;
#if 0
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___XSTAT) "(%s)\n",
	    path));

	if (!gfs_hook_is_url(path, &url, &sec))
		return (SYSCALL_XSTAT(ver, path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___XSTAT) "(%s)\n", path));

#if 1
	if (sec != NULL)
		e = GFS_STAT_SECTION(url, sec, &gs);
	else {
		/* first try in local view. */
		e = GFS_STAT_INDEX(url, gfarm_node, &gs);
		if (e != NULL)
			e = GFS_STAT(url, &gs);
	}
	free(url);
	if (sec != NULL)
		free(sec);
	if (e == NULL) {
		buf->st_mode = gs.st_mode;
		buf->st_nlink = 1;
		buf->st_uid = getuid();
		buf->st_gid = getgid();
		buf->st_size = gs.st_size;
		buf->st_blksize = GFS_BLKSIZE;
		buf->st_atime = gs.st_atimespec.tv_sec;
		buf->st_mtime = gs.st_mtimespec.tv_sec;
		buf->st_ctime = gs.st_ctimespec.tv_sec;
		gfs_stat_free(&gs);

		return (0);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___XSTAT) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
#else /* Temporary code until gfs_stat() will be implemented. */
	e = gfarm_url_make_path(url, &canonic_path);
	free(url);
	if (sec != NULL)
		free(sec);
	if (e != NULL) {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	e = gfarm_path_localize_file_fragment(canonic_path, gfarm_node,
	    &abs_path);
	free(canonic_path);
	if (e != NULL) {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___XSTAT) " locally: %s\n", abs_path));
	r = SYSCALL_XSTAT(ver, abs_path, buf);
	save_errno = errno;
	free(abs_path);
	errno = save_errno;
	return (r);
#endif
}

int
FUNC__XSTAT(int ver, const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	fprintf(stderr, "Hooking " S(FUNC__XSTAT) ": %s\n", path));
    return (FUNC___XSTAT(ver, path, buf));
}

int
FUNC_XSTAT(int ver, const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_XSTAT) ": %s\n", path));
    return (FUNC___XSTAT(ver, path, buf));
}

#endif /* SVR4 or Linux */

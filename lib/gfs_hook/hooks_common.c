/*
 * open
 */

int
FUNC___OPEN(const char *path, int oflag, ...)
{
	GFS_File gf;
	const char *e, *url;
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	_gfs_hook_debug(fprintf(stderr,
	    "Hooking " S(FUNC___OPEN) ": %s\n", path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_OPEN(path, oflag, mode));

	if (oflag & O_CREAT) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) " (create): %s\n", path));
		e = gfs_pio_create(url, oflag, mode, &gf);
	} else {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) ": %s\n", path));
		e = gfs_pio_open(url, oflag, &gf);
	}
	if (e != NULL) {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	if ((e = gfs_pio_set_view_local(gf, 0)) != NULL) {
		gfs_pio_close(gf);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	return (gfs_hook_insert_gfs_file(gf));
}

int
FUNC__OPEN(const char *path, int oflag, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);
	_gfs_hook_debug(fputs("Hooking " S(FUNC__OPEN) "\n", stderr));
	return (FUNC___OPEN(path, oflag, mode));
}

int
FUNC_OPEN(const char *path, int oflag, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);
	_gfs_hook_debug(fputs("Hooking " S(FUNC_OPEN) "\n", stderr));
	return (FUNC___OPEN(path, oflag, mode));
}

/*
 *  creat
 */

int
FUNC___CREAT(const char *path, mode_t mode)
{
	const char *e, *url;
	GFS_File gf;

	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC___CREAT) ": %s\n",
	    path)); 

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_CREAT(path, mode));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking " S(FUNC___CREAT) ": %s\n",
	    path));
	e = gfs_pio_create(url, GFARM_FILE_WRONLY, mode, &gf);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: " S(FUNC___CREAT) ": %s\n", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	if ((e = gfs_pio_set_view_local(gf, 0)) != NULL) {
		gfs_pio_close(gf);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	return (gfs_hook_insert_gfs_file(gf));
}

int
FUNC__CREAT(const char *path, mode_t mode)
{
    _gfs_hook_debug(fputs("Hooking " S(FUNC__CREAT) "\n", stderr));
    return (FUNC___CREAT(path, mode));
}

int
FUNC_CREAT(const char *path, mode_t mode)
{
    _gfs_hook_debug(fputs("Hooking " S(FUNC_CREAT) "\n", stderr));
    return (FUNC___CREAT(path, mode));
}

/*
 * lseek
 */

OFF_T
FUNC___LSEEK(int filedes, OFF_T offset, int whence)
{
	GFS_File gf;
	const char *e;
	file_offset_t o;

	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC___LSEEK) ": %d\n",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_LSEEK(filedes, offset, whence));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___LSEEK) ": %d %d\n",
	    filedes, gfs_pio_fileno(gf)));

	e = gfs_pio_seek(gf, offset, whence, &o);
	if (e == NULL)
		return ((OFF_T)o);
	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___LSEEK) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

OFF_T
FUNC__LSEEK(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC__LSEEK) ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
FUNC_LSEEK(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC_LSEEK) ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

/*
 * stat
 */
#include <stdlib.h>
extern int gfarm_node;

#ifndef _STAT_VER

int
FUNC___STAT(const char *path, STRUCT_STAT *buf)
{
	const char *e, *url;
#if 1
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC___STAT) ": %s\n",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_STAT(path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___STAT) ": %s\n", path));

#if 0 /* Not yet implemented. */
	e = gfs_stat(url, buf);
	if (e == NULL)
		return (0);
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
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC__STAT) ": %s\n", path));
    return (FUNC___STAT(path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC_STAT) ": %s\n", path));
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
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC___STAT) ": %s\n", path));
    return (FUNC___XSTAT(_STAT_VER, path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC_STAT) ": %s\n", path));
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
	const char *e, *url;
#if 1
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC___XSTAT) ": %s\n",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_XSTAT(ver, path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___XSTAT) ": %s\n", path));

#if 0 /* Not yet implemented. */
	e = gfs_stat(url, buf);
	if (e == NULL)
		return (0);
	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___XSTAT) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
#else /* Temporary code until gfs_stat() will be implemented. */
	e = gfarm_url_make_path(url, &canonic_path);
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
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC__XSTAT) ": %s\n", path));
    return (FUNC___XSTAT(ver, path, buf));
}

int
FUNC_XSTAT(int ver, const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug(fprintf(stderr, "Hooking " S(FUNC_XSTAT) ": %s\n", path));
    return (FUNC___XSTAT(ver, path, buf));
}

#endif /* SVR4 or Linux */

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
#if 0
	const char *e;
#else
	int r;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___FSTAT) "(%d)\n",
	    filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FSTAT(filedes, buf));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking " S(FUNC___FSTAT)
	    "(%d(%d))\n", filedes, gfs_pio_fileno(gf)));

#if 0 /* Not yet implemented. */
	e = GFS_FSTAT(filedes, buf);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___FSTAT) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
#else /* Temporary code until gfs_stat() will be implemented. */
	/*
	 * gfs_stat() may not appropriate here, because:
	 * 1. it doesn't/can't fill all necessary field of struct stat.
	 * 2. it returns information of whole gfarm file, rather than
	 *    information of the fragment.
	 */

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___FSTAT) " locally: %d\n",
	    gfs_pio_fileno(gf)));
	r = SYSCALL_FSTAT(gfs_pio_fileno(gf), buf);
	return (r);
#endif
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
#if 0
	const char *e;
#else
	int r;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___FXSTAT) "(%s)\n",
	    path));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FXSTAT(ver, filedes, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___FXSTAT) "(%d)\n", filedes));

#if 0 /* Not yet implemented. */
	e = GFS_FSTAT(filedes, buf);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: " S(FUNC___FXSTAT) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
#else /* Temporary code until gfs_stat() will be implemented. */
	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___FXSTAT) " locally: %d\n",
	    gfs_pio_fileno(gf)));
	r = SYSCALL_FXSTAT(ver, gfs_pio_fileno(gf), buf);
	return (r);
#endif
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

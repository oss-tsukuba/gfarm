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
	char *url;
	struct gfs_stat gs;
	int nf = -1, np;
#if 0
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___STAT) "(%s)\n",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_STAT(path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___STAT) "(%s)\n", path));

#if 1
	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT_SECTION) "(%s, %s)\n",
			url, gfs_hook_get_current_section()));
		e = GFS_STAT_SECTION(url, gfs_hook_get_current_section(), &gs);
		break;
	case index_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT_INDEX) "(%s, %d)\n",
			url, gfs_hook_get_current_index()));
		e = GFS_STAT_INDEX(url, gfs_hook_get_current_index(), &gs);
		break;
	case local_view:
		/*
		 * If the number of fragments is not the same as the
		 * number of parallel processes, or the file is not
		 * fragmented, do not change to the local file view.
		 */
		if (gfarm_url_fragment_number(url, &nf) == NULL) {
			if (gfs_pio_get_node_size(&np) == NULL && nf == np) {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: " S(GFS_STAT_INDEX) "(%s, %d)\n",
					url, gfarm_node));
				e = GFS_STAT_INDEX(url, gfarm_node, &gs);
			}
			else {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: " S(GFS_STAT) "(%s)\n", url));
				e = GFS_STAT(url, &gs);
			}
		}
		else {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: " S(GFS_STAT) "(%s)\n", url));
			e = GFS_STAT(url, &gs);
		}
		if (e != NULL) {
			/* someone is possibly creating the file right now */
			/*
			 * XXX - there is no way to determine other
			 * processes are now creating the
			 * corresponding section or not in Gfarm v1.
			 * However, at least, it is possible to
			 * determine this process is now creating the
			 * section or not.
			 */
			GFS_File gf;
			if ((gf = gfs_hook_is_now_creating(url)) != NULL) {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: gfs_fstat(%p (%s))\n", gf, url));
				e = gfs_fstat(gf, &gs);
			}
			else
				e = GFARM_ERR_NO_SUCH_OBJECT;
		}
		break;
	default:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT) "(%s)\n", url));
		e = GFS_STAT(url, &gs);
	}
	free(url);
	if (e == NULL) {
		struct passwd *p;

		buf->st_dev = GFS_DEV;
		buf->st_ino = gs.st_ino;
		buf->st_mode = gs.st_mode;
		buf->st_nlink = 1;

		/* XXX FIXME: need to convert gfarm global user to UNIX uid */
		p = getpwnam(gfarm_get_local_username());
		if (p != NULL) {
			buf->st_uid = p->pw_uid;
			buf->st_gid = p->pw_gid;
		} else {
			buf->st_uid = getuid(); /* XXX */
			buf->st_gid = getgid(); /* XXX */
		}
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
	char *url;
	struct gfs_stat gs;
	int nf = -1, np;
#if 0
	char *canonic_path, *abs_path;
	int r, save_errno;
#endif

	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC___XSTAT) "(%s)\n",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_XSTAT(ver, path, buf));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___XSTAT) "(%s)\n", path));

#if 1
	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT_SECTION) "(%s, %s)\n",
			url, gfs_hook_get_current_section()));
		e = GFS_STAT_SECTION(url, gfs_hook_get_current_section(), &gs);
		break;
	case index_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT_INDEX) "(%s, %d)\n",
			url, gfs_hook_get_current_index()));
		e = GFS_STAT_INDEX(url, gfs_hook_get_current_index(), &gs);
		break;
	case local_view:
		/*
		 * If the number of fragments is not the same as the
		 * number of parallel processes, or the file is not
		 * fragmented, do not change to the local file view.
		 */
		if (gfarm_url_fragment_number(url, &nf) == NULL) {
			if (gfs_pio_get_node_size(&np) == NULL && nf == np) {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: " S(GFS_STAT_INDEX) "(%s, %d)\n",
					url, gfarm_node));
				e = GFS_STAT_INDEX(url, gfarm_node, &gs);
			}
			else {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: " S(GFS_STAT) "(%s)\n", url));
				e = GFS_STAT(url, &gs);
			}
		}
		else {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: " S(GFS_STAT) "(%s)\n", url));
			e = GFS_STAT(url, &gs);
		}
		if (e != NULL) {
			/* someone is possibly creating the file right now */
			/*
			 * XXX - there is no way to determine other
			 * processes are now creating the corresponding
			 * section or not.  However, at least, it is
			 * possible to determine this process is now
			 * creating the section or not.
			 */
			GFS_File gf;
			if ((gf = gfs_hook_is_now_creating(url)) != NULL) {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: gfs_fstat(%p (%s))\n", gf, url));
				e = gfs_fstat(gf, &gs);
			}
			else
				e = GFARM_ERR_NO_SUCH_OBJECT;
		}
		break;
	default:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: " S(GFS_STAT) "(%s)\n", url));
		e = GFS_STAT(url, &gs);
	}
	free(url);
	if (e == NULL) {
		struct passwd *p;

		buf->st_dev = GFS_DEV;	  
		buf->st_ino = gs.st_ino;
		buf->st_mode = gs.st_mode;
		buf->st_nlink = 1;

		/* XXX FIXME: need to convert gfarm global user to UNIX uid */
		p = getpwnam(gfarm_get_local_username());
		if (p != NULL) {
			buf->st_uid = p->pw_uid;
			buf->st_gid = p->pw_gid;
		} else {
			buf->st_uid = getuid(); /* XXX */
			buf->st_gid = getgid(); /* XXX */
		}
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

#endif /* SVR4 or Linux */

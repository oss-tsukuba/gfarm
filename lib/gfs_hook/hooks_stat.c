/*
 * stat
 *
 * $Id$
 */

extern int gfarm_node;

#ifndef FUNC__XSTAT

int
FUNC___STAT(const char *path, STRUCT_STAT *buf)
{
	const char *e;
	char *url;
	struct gfs_stat gs;
	int nf = -1, np, errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___STAT) "(%s)",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_STAT(path, buf));

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: Hooking " S(FUNC___STAT) "(%s)", path));

	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT_SECTION) "(%s, %s)",
			url, gfs_hook_get_current_section()));
		e = GFS_STAT_SECTION(url, gfs_hook_get_current_section(), &gs);
		break;
	case index_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT_INDEX) "(%s, %d)",
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
				_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
					"GFS: " S(GFS_STAT_INDEX) "(%s, %d)",
					url, gfarm_node));
				e = GFS_STAT_INDEX(url, gfarm_node, &gs);
			}
			else {
				_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
					"GFS: " S(GFS_STAT) "(%s)", url));
				e = GFS_STAT(url, &gs);
			}
		}
		else {
			_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS: " S(GFS_STAT) "(%s)", url));
			e = GFS_STAT(url, &gs);
		}
		break;
	default:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT) "(%s)", url));
		e = GFS_STAT(url, &gs);
	}
	free(url);
	if (e == NULL) {
		struct passwd *p;

		memset(buf, 0, sizeof(*buf));
		buf->st_dev = GFS_DEV;
		buf->st_ino = gs.st_ino;
		buf->st_mode = gs.st_mode;
		buf->st_nlink = S_ISDIR(buf->st_mode) ? GFS_NLINK_DIR : 1;

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
		buf->st_blocks = (gs.st_size + STAT_BLKSIZ - 1) / STAT_BLKSIZ;
		buf->st_atime = gs.st_atimespec.tv_sec;
		buf->st_mtime = gs.st_mtimespec.tv_sec;
		buf->st_ctime = gs.st_ctimespec.tv_sec;
		gfs_stat_free(&gs);

		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___STAT) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC__STAT) ": %s", path));
    return (FUNC___STAT(path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC_STAT) ": %s", path));
    return (FUNC___STAT(path, buf));
}

#else /* defined(FUNC__XSTAT) -- SVR4 or Linux */
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
	gflog_info(GFARM_MSG_UNFIXED, "Hooking " S(FUNC___STAT) ": %s", path));
    return (FUNC___XSTAT(_STAT_VER, path, buf));
}

int
FUNC_STAT(const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	"Hooking " S(FUNC_STAT) ": %s", path));
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
	int nf = -1, np, errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___XSTAT) "(%s)",
	    path));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_XSTAT(ver, path, buf));

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: Hooking " S(FUNC___XSTAT) "(%s)", path));

	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT_SECTION) "(%s, %s)",
			url, gfs_hook_get_current_section()));
		e = GFS_STAT_SECTION(url, gfs_hook_get_current_section(), &gs);
		break;
	case index_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT_INDEX) "(%s, %d)",
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
				_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
					"GFS: " S(GFS_STAT_INDEX) "(%s, %d)",
					url, gfarm_node));
				e = GFS_STAT_INDEX(url, gfarm_node, &gs);
			}
			else {
				_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
					"GFS: " S(GFS_STAT) "(%s)", url));
				e = GFS_STAT(url, &gs);
			}
		}
		else {
			_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS: " S(GFS_STAT) "(%s)", url));
			e = GFS_STAT(url, &gs);
		}
		break;
	default:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: " S(GFS_STAT) "(%s)", url));
		e = GFS_STAT(url, &gs);
	}
	free(url);
	if (e == NULL) {
		struct passwd *p;

		memset(buf, 0, sizeof(*buf));
		buf->st_dev = GFS_DEV;	  
		buf->st_ino = gs.st_ino;
		buf->st_mode = gs.st_mode;
		buf->st_nlink = S_ISDIR(buf->st_mode) ? GFS_NLINK_DIR : 1;

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
		buf->st_blocks = (gs.st_size + STAT_BLKSIZ - 1) / STAT_BLKSIZ;
		buf->st_atime = gs.st_atimespec.tv_sec;
		buf->st_mtime = gs.st_mtimespec.tv_sec;
		buf->st_ctime = gs.st_ctimespec.tv_sec;
		gfs_stat_free(&gs);

		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___XSTAT) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__XSTAT(int ver, const char *path, STRUCT_STAT *buf)
{
    _gfs_hook_debug_v(
	gflog_info(GFARM_MSG_UNFIXED, "Hooking " S(FUNC__XSTAT) ": %s", path));
    return (FUNC___XSTAT(ver, path, buf));
}

#endif /* SVR4 or Linux */

/*
 * $Id$
 */

/*
 * open
 */

int
FUNC___OPEN(const char *path, int oflag, ...)
{
	GFS_File gf;
	const char *e;
	char *url, *sec;
	va_list ap;
	mode_t mode;
	int filedes;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___OPEN) "(%s, 0x%x)\n", path, oflag));

retry:
	if (!gfs_hook_is_url(path, &url, &sec))
		return (SYSCALL_OPEN(path, oflag, mode));

	/* XXX - ROOT I/O creates a new file with O_CREAT|O_RDWR mode. */
	/* XXX - FIXME */
	if ((oflag & O_CREAT) != 0 || (oflag & O_TRUNC) != 0) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) "(%s:%s, 0x%x, 0%o)\n",
		    path, sec != NULL ? sec : "(null)", oflag, mode));
		if (oflag & O_TRUNC) {
			/*
			 * Hooking open syscall does not mean to open
			 * an entire file but a file fragment in local and
			 * index file views.  gfs_unlink() should not be
			 * called in both views.
			 */
			if (_gfs_hook_default_view == global_view)
				gfs_unlink(path); /* XXX - FIXME */
			e = gfs_pio_create(url, oflag, mode, &gf);
		} else {
			e = gfs_pio_open(url, oflag, &gf);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) /* XXX - FIXME */
				e = gfs_pio_create(url, oflag, mode, &gf);
		}
	} else {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) "(%s:%s, 0x%x)\n",
		    path, sec != NULL ? sec : "(null)", oflag));
		e = gfs_pio_open(url, oflag, &gf);
	}
	free(url);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) ": %s\n", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	if (sec != NULL || _gfs_hook_default_view == index_view) {
		if (sec != NULL) {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_section(%s, %s)\n", path, sec));
			e = gfs_pio_set_view_section(gf, sec, NULL, 0);
			free(sec);
		} else {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_index(%s, %d, %d)\n", path,
				_gfs_hook_num_fragments, _gfs_hook_index));
			e = gfs_pio_set_view_index(gf, _gfs_hook_num_fragments,
				_gfs_hook_index, NULL, 0);
		}
		if (e != NULL) {
			_gfs_hook_debug(fprintf(stderr,
			    "GFS: set_view_section: %s\n", e));
			gfs_pio_close(gf);
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
	} else if (_gfs_hook_default_view == local_view) {
		int nf = -1, np;
		/*
		 * If the number of fragments is not the same as the
		 * number of parallel processes, or the file is not
		 * fragmented, do not change to the local file view.
		 */
		if (gfs_pio_get_nfragment(gf, &nf) ==
			GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE ||
		    (gfs_pio_get_node_size(&np) == NULL && nf == np)) {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_local(%s (%d, %d))\n",
					path, gfarm_node, gfarm_nnode));
			if ((e = gfs_pio_set_view_local(gf, 0)) != NULL) {
				_gfs_hook_debug(fprintf(stderr,
					"GFS: set_view_local: %s\n", e));
				gfs_pio_close(gf);
				if (e == GFARM_ERR_NO_SUCH_OBJECT) {
					/*
					 * corrupted GFarmPath entry,
					 * possibly caused by coredump.
					 */
					gfs_unlink(path); /* XXX - FIXME */
					goto retry; /* XXX - FIXME */
				}
				errno = gfarm_error_to_errno(e);
				return (-1);
			}
		}
	}
	filedes = gfs_hook_insert_gfs_file(gf);
	_gfs_hook_debug(
		if (filedes != -1) {
			fprintf(stderr,
			    "GFS: Hooking " S(FUNC___OPEN) " --> %d(%d)\n",
			    filedes, gfs_pio_fileno(gf));
		}
	);
	return (filedes);
}

int
FUNC__OPEN(const char *path, int oflag, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC__OPEN) "\n", stderr));
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
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC_OPEN) "\n", stderr));
	return (FUNC___OPEN(path, oflag, mode));
}

/*
 *  creat
 */

int
FUNC___CREAT(const char *path, mode_t mode)
{
	const char *e;
	char *url, *sec;
	GFS_File gf;
	int filedes;

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___CREAT) "(%s, 0%o)\n", path, mode)); 

	if (!gfs_hook_is_url(path, &url, &sec))
		return (SYSCALL_CREAT(path, mode));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking " S(FUNC___CREAT) "(%s:%s, 0%o)\n",
	    path, sec != NULL ? sec : "(null)", mode));
	e = gfs_pio_create(url, GFARM_FILE_WRONLY, mode, &gf);
	free(url);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: " S(FUNC___CREAT) ": %s\n", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	if (sec != NULL || _gfs_hook_default_view == index_view) {
		if (sec != NULL) {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_section(%s, %s)\n", path, sec));
			e = gfs_pio_set_view_section(gf, sec, NULL, 0);
			free(sec);
		} else {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_index(%s, %d, %d)\n", path,
				_gfs_hook_num_fragments, _gfs_hook_index));
			e = gfs_pio_set_view_index(gf, _gfs_hook_num_fragments,
				_gfs_hook_index, NULL, 0);
		}
		if (e != NULL) {
			_gfs_hook_debug(fprintf(stderr,
			    "GFS: set_view_section: %s\n", e));
			gfs_pio_close(gf);
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
	} else if (_gfs_hook_default_view == local_view) {
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_local(%s)\n", path));
		if ((e = gfs_pio_set_view_local(gf, 0)) != NULL) {
			_gfs_hook_debug(fprintf(stderr,
			    "GFS: set_view_local: %s\n", e));
			gfs_pio_close(gf);
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
	}
	filedes = gfs_hook_insert_gfs_file(gf);
	_gfs_hook_debug(
		if (filedes != -1) {
			fprintf(stderr,
			    "GFS: Hooking " S(FUNC___CREAT) " --> %d(%d)\n",
			    filedes, gfs_pio_fileno(gf));
		}
	);
	return (filedes);
}

int
FUNC__CREAT(const char *path, mode_t mode)
{
    _gfs_hook_debug_v(fputs("Hooking " S(FUNC__CREAT) "\n", stderr));
    return (FUNC___CREAT(path, mode));
}

int
FUNC_CREAT(const char *path, mode_t mode)
{
    _gfs_hook_debug_v(fputs("Hooking " S(FUNC_CREAT) "\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___LSEEK) "(%d, %" PR_FILE_OFFSET ", %d)\n",
	    filedes, (file_offset_t)offset, whence));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_LSEEK(filedes, offset, whence));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking "
	    S(FUNC___LSEEK) "(%d(%d), %" PR_FILE_OFFSET ", %d)\n",
	    filedes, gfs_pio_fileno(gf), (file_offset_t)offset, whence));

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
	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC__LSEEK) ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
FUNC_LSEEK(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking " S(FUNC_LSEEK) ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

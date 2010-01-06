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
	char *url;
	va_list ap;
	mode_t mode;
	int filedes;
	struct gfs_stat gs;
	int is_directory, errno_save = errno;
	int nf = -1, np;

	va_start(ap, oflag);
	/*
	 * We need `int' instead of `mode_t' in va_arg() below, because
	 * sizeof(mode_t) < sizeof(int) on some platforms (e.g. FreeBSD),
	 * and gcc-3.4/gcc-4's builtin va_arg() warns the integer promotion.
	 * XXX	this doesn't work, if sizeof(mode_t) > sizeof(int),
	 *	but currently there isn't any such platform as far as we know.
	 */
	mode = va_arg(ap, int);
	va_end(ap);

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___OPEN) "(%s, 0%o)", path, oflag));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_OPEN(path, oflag, mode));

	if (gfs_hook_get_current_view() == section_view)
		e = gfs_stat_section(url, gfs_hook_get_current_section(), &gs);
	else
		e = gfs_stat(url, &gs);
	if (e == NULL) {
		is_directory = GFARM_S_ISDIR(gs.st_mode);
		gfs_stat_free(&gs);
	} else {
		/* XXX - metadata may be incomplete. anyway, continue. */
		/* XXX - metadata of a directory should not be imcomplete */
		is_directory = 0;
		if (e != GFARM_ERR_NO_SUCH_OBJECT)
			_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			    "GFS: Hooking " S(FUNC___OPEN) ": gfs_stat: %s",
			    e));
	}

	if (is_directory) {
		GFS_Dir dir;

		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		   "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o): dir",
		    url, oflag));

		if ((oflag & (O_CREAT|O_TRUNC)) != 0 ||
		    (oflag & O_ACCMODE) != O_RDONLY) {
			free(url);
			errno = EISDIR;
			return (-1);
		}
		e = gfs_opendir(url, &dir);
		if (e == NULL) {
			filedes = gfs_hook_insert_gfs_dir(dir, url);
			if (filedes == -1) {
				errno_save = errno;
				gfs_closedir(dir);
			}
			_gfs_hook_debug(
				gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
				    S(FUNC___OPEN) " --> %d", filedes);
			);
			free(url);
			errno = errno_save;
			return (filedes);
		}
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	if ((oflag & O_CREAT) != 0) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o, 0%o)",
		    url, oflag, mode));

		oflag = gfs_hook_open_flags_gfarmize(oflag);
		e = gfs_pio_create(url, oflag, mode, &gf);
	} else {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		   "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o)", url, oflag));

		oflag = gfs_hook_open_flags_gfarmize(oflag);
		e = gfs_pio_open(url, oflag, &gf);
	}
	free(url);
	if (e != NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___OPEN) ": %s", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	/* set file view */
	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: set_view_section(%s, %s)",
			path, gfs_hook_get_current_section()));
		e = gfs_pio_set_view_section(
			gf, gfs_hook_get_current_section(), NULL, 0);
		break;
	case index_view:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: set_view_index(%s, %d, %d)", path,
			gfs_hook_get_current_nfrags(),
			gfs_hook_get_current_index()));
		e = gfs_pio_set_view_index(gf, gfs_hook_get_current_nfrags(),
			gfs_hook_get_current_index(), NULL, 0);
		break;
	case local_view:
		/*
		 * If the number of fragments is not the same as the
		 * number of parallel processes, or the file is not
		 * fragmented, do not change to the local file view.
		 */
		if ((e = gfs_pio_get_nfragment(gf, &nf)) ==
			GFARM_ERR_OPERATION_NOT_PERMITTED) { 
			/* program file */
			char *arch;
					
			e = gfarm_host_get_self_architecture(&arch);
			if (e != NULL) {
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			} else {
				e = gfs_pio_set_view_section(gf,
					 arch, NULL, 0);
				if (e == GFARM_ERR_NO_SUCH_OBJECT)
					e = gfs_pio_set_view_section(gf,
						"noarch", NULL, 0);
			}				
		} else if (e == GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE ||
		    (e == NULL && gfs_pio_get_node_size(&np) == NULL &&
		     nf == np)) {
			_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS: set_view_local(%s) @ %d/%d",
					path, gfarm_node, gfarm_nnode));
			e = gfs_pio_set_view_local(gf, 0);
		} else {
			_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS: set_view_global(%s) @ %d/%d",
					path, gfarm_node, gfarm_nnode));
			e = gfs_pio_set_view_global(gf, 0);
		}
		break;
	default:
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
			"GFS: set_view_global(%s)", path));
		e = gfs_pio_set_view_global(gf, 0);
	}
	if (e != NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: set_view: %s", e));
		gfs_pio_close(gf);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	filedes = gfs_hook_insert_gfs_file(gf);
	_gfs_hook_debug(
		if (filedes != -1) {
			gflog_info(GFARM_MSG_UNFIXED,
			    "GFS: Hooking " S(FUNC___OPEN) " --> %d(%d)",
			    filedes, gfs_pio_fileno(gf));
		}
	);
	errno = errno_save;
	return (filedes);
}

int
FUNC__OPEN(const char *path, int oflag, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, int); /* See comment of va_arg() in FUNC___OPEN(); */
	va_end(ap);
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__OPEN)));
	return (FUNC___OPEN(path, oflag, mode));
}

int
FUNC_OPEN(const char *path, int oflag, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, oflag);
	mode = va_arg(ap, int); /* See comment of va_arg() in FUNC___OPEN(); */
	va_end(ap);
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_OPEN)));
	return (FUNC___OPEN(path, oflag, mode));
}

/*
 *  creat
 */

int
FUNC___CREAT(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___CREAT)));
	return (FUNC___OPEN(path, O_CREAT|O_TRUNC|O_WRONLY, mode));
}

int
FUNC__CREAT(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__CREAT)));
	return (FUNC___CREAT(path, mode));
}

int
FUNC_CREAT(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_CREAT)));
	return (FUNC___CREAT(path, mode));
}

int
FUNC__LIBC_CREAT(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__LIBC_CREAT)));
	return (FUNC___CREAT(path, mode));
}

/*
 * lseek
 */

OFF_T
FUNC___LSEEK(int filedes, OFF_T offset, int whence)
{
	GFS_File gf;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	const char *e;
	file_offset_t o;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___LSEEK) "(%d, %" PR_FILE_OFFSET ", %d)",
	    filedes, (file_offset_t)offset, whence));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_LSEEK(filedes, offset, whence));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		"GFS: Hooking " S(FUNC___LSEEK)
		   "(%d, %" PR_FILE_OFFSET ", %d)",
		filedes, (file_offset_t)offset, whence));

		dir = (GFS_Dir)gf;
		switch (whence) {
		case SEEK_SET:
			o = offset;
			break;
		case SEEK_END:
			while ((e = gfs_readdir(dir, &entry)) == NULL &&
			    entry != NULL)
				;
			/*FALLTHROUGH*/
		case SEEK_CUR:
			e = gfs_telldir(dir, &o);
			if (e != NULL) {
				goto error;
			}
			o += offset;
			break;
		}
		e = gfs_seekdir(dir, o);
		if (e == NULL) {
			e = gfs_telldir(dir, &o);
			if (e == NULL)
				return ((OFF_T)o);
		}
		goto error;
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
	    S(FUNC___LSEEK) "(%d(%d), %" PR_FILE_OFFSET ", %d)",
	    filedes, gfs_pio_fileno(gf), (file_offset_t)offset, whence));

	e = gfs_pio_seek(gf, offset, whence, &o);
	if (e == NULL) {
		errno = errno_save;
		return ((OFF_T)o);
	}
error:

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___LSEEK) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

OFF_T
FUNC__LSEEK(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__LSEEK) ": %d",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
FUNC_LSEEK(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_LSEEK) ": %d",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

/*
 * pread
 */

#ifdef FUNC___PREAD
ssize_t
FUNC___PREAD(int filedes, void *buf, size_t nbyte, OFF_T offset)
{
	GFS_File gf;
	const char *e;
	file_offset_t o;
	int n, errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___PREAD) "(%d, ,%lu, %" PR_FILE_OFFSET ")",
	    filedes, (unsigned long)nbyte, (file_offset_t)offset));

	if ((gf = gfs_hook_is_open(filedes)) == NULL) 
		return (SYSCALL_PREAD(filedes, buf, nbyte, offset));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
	    S(FUNC___PREAD) "(%d(%d), ,%lu, %" PR_FILE_OFFSET ")",
	    filedes, gfs_pio_fileno(gf),
				(unsigned long)nbyte, (file_offset_t)offset));

	e = gfs_pio_seek(gf, 0, SEEK_CUR, &o);
	if (e != NULL)
		goto error;

	e = gfs_pio_seek(gf, offset, SEEK_SET, NULL);
	if (e != NULL)
		goto error;

	e = gfs_pio_read(gf, buf, nbyte, &n);
	if (e != NULL)
		goto error;

	e = gfs_pio_seek(gf, o, SEEK_SET, NULL);
	if (e != NULL)
		goto error;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___PREAD) " --> %d", n));
	errno = errno_save;
	return (n);
error:

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___PREAD) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
FUNC__PREAD(int filedes, void *buf, size_t nbyte, OFF_T offset)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__PREAD) ": %d",
	    filedes));
	return (FUNC___PREAD(filedes, buf, nbyte, offset));
}

ssize_t
FUNC_PREAD(int filedes, void *buf, size_t nbyte, OFF_T offset)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_PREAD) ": %d",
	    filedes));
	return (FUNC___PREAD(filedes, buf, nbyte, offset));
}
#endif

/*
 * pwrite
 */

#ifdef FUNC___PWRITE
ssize_t
FUNC___PWRITE(int filedes, const void *buf, size_t nbyte, OFF_T offset)
{
	GFS_File gf;
	const char *e;
	file_offset_t o;
	int n, errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___PWRITE) "(%d, ,%lu, %" PR_FILE_OFFSET ")",
	    filedes, (unsigned long)nbyte, (file_offset_t)offset));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_PWRITE(filedes, buf, nbyte, offset));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
	    S(FUNC___PWRITE) "(%d(%d), ,%lu, %" PR_FILE_OFFSET ")",
	    filedes, gfs_pio_fileno(gf),
				(unsigned long)nbyte, (file_offset_t)offset));

	e = gfs_pio_seek(gf, 0, SEEK_CUR, &o);
	if (e != NULL)
		goto error;

	e = gfs_pio_seek(gf, offset, SEEK_SET, NULL);
	if (e != NULL)
		goto error;

	e = gfs_pio_write(gf, buf, nbyte, &n);
	if (e != NULL)
		goto error;

	e = gfs_pio_seek(gf, o, SEEK_SET, NULL);
	if (e != NULL)
		goto error;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___PWRITE) " --> %d", n));
	errno = errno_save;
	return (n);
error:

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	    "GFS: " S(FUNC___PWRITE) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
FUNC__PWRITE(int filedes, const void *buf, size_t nbyte, OFF_T offset)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__PWRITE) ": %d",
	    filedes));
	return (FUNC___PWRITE(filedes, buf, nbyte, offset));
}

ssize_t
FUNC_PWRITE(int filedes, const void *buf, size_t nbyte, OFF_T offset)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_PWRITE) ": %d",
	    filedes));
	return (FUNC___PWRITE(filedes, buf, nbyte, offset));
}
#endif

/*
 * getdents
 */

#if defined(__linux__) && defined(__i386__)
# define internal_function __attribute__ ((regparm (3), stdcall))
#else
# define internal_function /* empty */
#endif

int internal_function
#ifdef HOOK_GETDIRENTRIES
FUNC___GETDENTS(int filedes, STRUCT_DIRENT *buf, int nbyte, long *offp)
#else
FUNC___GETDENTS(int filedes, STRUCT_DIRENT *buf, size_t nbyte)
#endif
{
	GFS_Dir dir;
	const char *e;
	unsigned short reclen;
	struct gfs_dirent *entry;
	STRUCT_DIRENT *bp;
	file_offset_t offset;
#ifdef HOOK_GETDIRENTRIES
	int at_first = 1;
#endif
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___GETDENTS) "(%d, %p, %lu)",
	    filedes, buf, (unsigned long)nbyte));

	if ((dir = gfs_hook_is_open(filedes)) == NULL)
#ifdef HOOK_GETDIRENTRIES
		return (SYSCALL_GETDENTS(filedes, (char *)buf, nbyte, offp));
#else
		return (SYSCALL_GETDENTS(filedes, buf, nbyte));
#endif

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
	    S(FUNC___GETDENTS) "(%d, %p, %lu)",
	    filedes, buf, (unsigned long)nbyte));

	if (gfs_hook_gfs_file_type(filedes) != GFS_DT_DIR) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		goto error;
	}

	bp = buf;
	if ((entry = gfs_hook_get_suspended_gfs_dirent(filedes, &offset))
	    != NULL) {
		reclen = ALIGN(
			offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen + 1);
		if ((char *)bp + reclen > (char *)buf + nbyte) {
			e = GFARM_ERR_INVALID_ARGUMENT;
			goto finish;
		}
		gfs_hook_set_suspended_gfs_dirent(filedes, NULL, 0);
		memset(bp, 0, offsetof(STRUCT_DIRENT, d_name)); /* XXX */
		bp->d_ino = entry->d_fileno;

		/* XXX - as readdir()'s retrun value to user level nfsd */
#ifdef HOOK_GETDIRENTRIES
		at_first = 0;
		if (offp != NULL)
			*offp = offset;
#endif
#ifdef HAVE_D_OFF
		bp->d_off = offset;
#endif
#ifdef HAVE_D_NAMLEN
		bp->d_namlen = entry->d_namlen;
#endif
#ifdef HAVE_D_TYPE
		bp->d_type =
		    entry->d_type == GFS_DT_DIR ? DT_DIR :
		    entry->d_type == GFS_DT_REG ? DT_REG : DT_UNKNOWN;
#endif
		bp->d_reclen = reclen;
		memcpy(bp->d_name, entry->d_name, entry->d_namlen);
		memset(bp->d_name + entry->d_namlen, 0,
		 reclen - (offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen));
		bp = (STRUCT_DIRENT *) ((char *)bp + reclen);
	}
	
	for (gfs_telldir(dir, &offset);
	    (e = gfs_readdir(dir, &entry)) == NULL && entry != NULL;
	    gfs_telldir(dir, &offset)) {
		reclen = ALIGN(
			offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen + 1);
		if ((char *)bp + reclen > (char *)buf + nbyte) {
			gfs_hook_set_suspended_gfs_dirent(filedes,
			    entry, offset);
			goto finish;
		}
		memset(bp, 0, offsetof(STRUCT_DIRENT, d_name)); /* XXX */
		bp->d_ino = entry->d_fileno;

		/* XXX - as readdir()'s retrun value to user level nfsd */
#ifdef HOOK_GETDIRENTRIES
		if (at_first) {
			at_first = 0;
			if (offp != NULL)
				*offp = offset;
		}
#endif
#ifdef HAVE_D_OFF
		bp->d_off = offset;
#endif
#ifdef HAVE_D_NAMLEN
		bp->d_namlen = entry->d_namlen;
#endif
#ifdef HAVE_D_TYPE
		bp->d_type =
		    entry->d_type == GFS_DT_DIR ? DT_DIR :
		    entry->d_type == GFS_DT_REG ? DT_REG : DT_UNKNOWN;
#endif
		bp->d_reclen = reclen;
		memcpy(bp->d_name, entry->d_name, entry->d_namlen);
		memset(bp->d_name + entry->d_namlen, 0,
		 reclen - (offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen));
		bp = (STRUCT_DIRENT *) ((char *)bp + reclen);
	}
finish:
	if (e == NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___GETDENTS) " --> %d", filedes));
		errno = errno_save;
		return ((char *)bp - (char *)buf);
	}
error:

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS: " S(FUNC___GETDENTS) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

#ifdef HOOK_GETDIRENTRIES

int
FUNC__GETDENTS(int filedes, STRUCT_DIRENT *buf, int nbyte, long *offp)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
				  "Hooking " S(FUNC__GETDENTS) ": %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, buf, nbyte, offp));
}

int
FUNC_GETDENTS(int filedes, char *buf, int nbyte, long *offp)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
				  "Hooking " S(FUNC_GETDENTS) ": %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte, offp));
}

#else /* !defined(HOOK_GETDIRENTRIES) */

int internal_function
FUNC__GETDENTS(int filedes, STRUCT_DIRENT *buf, size_t nbyte)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
				  "Hooking " S(FUNC__GETDENTS) ": %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, buf, nbyte));
}

int internal_function
#ifdef GETDENTS_CHAR_P
FUNC_GETDENTS(int filedes, char *buf, size_t nbyte)
#else
FUNC_GETDENTS(int filedes, STRUCT_DIRENT *buf, size_t nbyte)
#endif
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
				  "Hooking " S(FUNC_GETDENTS) ": %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte));
}

#endif /* !defined(HOOK_GETDIRENTRIES) */

/*
 * truncate
 */

#ifdef FUNC___TRUNCATE
int
FUNC___TRUNCATE(const char *path, OFF_T length)
{
	GFS_File gf;
	const char *e;
	char *url;
	int errno_save = errno;
	struct gfs_stat gs;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___TRUNCATE) "(%s, %" PR_FILE_OFFSET ")",
	    path, (file_offset_t)length));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_TRUNCATE(path, length));

	e = gfs_stat(url, &gs);
	if (e != NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		    "GFS: Hooking " S(FUNC___TRUNCATE) ": gfs_stat: %s", e));
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	if (GFARM_S_ISDIR(gs.st_mode)) {
		e = GFARM_ERR_IS_A_DIRECTORY;
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
			S(FUNC___FTRUNCATE) "(%s, %" PR_FILE_OFFSET"): %s",
			path, (file_offset_t)length ,e));
		free(url);
		gfs_stat_free(&gs);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	gfs_stat_free(&gs);
	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
	   "GFS: Hooking " S(FUNC___TRUNCATE) "(%s, %" PR_FILE_OFFSET ")",
	    path, (file_offset_t)length));

	e = gfs_pio_open(url, GFARM_FILE_RDWR, &gf);
	if (e != NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
		"GFS: Hooking " S(FUNC___TRUNCATE) ": gfs_pio_open: %s", e));
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	free(url);
	e = gfs_pio_truncate(gf, length);
	if (e != NULL) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
		 S(FUNC___TRUNCATE) ": gfs_pio_truncate: %s", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	gfs_pio_close(gf);
	errno = errno_save;
	return (0);
}

int
FUNC__TRUNCATE(const char *path, OFF_T length)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__TRUNCATE)));
	return (FUNC___TRUNCATE(path, length));
}

int
FUNC_TRUNCATE(const char *path, OFF_T length)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_TRUNCATE)));
	return (FUNC___TRUNCATE(path, length));
}
#endif

/*
 * ftruncate
 */

#ifdef FUNC___FTRUNCATE
int
FUNC___FTRUNCATE(int filedes, OFF_T length)
{
	GFS_File gf;
	const char *e;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC___FTRUNCATE) "(%d, %" PR_FILE_OFFSET")",
	    filedes, (file_offset_t)length));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FTRUNCATE(filedes, length));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
			S(FUNC___FTRUNCATE) "(%d, %" PR_FILE_OFFSET")",
		 filedes, (file_offset_t)length));

		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED, "GFS: Hooking "
	    S(FUNC___FTRUNCATE) "(%d(%d), %" PR_FILE_OFFSET ")",
	    filedes, gfs_pio_fileno(gf), (file_offset_t)length));

	e = gfs_pio_truncate(gf, length);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}
error:
	_gfs_hook_debug(gflog_info(GFARM_MSG_UNFIXED,
				"GFS:" S(FUNC___FTRUNCATE) ": %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__FTRUNCATE(int filedes, OFF_T length)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC__FTRUNCATE)));
	return (FUNC___FTRUNCATE(filedes, length));
}

int
FUNC_FTRUNCATE(int filedes, OFF_T length)
{
	_gfs_hook_debug_v(gflog_info(GFARM_MSG_UNFIXED,
	    "Hooking " S(FUNC_FTRUNCATE)));
	return (FUNC___FTRUNCATE(filedes, length));
}
#endif

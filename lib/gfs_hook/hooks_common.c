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
	int is_directory, path_exist, save_errno;
	int nf = -1, np;

	va_start(ap, oflag);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___OPEN) "(%s, 0%o)\n", path, oflag));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_OPEN(path, oflag, mode));

	if (gfs_hook_get_current_view() == section_view)
		e = gfs_stat_section(url, gfs_hook_get_current_section(), &gs);
	else
		e = gfs_stat(url, &gs);
	if (e == NULL) {
		path_exist = 1;
		is_directory = GFARM_S_ISDIR(gs.st_mode);
		gfs_stat_free(&gs);
	} else {
		/* XXX - metadata may be incomplete. anyway, continue. */
		path_exist = 0;
		/* XXX - metadata of a directory should not be imcomplete */
		is_directory = 0;
		if (e != GFARM_ERR_NO_SUCH_OBJECT)
			_gfs_hook_debug(fprintf(stderr,
			    "GFS: Hooking " S(FUNC___OPEN) ": gfs_stat: %s\n",
			    e));
	}

	if (is_directory) {
		GFS_Dir dir;

		_gfs_hook_debug(fprintf(stderr,
		   "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o): dir\n",
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
			save_errno = errno;
			_gfs_hook_debug(
				fprintf(stderr, "GFS: Hooking "
				    S(FUNC___OPEN) " --> %d\n", filedes);
			);
			free(url);
			if (filedes == -1)
				errno = save_errno;
			return (filedes);
		}
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	if ((oflag & O_CREAT) != 0) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o, 0%o)\n",
		    url, oflag, mode));

		oflag = gfs_hook_open_flags_gfarmize(oflag);
		e = gfs_pio_create(url, oflag, mode, &gf);
		if (e == NULL && !path_exist)
			gfs_hook_add_creating_file(gf);
	} else {
		_gfs_hook_debug(fprintf(stderr,
		   "GFS: Hooking " S(FUNC___OPEN) "(%s, 0%o)\n", url, oflag));

		oflag = gfs_hook_open_flags_gfarmize(oflag);
		e = gfs_pio_open(url, oflag, &gf);
	}
	free(url);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___OPEN) ": %s\n", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}

	/* set file view */
	switch (gfs_hook_get_current_view()) {
	case section_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_section(%s, %s)\n",
			path, gfs_hook_get_current_section()));
		e = gfs_pio_set_view_section(
			gf, gfs_hook_get_current_section(), NULL, 0);
		break;
	case index_view:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_index(%s, %d, %d)\n", path,
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
			}				
		} else if (e == GFARM_ERR_FRAGMENT_INDEX_NOT_AVAILABLE ||
		    (e == NULL && gfs_pio_get_node_size(&np) == NULL &&
		     nf == np)) {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_local(%s) @ %d/%d\n",
					path, gfarm_node, gfarm_nnode));
			e = gfs_pio_set_view_local(gf, 0);
		} else {
			_gfs_hook_debug(fprintf(stderr,
				"GFS: set_view_global(%s) @ %d/%d\n",
					path, gfarm_node, gfarm_nnode));
			e = gfs_pio_set_view_global(gf, 0);
		}
		break;
	default:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: set_view_global(%s)\n", path));
		e = gfs_pio_set_view_global(gf, 0);
	}
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr, "GFS: set_view: %s\n", e));
		gfs_hook_delete_creating_file(gf);
		gfs_pio_close(gf);
		errno = gfarm_error_to_errno(e);
		return (-1);
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
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC___CREAT) "\n", stderr));
	return (FUNC___OPEN(path, O_CREAT|O_TRUNC|O_WRONLY, mode));
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

int
FUNC__LIBC_CREAT(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC__LIBC_CREAT) "\n", stderr));
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

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking " S(FUNC___LSEEK)
		   "(%d, %" PR_FILE_OFFSET ", %d)\n",
		filedes, (file_offset_t)offset, whence));

		_gfs_hook_debug(fprintf(stderr,
		    "lseek(2) trapping of Gfarm directory "
		      "not supported yet\n"));

		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking "
	    S(FUNC___LSEEK) "(%d(%d), %" PR_FILE_OFFSET ", %d)\n",
	    filedes, gfs_pio_fileno(gf), (file_offset_t)offset, whence));

	e = gfs_pio_seek(gf, offset, whence, &o);
	if (e == NULL)
		return ((OFF_T)o);
error:

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

/*
 * getdents
 */

#if defined(__linux__) && defined(__i386__)
# define internal_function __attribute__ ((regparm (3), stdcall))
#else
# define internal_function /* empty */
#endif

int internal_function
#if defined(__FreeBSD__) || defined(__DragonFly__)
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
	int reccnt = 0;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	int at_first = 1;
#endif

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___GETDENTS) "(%d, %p, %lu)\n",
	    filedes, buf, (unsigned long)nbyte));

	if ((dir = gfs_hook_is_open(filedes)) == NULL)
#if defined(__FreeBSD__) || defined(__DragonFly__)
		return (SYSCALL_GETDENTS(filedes, (char *)buf, nbyte, offp));
#else
		return (SYSCALL_GETDENTS(filedes, buf, nbyte));
#endif

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking "
	    S(FUNC___GETDENTS) "(%d, %p, %lu)\n",
	    filedes, buf, (unsigned long)nbyte));

	if (gfs_hook_gfs_file_type(filedes) != GFS_DT_DIR) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		goto error;
	}

	bp = buf;
	if ((entry = gfs_hook_get_suspended_gfs_dirent(filedes, &reccnt))
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
#if defined(__FreeBSD__) || defined(__DragonFly__)
		at_first = 0;
		if (offp != NULL)
			*offp = GFS_DIRENTSIZE * ++reccnt;
#elif !defined(__NetBSD__) && !defined(__OpenBSD__)
		bp->d_off = GFS_DIRENTSIZE * ++reccnt;
#endif

		bp->d_reclen = reclen;
		memcpy(bp->d_name, entry->d_name, entry->d_namlen);
		memset(bp->d_name + entry->d_namlen, 0,
		 reclen - (offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen));
		bp = (STRUCT_DIRENT *) ((char *)bp + reclen);
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		reclen = ALIGN(
			offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen + 1);
		if ((char *)bp + reclen > (char *)buf + nbyte) {
			gfs_hook_set_suspended_gfs_dirent(filedes,
			    entry, reccnt);
			goto finish;
		}
		memset(bp, 0, offsetof(STRUCT_DIRENT, d_name)); /* XXX */
		bp->d_ino = entry->d_fileno;

		/* XXX - as readdir()'s retrun value to user level nfsd */
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (at_first) {
			at_first = 0;
			if (offp != NULL)
				*offp = GFS_DIRENTSIZE * ++reccnt;
		}
#elif !defined(__NetBSD__) && !defined(__OpenBSD__)
		bp->d_off = GFS_DIRENTSIZE * ++reccnt;
#endif

		bp->d_reclen = reclen;
		memcpy(bp->d_name, entry->d_name, entry->d_namlen);
		memset(bp->d_name + entry->d_namlen, 0,
		 reclen - (offsetof(STRUCT_DIRENT, d_name) + entry->d_namlen));
		bp = (STRUCT_DIRENT *) ((char *)bp + reclen);
	}
finish:
	if (e == NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___GETDENTS) " --> %d\n", filedes));
		return ((char *)bp - (char *)buf);
	}
error:

	_gfs_hook_debug(fprintf(stderr,
				"GFS: " S(FUNC___GETDENTS) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

#if defined(__FreeBSD__) || defined(__DragonFly__)

int
FUNC__GETDENTS(int filedes, STRUCT_DIRENT *buf, int nbyte, long *offp)
{
	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking " S(FUNC__GETDENTS) ": %d\n",
				  filedes));
	return (FUNC___GETDENTS(filedes, buf, nbyte, offp));
}

int
FUNC_GETDENTS(int filedes, char *buf, int nbyte, long *offp)
{
	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking " S(FUNC_GETDENTS) ": %d\n",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte, offp));
}

#else /* !defined(__FreeBSD__) && !defined(__DragonFly__) */

int internal_function
FUNC__GETDENTS(int filedes, STRUCT_DIRENT *buf, size_t nbyte)
{
	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking " S(FUNC__GETDENTS) ": %d\n",
				  filedes));
	return (FUNC___GETDENTS(filedes, buf, nbyte));
}

int internal_function
#if defined(__NetBSD__) || defined(__OpenBSD__)
FUNC_GETDENTS(int filedes, char *buf, size_t nbyte)
#else
FUNC_GETDENTS(int filedes, STRUCT_DIRENT *buf, size_t nbyte)
#endif
{
	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking " S(FUNC_GETDENTS) ": %d\n",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte));
}

#endif /* !defined(__FreeBSD__) && !defined(__DragonFly__) */

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
	struct gfs_stat gs;

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___TRUNCATE) "(%s, %" PR_FILE_OFFSET ")\n",
	    path, (file_offset_t)length));

	if (!gfs_hook_is_url(path, &url))
		return (SYSCALL_TRUNCATE(path, length));

	e = gfs_stat(url, &gs);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking " S(FUNC___TRUNCATE) ": gfs_stat: %s\n", e));
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	if (GFARM_S_ISDIR(gs.st_mode)) {
		e = GFARM_ERR_IS_A_DIRECTORY;
		_gfs_hook_debug(fprintf(stderr, "GFS: Hooking "
			S(FUNC___FTRUNCATE) "(%s, %" PR_FILE_OFFSET"): %s\n",
			path, (file_offset_t)length ,e));
		free(url);
		gfs_stat_free(&gs);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	gfs_stat_free(&gs);
	_gfs_hook_debug(fprintf(stderr,
	   "GFS: Hooking " S(FUNC___TRUNCATE) "(%s, %" PR_FILE_OFFSET ")\n",
	    path, (file_offset_t)length));

	e = gfs_pio_open(url, GFARM_FILE_RDWR, &gf);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking " S(FUNC___TRUNCATE) ": gfs_pio_open: %s\n", e));
		free(url);
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	free(url);
	e = gfs_pio_truncate(gf, length);
	if (e != NULL) {
		_gfs_hook_debug(fprintf(stderr,	"GFS: Hooking "
		 S(FUNC___TRUNCATE) ": gfs_pio_truncate: %s\n", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	gfs_pio_close(gf);
	return (0);
}

int
FUNC__TRUNCATE(const char *path, OFF_T length)
{
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC__TRUNCATE) "\n", stderr));
	return (FUNC___TRUNCATE(path, length));
}

int
FUNC_TRUNCATE(const char *path, OFF_T length)
{
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC_TRUNCATE) "\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr,
	    "Hooking " S(FUNC___FTRUNCATE) "(%d, %" PR_FILE_OFFSET")\n",
	    filedes, (file_offset_t)length));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (SYSCALL_FTRUNCATE(filedes, length));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(fprintf(stderr,	"GFS: Hooking "
			S(FUNC___FTRUNCATE) "(%d, %" PR_FILE_OFFSET")\n",
		 filedes, (file_offset_t)length));

		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking "
	    S(FUNC___FTRUNCATE) "(%d(%d), %" PR_FILE_OFFSET ")\n",
	    filedes, gfs_pio_fileno(gf), (file_offset_t)length));

	e = gfs_pio_truncate(gf, length);
	if (e == NULL)
		return (0);
error:
	_gfs_hook_debug(fprintf(stderr,
				"GFS:" S(FUNC___FTRUNCATE) ": %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
FUNC__FTRUNCATE(int filedes, OFF_T length)
{
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC__FTRUNCATE) "\n", stderr));
	return (FUNC___FTRUNCATE(filedes, length));
}

int
FUNC_FTRUNCATE(int filedes, OFF_T length)
{
	_gfs_hook_debug_v(fputs("Hooking " S(FUNC_FTRUNCATE) "\n", stderr));
	return (FUNC___FTRUNCATE(filedes, length));
}
#endif

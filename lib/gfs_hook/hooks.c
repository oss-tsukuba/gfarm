/*
 *  Hooking system calls to utilize Gfarm file system.
 *
 *  $Id$
 */

#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "hooks_subr.h"

#include <sys/syscall.h>
#if defined(__osf__) && defined(__alpha) /* Tru64 */
#define SYS_creat SYS_old_creat
#endif
#ifdef __NetBSD__
#define SYS_creat SYS_compat_43_ocreat
#define SYS_stat SYS___stat13
#endif

/*
 *  XXX - quite naive implementation
 *
 *  It is necessary to re-implement more cleverly.
 */

/*
 * read
 */

ssize_t
__read(int filedes, void *buf, size_t nbyte)
{
	GFS_File gf;
	char *e;
	int n;

	_gfs_hook_debug(fprintf(stderr, "Hooking __read(%d, , %d)\n",
	    filedes, nbyte));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return syscall(SYS_read, filedes, buf, nbyte);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __read(%d(%d), , %d)\n",
	    filedes, gfs_pio_fileno(gf), nbyte));

	e = gfs_pio_read(gf, buf, nbyte, &n);
	if (e == NULL)
		return (n);

	_gfs_hook_debug(fprintf(stderr, "GFS: __read: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
_read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug(fputs("Hooking _read\n", stderr));
	return (__read(filedes, buf, nbyte));
}

ssize_t
read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug(fputs("Hooking read\n", stderr));
	return (__read(filedes, buf, nbyte));
}

/*
 * write
 */

/* fprintf and fputs should not be put into the following function. */
ssize_t
__write(int filedes, const void *buf, size_t nbyte)
{
	GFS_File gf;
	char *e;
	int n;

	_gfs_hook_debug(fprintf(stderr, "Hooking __write(%d, , %d)\n",
	    filedes, nbyte));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_write, filedes, buf, nbyte));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __write(%d(%d), , %d)\n",
	    filedes, gfs_pio_fileno(gf), nbyte));

	e = gfs_pio_write(gf, buf, nbyte, &n);
	if (e == NULL)
		return (n);

	_gfs_hook_debug(fprintf(stderr, "GFS: __write: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
_write(int filedes, const void *buf, size_t nbyte)
{
	return (__write(filedes, buf, nbyte));
}

ssize_t
write(int filedes, const void *buf, size_t nbyte)
{
	return (__write(filedes, buf, nbyte));
}

/*
 *  close
 */

int
__syscall_close(int filedes)
{
	return (syscall(SYS_close, filedes));
}

int
__close(int filedes)
{
	GFS_File gf;
	char *e;

	_gfs_hook_debug(fprintf(stderr, "Hooking __close(%d)\n", filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (__syscall_close(filedes));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __close(%d(%d))\n",
	    filedes, gfs_pio_fileno(gf)));

	gfs_hook_clear_gfs_file(filedes);
	e = gfs_pio_close(gf);
	if (e == NULL)
		return (0);
	_gfs_hook_debug(fprintf(stderr, "GFS: __close: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_close(int filedes)
{
	_gfs_hook_debug(fputs("Hooking _close\n", stderr));
	return (__close(filedes));
}

int
close(int filedes)
{
	_gfs_hook_debug(fputs("Hooking __close\n", stderr));
	return (__close(filedes));
}

/*
 * unlink
 */

int
__unlink(const char *path)
{
	const char *e;
	char *url, *sec;

	_gfs_hook_debug(fprintf(stderr, "Hooking __unlink(%s)\n", path));

	if (!gfs_hook_is_url(path, &url, &sec))
		return syscall(SYS_unlink, path);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __unlink(%s)\n", path));
	e = gfs_unlink(url);
	if (sec != NULL) {
	    free(url);
	    free(sec);
	}
	if (e == NULL)
	    return (0);
	_gfs_hook_debug(fprintf(stderr, "GFS: __unlink: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * definitions for "hooks_common.c"
 */

int
gfs_hook_syscall_open(const char *path, int oflag, mode_t mode)
{
	return (syscall(SYS_open, path, oflag, mode));
}

int
gfs_hook_syscall_creat(const char *path, mode_t mode)
{
	return (syscall(SYS_creat, path, mode));
}

int
gfs_hook_syscall_lseek(int filedes, off_t offset, int whence)
{
#if defined(__NetBSD__)
	return (__syscall((quad_t)SYS_lseek, filedes, 0, offset, whence));
#else
	return (syscall(SYS_lseek, filedes, offset, whence));
#endif
}

int
gfs_hook_syscall_stat(const char *path, struct stat *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_stat, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_xstat(_STAT_VER, path, buf));
#endif
}

/*
 * for SVR4.
 *
 * (see sysdep/linux/xstat.c about Linux)
 */
#if defined(_STAT_VER) && defined(SYS_xstat)
int
gfs_hook_syscall_xstat(int ver, const char *path, struct stat *buf)
{
	return (syscall(SYS_xstat, ver, path, buf));

}
#endif

#define OFF_T off_t

#define SYSCALL_OPEN(path, oflag, mode)	\
	gfs_hook_syscall_open(path, oflag, mode)
#define SYSCALL_CREAT(path, mode)	\
	gfs_hook_syscall_creat(path, mode)
#define SYSCALL_LSEEK(filedes, offset, whence)	\
	gfs_hook_syscall_lseek(filedes, offset, whence)
#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_stat(path, buf)

#define FUNC___OPEN	__open
#define FUNC__OPEN	_open
#define FUNC_OPEN	open
#define FUNC___CREAT	__creat
#define FUNC__CREAT	_creat
#define FUNC_CREAT	creat
#define FUNC___LSEEK	__lseek
#define FUNC__LSEEK	_lseek
#define FUNC_LSEEK	lseek
#define FUNC___STAT	__stat
#define FUNC__STAT	_stat
#define FUNC_STAT	stat
#define STRUCT_STAT	struct stat

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_xstat(ver, path, buf)
#define FUNC___XSTAT	__xstat
#define FUNC__XSTAT	_xstat
#define FUNC_XSTAT	xstat
#endif

#include "hooks_common.c"

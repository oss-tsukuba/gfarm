#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <errno.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "hooks_subr.h"

#include <sys/syscall.h>

/*
 * The following hooks are not implemented:
 *
 * _LFS64_ASYNCHRONOUS_IO APIs
 *	aio_cancel64()
 *	aio_error64()
 *	aio_fsync64()
 *	aio_read64()
 *	aio_return64()
 *	aio_suspend64()
 *	aio_write64()
 *	lio_listio64()
 *
 * _LFS64_LARGEFILE APIs
 *	ftruncate64()
 *	lockf64()
 *	mmap64()
 *	readdir64()
 *	truncate64()
 *
 */

/*
 * definitions for "hooks_common.c"
 */

int
gfs_hook_syscall_open64(const char *path, int oflag, mode_t mode)
{
#ifdef SYS_open64
	return (syscall(SYS_open64, path, oflag, mode));
#else
	return (gfs_hook_syscall_open(path, oflag | O_LARGEFILE, mode));
#endif
}

int
gfs_hook_syscall_creat64(const char *path, mode_t mode)
{
#ifdef SYS_creat64
	return (syscall(SYS_creat64, path, mode));
#else
	return (gfs_hook_syscall_open64(path, O_CREAT|O_TRUNC|O_WRONLY, mode));
#endif
}

#ifndef __linux__
int
gfs_hook_syscall_getdents64(int filedes, struct dirent64 *buf, size_t nbyte)
{
# ifdef SYS_getdents64
	return (syscall(SYS_getdents64, filedes, buf, nbyte));
# else
	return (gfs_hook_syscall_getdents(filedes, buf, nbyte));
# endif
}
#endif

/* see lseek64.c for gfs_hook_syscall_lseek64() implementation */

int
gfs_hook_syscall_stat64(const char *path, struct stat64 *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_stat64, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_xstat64(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_lstat64(const char *path, struct stat64 *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_lstat64, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_lxstat64(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_fstat64(int filedes, struct stat64 *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_fstat64, filedes, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_fxstat64(_STAT_VER, filedes, buf));
#endif
}

/*
 * for SVR4.
 *
 * (see sysdep/linux/xstat64.c about Linux)
 */
#if defined(_STAT_VER) && defined(SYS_xstat)
int
gfs_hook_syscall_xstat64(int ver, const char *path, struct stat64 *buf)
{
#if defined(__sgi) && _MIPS_SIM == _ABIN32 /* ABI N32 */
	return (gfs_hook_syscall_xstat(SYS_xstat, _STAT_VER, path, buf));
#else
	return (gfs_hook_syscall_xstat(SYS_xstat, _STAT64_VER, path, buf));
#endif
}

int
gfs_hook_syscall_lxstat64(int ver, const char *path, struct stat64 *buf)
{
#if defined(__sgi) && _MIPS_SIM == _ABIN32 /* ABI N32 */
	return (gfs_hook_syscall_lxstat(SYS_lxstat, _STAT_VER, path, buf));
#else
	return (gfs_hook_syscall_lxstat(SYS_lxstat, _STAT64_VER, path, buf));
#endif
}

int
gfs_hook_syscall_fxstat64(int ver, int filedes, struct stat64 *buf)
{
#if defined(__sgi) && _MIPS_SIM == _ABIN32 /* ABI N32 */
	return (gfs_hook_syscall_fxstat(SYS_fxstat, _STAT_VER, filedes, buf));
#else
	return (gfs_hook_syscall_fxstat(SYS_fxstat, _STAT64_VER, filedes, buf));
#endif
}
#endif

#define OFF_T off64_t

#define SYSCALL_OPEN(path, oflag, mode)	\
	gfs_hook_syscall_open64(path, oflag, mode)
#define SYSCALL_CREAT(path, mode)	\
	gfs_hook_syscall_creat64(path, mode)
#define SYSCALL_LSEEK(filedes, offset, whence)	\
	gfs_hook_syscall_lseek64(filedes, offset, whence)
#define SYSCALL_GETDENTS(filedes, buf, nbyte) \
	gfs_hook_syscall_getdents64(filedes, buf, nbyte)

#define FUNC___OPEN	__open64
#define FUNC__OPEN	_open64
#define FUNC_OPEN	open64
#define FUNC___CREAT	__creat64
#define FUNC__CREAT	_creat64
#define FUNC_CREAT	creat64
#define FUNC__LIBC_CREAT	_libc_creat64
#define FUNC___LSEEK	__lseek64
#define FUNC__LSEEK	_lseek64
#define FUNC_LSEEK	lseek64
#define FUNC___GETDENTS	__getdents64
#define FUNC__GETDENTS	_getdents64
#define FUNC_GETDENTS	getdents64

#define STRUCT_DIRENT	struct dirent64
#define ALIGNMENT 8
#define ALIGN(p) (((unsigned long)(p) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

#include "hooks_common.c"

/* stat */

#define STRUCT_STAT	struct stat64
#define GFS_BLKSIZE	8192

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_stat64(path, buf)
#define FUNC___STAT	__stat64
#define FUNC__STAT	_stat64
#define FUNC_STAT	stat64
#define GFS_STAT	gfs_stat
#define GFS_STAT_SECTION gfs_stat_section
#define GFS_STAT_INDEX	gfs_stat_index

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_xstat64(ver, path, buf)
#define FUNC___XSTAT	__xstat64
#define FUNC__XSTAT	_xstat64
#define FUNC_XSTAT	xstat64
#endif

#include "hooks_stat.c"

#undef SYSCALL_STAT
#undef FUNC___STAT
#undef FUNC__STAT
#undef FUNC_STAT
#undef GFS_STAT
#ifdef _STAT_VER /* SVR4 or Linux */
#undef SYSCALL_XSTAT
#undef FUNC___XSTAT
#undef FUNC__XSTAT
#undef FUNC_XSTAT
#endif

/* lstat */

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_lstat64(path, buf)
#define FUNC___STAT	__lstat64
#define FUNC__STAT	_lstat64
#define FUNC_STAT	lstat64
#define GFS_STAT	gfs_stat /* XXX - gfs_lstat in not implemented yet */

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_lxstat64(ver, path, buf)
#define FUNC___XSTAT	__lxstat64
#define FUNC__XSTAT	_lxstat64
#define FUNC_XSTAT	lxstat64
#endif

#include "hooks_stat.c"

/* fstat */

#define SYSCALL_FSTAT(path, buf)	\
	gfs_hook_syscall_fstat64(path, buf)
#define FUNC___FSTAT	__fstat64
#define FUNC__FSTAT	_fstat64
#define FUNC_FSTAT	fstat64
#define GFS_FSTAT	gfs_fstat

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_FXSTAT(ver, fd, buf)	\
	gfs_hook_syscall_fxstat64(ver, fd, buf)
#define FUNC___FXSTAT	__fxstat64
#define FUNC__FXSTAT	_fxstat64
#define FUNC_FXSTAT	fxstat64
#endif

#include "hooks_fstat.c"

#if defined(SYS_llseek) || defined(SYS__llseek)
/*
 * llseek
 */

OFF_T
__llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking " "__llseek" ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
_llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking " "_llseek" ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking " "llseek" ": %d\n",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}
#endif /* defined(SYS_llseek) || defined(SYS__llseek) */

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
 *	fstat64()
 *	ftruncate64()
 *	lockf64()
 *	lstat64()
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

/*
 * XXX - not really tested.
 */
int
gfs_hook_syscall_lseek64(int filedes, off64_t offset, int whence)
{
#if defined(SYS_lseek64)
	return (syscall(SYS_lseek64, filedes, offset, whence));
#elif defined(SYS_llseek)
	return (syscall(SYS_llseek, filedes, (int)(offset >> 32), (int)offset,
			whence));
#elif defined(SYS__llseek) /* linux */
	off64_t rv, result;

	rv = syscall(SYS__llseek, filedes, (int)(offset >>32), (int)offset,
	    &result, whence);
	return (rv ? rv : result);
#else
#error do not know how to implement lseek64
#endif
}

int
gfs_hook_syscall_stat64(const char *path, struct stat64 *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_stat64, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_xstat64(_STAT_VER, path, buf));
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
#endif

#define OFF_T off64_t

#define SYSCALL_OPEN(path, oflag, mode)	\
	gfs_hook_syscall_open64(path, oflag, mode)
#define SYSCALL_CREAT(path, mode)	\
	gfs_hook_syscall_creat64(path, mode)
#define SYSCALL_LSEEK(filedes, offset, whence)	\
	gfs_hook_syscall_lseek64(filedes, offset, whence)
#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_stat64(path, buf)

#define FUNC___OPEN	__open64
#define FUNC__OPEN	_open64
#define FUNC_OPEN	open64
#define FUNC___CREAT	__creat64
#define FUNC__CREAT	_creat64
#define FUNC_CREAT	creat64
#define FUNC___LSEEK	__lseek64
#define FUNC__LSEEK	_lseek64
#define FUNC_LSEEK	lseek64
#define FUNC___STAT	__stat64
#define FUNC__STAT	_stat64
#define FUNC_STAT	stat64
#define STRUCT_STAT	struct stat64

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_xstat64(ver, path, buf)
#define FUNC___XSTAT	__xstat64
#define FUNC__XSTAT	_xstat64
#define FUNC_XSTAT	xstat64
#endif

#include "hooks_common.c"

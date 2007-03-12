#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <errno.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfutil.h"
#include "hooks_subr.h"

#include <sys/syscall.h>

#if defined(sun) && (defined(__svr4__) || defined(__SVR4))
#define OS_SOLARIS	1
#endif

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

#ifdef SYS_truncate64
int
gfs_hook_syscall_truncate64(const char *path, off64_t length)
{
	return (syscall(SYS_truncate64, path, length));
}

#define SYSCALL_TRUNCATE(path, length) \
	gfs_hook_syscall_truncate64(path, length)
#define FUNC___TRUNCATE	__truncate64
#define FUNC__TRUNCATE	_truncate64
#define FUNC_TRUNCATE	truncate64
#endif

#ifdef SYS_ftruncate64
int
gfs_hook_syscall_ftruncate64(int filedes, off64_t length)
{
	return (syscall(SYS_ftruncate64, filedes, length));
}

#define SYSCALL_FTRUNCATE(filedes, length) \
	gfs_hook_syscall_ftruncate64(filedes, length)
#define FUNC___FTRUNCATE	__ftruncate64
#define FUNC__FTRUNCATE		_ftruncate64
#define FUNC_FTRUNCATE		ftruncate64
#endif

/* see lseek64.c for gfs_hook_syscall_lseek64() implementation */

#if defined(SYS_pread) || defined(SYS_pread64)
int
gfs_hook_syscall_pread64(int filedes, void *buf, size_t nbyte, off64_t offset)
{
# ifdef SYS_pread64
	return (syscall(SYS_pread64, filedes, buf, nbyte, offset));
# else
	return (syscall(SYS_pread, filedes, buf, nbyte, offset));
# endif
}

#define SYSCALL_PREAD(filedes, buf, nbyte, offset)	\
	gfs_hook_syscall_pread64(filedes, buf, nbyte, offset)
#define FUNC___PREAD	__pread64
#define FUNC__PREAD	_pread64
#define FUNC_PREAD	pread64
#endif

#if defined(SYS_pwrite) || defined(SYS_pwrite64)
int
gfs_hook_syscall_pwrite64(int filedes, const void *buf, size_t nbyte, off64_t offset)
{
# ifdef SYS_pwrite64
	return (syscall(SYS_pwrite64, filedes, buf, nbyte, offset));
# else
	return (syscall(SYS_pwrite, filedes, buf, nbyte, offset));
# endif

#define SYSCALL_PWRITE(filedes, buf, nbyte, offset)	\
	gfs_hook_syscall_pwrite64(filedes, buf, nbyte, offset)
#define FUNC___PWRITE	__pwrite64
#define FUNC__PWRITE	_pwrite64
#define FUNC_PWRITE	pwrite64
}
#endif

#if defined(OS_SOLARIS)
	/*
	 * do not need xstat64 on Solaris 2.
	 *
	 * on sparc, _STAT_VER is not defined.
	 * on i386, _STAT_VER is defined, but only used for 32bit interface.
	 */
#elif defined(_STAT_VER) /* Linux or SVR4 except Solaris */
#	define NEED_XSTAT64
#endif	

int
gfs_hook_syscall_stat64(const char *path, struct stat64 *buf)
{
#ifndef NEED_XSTAT64
	return (syscall(SYS_stat64, path, buf));
#else /* Linux or SVR4, but not Solaris 2 */
	return (gfs_hook_syscall_xstat64(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_lstat64(const char *path, struct stat64 *buf)
{
#ifndef NEED_XSTAT64
	return (syscall(SYS_lstat64, path, buf));
#else /* Linux or SVR4, but not Solaris 2 */
	return (gfs_hook_syscall_lxstat64(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_fstat64(int filedes, struct stat64 *buf)
{
#ifndef NEED_XSTAT64
	return (syscall(SYS_fstat64, filedes, buf));
#else /* Linux or SVR4, but not Solaris 2 */
	return (gfs_hook_syscall_fxstat64(_STAT_VER, filedes, buf));
#endif
}

/*
 * for SVR4 except Solaris 2
 *
 * (see gfs_hook/sysdep/linux/xstat64.c about Linux)
 */
#if defined(NEED_XSTAT64) && defined(SYS_xstat)
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
#endif /* defined(NEED_XSTAT64) && defined(SYS_xstat) */

#define OFF_T off64_t

#define SYSCALL_OPEN(path, oflag, mode)	\
	gfs_hook_syscall_open64(path, oflag, mode)
#define SYSCALL_CREAT(path, mode)	\
	gfs_hook_syscall_creat64(path, mode)
#define SYSCALL_LSEEK(filedes, offset, whence)	\
	gfs_hook_syscall_lseek64(filedes, offset, whence)

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


#define SYSCALL_GETDENTS(filedes, buf, nbyte) \
	gfs_hook_syscall_getdents64(filedes, buf, nbyte)
#define FUNC___GETDENTS	__getdents64
#define FUNC__GETDENTS	_getdents64
#define FUNC_GETDENTS	getdents64

#define STRUCT_DIRENT	struct dirent64
#define ALIGNMENT 8
#define ALIGN(p) (((unsigned long)(p) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))


#include "hooks_common.c"

/* stat */

#define STRUCT_STAT	struct stat64

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_stat64(path, buf)
#define FUNC___STAT	__stat64
#define FUNC__STAT	_stat64
#define FUNC_STAT	stat64
#define GFS_STAT	gfs_stat
#define GFS_STAT_SECTION gfs_stat_section
#define GFS_STAT_INDEX	gfs_stat_index

#ifdef NEED_XSTAT64 /* SVR4 or Linux, but not Solaris 2 */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_xstat64(ver, path, buf)
#define FUNC___XSTAT	__xstat64
#define FUNC__XSTAT	_xstat64
#endif

#include "hooks_stat.c"

#undef SYSCALL_STAT
#undef FUNC___STAT
#undef FUNC__STAT
#undef FUNC_STAT
#undef GFS_STAT
#ifdef NEED_XSTAT64 /* SVR4 or Linux, but not Solaris 2 */
#undef SYSCALL_XSTAT
#undef FUNC___XSTAT
#undef FUNC__XSTAT
#endif

/* lstat */

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_lstat64(path, buf)
#define FUNC___STAT	__lstat64
#define FUNC__STAT	_lstat64
#define FUNC_STAT	lstat64
#define GFS_STAT	gfs_stat /* XXX - gfs_lstat in not implemented yet */

#ifdef NEED_XSTAT64 /* SVR4 or Linux, but not Solaris 2 */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_lxstat64(ver, path, buf)
#define FUNC___XSTAT	__lxstat64
#define FUNC__XSTAT	_lxstat64
#endif

#include "hooks_stat.c"

/* fstat */

#define SYSCALL_FSTAT(path, buf)	\
	gfs_hook_syscall_fstat64(path, buf)
#define FUNC___FSTAT	__fstat64
#define FUNC__FSTAT	_fstat64
#define FUNC_FSTAT	fstat64
#define GFS_FSTAT	gfs_fstat

#ifdef NEED_XSTAT64 /* SVR4 or Linux, but not Solaris 2 */
#define SYSCALL_FXSTAT(ver, fd, buf)	\
	gfs_hook_syscall_fxstat64(ver, fd, buf)
#define FUNC___FXSTAT	__fxstat64
#define FUNC__FXSTAT	_fxstat64
#endif

#include "hooks_fstat.c"

#if defined(SYS_llseek) || defined(SYS__llseek)
/*
 * llseek
 */

OFF_T
__llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(gflog_info("Hooking " "__llseek" ": %d",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
_llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(gflog_info("Hooking " "_llseek" ": %d",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}

OFF_T
llseek(int filedes, OFF_T offset, int whence)
{
	_gfs_hook_debug_v(gflog_info("Hooking " "llseek" ": %d",
	    filedes));
	return (FUNC___LSEEK(filedes, offset, whence));
}
#endif /* defined(SYS_llseek) || defined(SYS__llseek) */

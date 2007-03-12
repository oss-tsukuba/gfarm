/*
 * Hooking system calls to utilize Gfarm file system.
 *
 * $Id$
 */

#ifdef __osf__
/* argument types of mknod() and utimes() are different without this */
#define _XOPEN_SOURCE_EXTENDED
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include <errno.h>

#if !defined(WCOREDUMP) && defined(_AIX)
#define WCOREDUMP(status)	((status) & 0x80)
#endif

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfs_misc.h"

#include "hooks_subr.h"

#include <sys/syscall.h>
#ifdef SYS_fdsync /* Solaris */
#include <sys/file.h>		/* FSYNC, FDSYNC */
#endif

#if defined(sun) && (defined(__svr4__) || defined(__SVR4))
#define OS_SOLARIS	1
#endif

#ifdef __osf__
#define HOOK_GETDIRENTRIES
#endif

#ifdef __FreeBSD__
#define USE_BSD_LSEEK_ARGUMENT
#define HOOK_GETDIRENTRIES
#endif /* __FreeBSD__ */

#if defined(__APPLE__) && defined(__MACH__) /* MacOS X */
#define HOOK_GETDIRENTRIES
#endif

#ifdef __DragonFly__
#define USE_BSD_LSEEK_ARGUMENT
#define HOOK_GETDIRENTRIES
#endif /* __DragonFly__ */

#ifdef __NetBSD__
#define USE_BSD_LSEEK_ARGUMENT
#define GETDENTS_CHAR_P
/* has getdirentries(), but doesn't have to hook it, since it's deprecated */

#define SYS_stat SYS___stat13
#define SYS_fstat SYS___fstat13
#define SYS_lstat SYS___lstat13
#endif /* __NetBSD__ */

#ifdef __OpenBSD__
#define USE_BSD_LSEEK_ARGUMENT
#define GETDENTS_CHAR_P
#endif /* __OpenBSD__ */

#ifdef SYS_utime
#include <utime.h>
#endif

/*
 * XXX - quite naive implementation
 *
 * It is necessary to re-implement more cleverly.
 */

/*
 * read
 */

ssize_t
__read(int filedes, void *buf, size_t nbyte)
{
	GFS_File gf;
	char *e;
	int n, errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __read(%d, , %lu)",
	    filedes, (unsigned long)nbyte));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return syscall(SYS_read, filedes, buf, nbyte);

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(gflog_info(
		    "GFS: Hooking __read(%d, , %lu)",
		    filedes, (unsigned long)nbyte));

		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __read(%d(%d), , %lu)",
	    filedes, gfs_pio_fileno(gf), (unsigned long)nbyte));

	e = gfs_pio_read(gf, buf, nbyte, &n);
	if (e == NULL) {
		_gfs_hook_debug_v(gflog_info(
		    "GFS: Hooking __read --> %d", n));
		errno = errno_save;
		return (n);
	}
error:

	_gfs_hook_debug(gflog_info("GFS: __read: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
_read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking _read"));
	return (__read(filedes, buf, nbyte));
}

ssize_t
read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking read"));
	return (__read(filedes, buf, nbyte));
}

ssize_t
_libc_read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking _libc_read"));
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
	int n, errno_save = errno;

	/* 
	 * DO NOT put the following line here. This causes infinite loop!
	 *
	 * _gfs_hook_debug_v(gflog_info("Hooking __write(%d, , %lu)",
	 *     filedes, (unsigned long)nbyte));
	 */

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_write, filedes, buf, nbyte));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		/*
		 * DO NOT put the following line here, which results
		 * in infinite loop.
		 * 
		 * _gfs_hook_debug(gflog_info(
		 *			"GFS: Hooking __write(%d, , %d)",
		 *			filedes, nbyte));
		 */
		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __write(%d(%d), , %lu)",
	    filedes, gfs_pio_fileno(gf), (unsigned long)nbyte));

	e = gfs_pio_write(gf, buf, nbyte, &n);
	if (e == NULL) {
		_gfs_hook_debug_v(gflog_info(
		    "GFS: Hooking __write --> %d", n));
		errno = errno_save;
		return (n);
	}
error:
	/*
	 * DO NOT put the following line here.
	 *
	 * _gfs_hook_debug(gflog_info("GFS: __write: %s", e));
	 */
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

ssize_t
_libc_write(int filedes, const void *buf, size_t nbyte)
{
	return (__write(filedes, buf, nbyte));
}

/*
 * close
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
	char *e, errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __close(%d)", filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (__syscall_close(filedes));

	switch (gfs_hook_gfs_file_type(filedes)) {
	case GFS_DT_REG:
		_gfs_hook_debug(gflog_info(
					"GFS: Hooking __close(%d(%d))",
					filedes, gfs_pio_fileno(gf)));
		break;
	case GFS_DT_DIR:
		_gfs_hook_debug(gflog_info(
					"GFS: Hooking __close(%d)",
					filedes));
		break;
	default:
		_gfs_hook_debug(gflog_info(
			"GFS: Hooking __close: couldn't get gf or dir"));
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}

	e = gfs_hook_clear_gfs_file(filedes);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}
	_gfs_hook_debug(gflog_info("GFS: __close: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_close(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking _close"));
	return (__close(filedes));
}

int
close(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking close"));
	return (__close(filedes));
}

int
_libc_close(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking close"));
	return (__close(filedes));
}

int
_private_close(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking close"));
	return (__close(filedes));
}

/*
 * unlink
 */

int
__unlink(const char *path)
{
	const char *e;
	char *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __unlink(%s)", path));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_unlink, path);

	_gfs_hook_debug(gflog_info("GFS: Hooking __unlink(%s)", path));
	if (gfs_hook_get_current_view() == section_view) {
		e = gfs_unlink_section(url, gfs_hook_get_current_section());
	} else {
		struct gfs_stat gs;

		e = gfs_stat(url, &gs);
		if (e == GFARM_ERR_NO_FRAGMENT_INFORMATION) {
			e = gfs_unlink(url);
			goto free_url;
		}
		else if (e != NULL) {
			_gfs_hook_debug(gflog_info(
			    "GFS: Hooking __unlink: gfs_stat: %s", e));
			goto free_url;
		}

		if (GFARM_S_IS_PROGRAM(gs.st_mode)) {
			char *arch;

			e = gfarm_host_get_self_architecture(&arch);
			if (e != NULL) {
				e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			} else {
				e = gfs_unlink_section(url, arch);
			}
		} else {
			e = gfs_unlink(url);
		}
		gfs_stat_free(&gs);
	}

 free_url:
	free(url);

	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __unlink: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_unlink(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking _unlink"));
	return (__unlink(path));
}

int
unlink(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking unlink"));
	return (__unlink(path));
}

/*
 * access
 */

int
__access(const char *path, int type)
{
	char *e, *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __access(%s, %d)",
	    path, type));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_access, path, type);

	_gfs_hook_debug(gflog_info("GFS: Hooking __access(%s, %d)",
				path, type));
	e = gfs_access(url, type);
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __access: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_access(const char *path, int type)
{
	_gfs_hook_debug_v(gflog_info("Hooking _access"));
	return (__access(path, type));
}

int
access(const char *path, int type)
{
	_gfs_hook_debug_v(gflog_info("Hooking access"));
	return (__access(path, type));
}

/*
 * mmap
 *
 * XXX - just print out the information.
 */

#if 0 /* XXX - Linux causes a segfault while loading a shared library.
	       Should we call old_map() instead of syscall(SYS_mmap)??? */
void *
__mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	GFS_File gf;
	int gfs_fd;

	_gfs_hook_debug_v(gflog_info(
		"Hooking __mmap(%p, %lu, %d, %d, %d, %ld)",
		addr, (unsigned long)len, prot, flags, fildes, (long)off));

	if ((gf = gfs_hook_is_open(fildes)) == NULL)
		return (void *)syscall(
			SYS_mmap, addr, len, prot, flags, fildes, off);

	_gfs_hook_debug(gflog_info(
		"GFS: Hooking __mmap(%p, %lu, %d, %d, %d, %ld)",
		addr, (unsigned long)len, prot, flags, fildes, (long)off));

	gfs_fd = gfs_pio_fileno(gf);
	return (void *)syscall(SYS_mmap, addr, len, prot, flags, gfs_fd, off);
}

void *
_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	_gfs_hook_debug_v(gflog_info("Hooking _mmap"));
	return (__mmap(addr, len, prot, flags, fildes, off));
}

void *
mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	_gfs_hook_debug_v(gflog_info("Hooking mmap"));
	return (__mmap(addr, len, prot, flags, fildes, off));
}
#endif

/*
 * dup2 and dup
 */

#ifdef SYS_dup2
int
__dup2(int oldfd, int newfd)
{
	struct _gfs_file_descriptor *d;
	GFS_File gf1, gf2;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __dup2(%d, %d)",
				  oldfd, newfd));

	gf1 = gfs_hook_is_open(oldfd);
	gf2 = gfs_hook_is_open(newfd);
	if (gf1 == NULL && gf2 ==  NULL)
		return syscall(SYS_dup2, oldfd, newfd);

	_gfs_hook_debug(gflog_info("GFS: Hooking __dup2(%d, %d)",
				oldfd, newfd));

	if (gf1 != NULL) {
		/* flush the buffer */
		(void)gfs_pio_flush(gf1);
		if (gf2 == NULL)
			/* this file may be accessed by the child process */
			gfs_pio_unset_calc_digest(gf1);
	}
	d = gfs_hook_dup_descriptor(oldfd);
	gfs_hook_set_descriptor(newfd, d);
	/*
	 * dup2() should be called after gfs_hook_set_descriptor()
	 * because newfd may be a descriptor reserved for a hooking point
	 * of a Gfarm file.
	 */
	if (syscall(SYS_dup2, oldfd, newfd) == -1)
		return (-1);

	errno = errno_save;
	return (newfd);
}

int
_dup2(int oldfd, int newfd)
{
	_gfs_hook_debug_v(gflog_info("Hooking _dup2"));
	return (__dup2(oldfd, newfd));
}

int
dup2(int oldfd, int newfd)
{
	_gfs_hook_debug_v(gflog_info("Hooking dup2"));
	return (__dup2(oldfd, newfd));
}
#endif /* SYS_dup2 */

int
__dup(int oldfd)
{
	struct _gfs_file_descriptor *d;
	int newfd, errno_save = errno;
	GFS_File gf;

	_gfs_hook_debug_v(gflog_info("Hooking __dup(%d)", oldfd));

	if ((gf = gfs_hook_is_open(oldfd)) == NULL)
		return syscall(SYS_dup, oldfd);

	_gfs_hook_debug(gflog_info("GFS: Hooking __dup(%d)", oldfd));

	/* flush the buffer */
	(void)gfs_pio_flush(gf);
	/* this file may be accessed by the child process */
	gfs_pio_unset_calc_digest(gf);

	newfd = syscall(SYS_dup, oldfd);
	if (newfd == -1)
		return (-1);
	d = gfs_hook_dup_descriptor(oldfd);
	gfs_hook_set_descriptor(newfd, d);

	errno = errno_save;
	return (newfd);
}

int
_dup(int oldfd)
{
	_gfs_hook_debug_v(gflog_info("Hooking _dup"));
	return (__dup(oldfd));
}

int
dup(int oldfd)
{
	_gfs_hook_debug_v(gflog_info("Hooking dup"));
	return (__dup(oldfd));
}

/*
 * execve
 */

int
__execve(const char *filename, char *const argv [], char *const envp[])
{
	char *url, *e;
	int status, r;
	pid_t pid;

	_gfs_hook_debug(gflog_info("Hooking __execve(%s)", filename));

	if (!gfs_hook_is_url(filename, &url)) {
		if (gfs_hook_num_gfs_files() > 0) {
			_gfs_hook_debug(
			    gflog_info("GFS: __execve(%s) - fork : %d",
				    filename, gfs_hook_num_gfs_files()));
			/* flush all of buffer */
			gfs_hook_flush_all();
			/* all files may be accessed by the child process */
			gfs_hook_unset_calc_digest_all();
			pid = fork();
		}
		else
			pid = 0;

		switch (pid) {
		case -1:
			_gfs_hook_debug(perror("GFS: fork"));
			status = 255;
			break;
		case 0:
			r = syscall(SYS_execve, filename, argv, envp);
			if (gfs_hook_num_gfs_files() == 0)
				return (r);
			_gfs_hook_debug(perror(filename));
			exit(255);
		default:
			while ((r = waitpid(pid, &status, 0)) == -1 &&
			       errno == EINTR);
			if (r == -1) {
				_gfs_hook_debug(perror("GFS: waitpid"));
				status = 255;
			}
			else if (WIFEXITED(status)) {
				switch (WEXITSTATUS(status)) {
				case 255:
					/* child process fails in execve */
					_gfs_hook_debug(
					 gflog_info("%s(%d): %s",
					 "GFS: waitpid", pid, "status 255"));
					/* XXX - need to obtain from child */
					errno = ENOENT;
					return (-1);
				default:
					status = WEXITSTATUS(status);
					break;
				}
			}
			else if (WIFSIGNALED(status)) {
				_gfs_hook_debug(
				 gflog_info(
				  "%s(%d): signal %d received%s.",
				  "GFS: waitpid", pid, WTERMSIG(status),
				  WCOREDUMP(status) ? " (core dumped)" : ""));
				/* propagate the signal */
				raise(WTERMSIG(status));
				status = 255;
			}
			break;
		}
		e = gfs_hook_close_all();
		if (e != NULL)
			_gfs_hook_debug(gflog_info("close_all: %s", e));
		exit(status);
	}
	_gfs_hook_debug(gflog_info("GFS: Hooking __execve(%s)", url));
	e = gfs_execve(url, argv, envp);
	free(url);
	_gfs_hook_debug(gflog_info("GFS: __execve: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(gflog_info("Hooking _execve"));
	return (__execve(filename, argv, envp));
}

int
execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(gflog_info("Hooking execve"));
	return (__execve(filename, argv, envp));
}

int
_private_execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(gflog_info("Hooking execve"));
	return (__execve(filename, argv, envp));
}

/*
 * utimes
 */

int
__utimes(const char *path, const struct timeval *tvp)
{
	char *e, *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __utimes(%s, %p)",
	    path, tvp));

	if (!gfs_hook_is_url(path, &url)) {
#ifdef SYS_utimes
		return syscall(SYS_utimes, path, tvp);
#else /* e.g. linux */
		if (tvp == NULL) {
			return syscall(SYS_utime, path, NULL);
		} else {
			struct utimbuf ut;

			ut.actime = tvp[0].tv_sec;
			ut.modtime = tvp[1].tv_sec;
			return syscall(SYS_utime, path, &ut);
		}
#endif
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __utimes(%s)", url));
	if (tvp == NULL)
		e = gfs_utimes(url, NULL);
	else {
		struct gfarm_timespec gt[2];

		gt[0].tv_sec = tvp[0].tv_sec;
		gt[0].tv_nsec= tvp[0].tv_usec * 1000;
		gt[1].tv_sec = tvp[1].tv_sec;
		gt[1].tv_nsec= tvp[1].tv_usec * 1000;
		e = gfs_utimes(url, gt);
	}
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __utimes: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_utimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(gflog_info("Hooking _utimes"));
	return (__utimes(path, tvp));
}

int
utimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(gflog_info("Hooking utimes"));
	return (__utimes(path, tvp));
}

/*
 * lutimes
 */

#ifdef SYS_lutimes
int
__lutimes(const char *path, const struct timeval *tvp)
{
	char *e, *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __lutimes(%s, %p)",
	    path, tvp));

	if (!gfs_hook_is_url(path, &url)) {
		return syscall(SYS_utimes, path, tvp);
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __lutimes(%s)", url));
	if (tvp == NULL)
		e = gfs_utimes(url, NULL);
	else {
		struct gfarm_timespec gt[2];

		gt[0].tv_sec = tvp[0].tv_sec;
		gt[0].tv_nsec= tvp[0].tv_usec * 1000;
		gt[1].tv_sec = tvp[1].tv_sec;
		gt[1].tv_nsec= tvp[1].tv_usec * 1000;
		e = gfs_utimes(url, gt);
	}
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __lutimes: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_lutimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(gflog_info("Hooking _lutimes"));
	return (__lutimes(path, tvp));
}

int
lutimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(gflog_info("Hooking lutimes"));
	return (__lutimes(path, tvp));
}
#endif /* SYS_lutimes */

/*
 * utime
 */

#ifdef SYS_utime
int
__utime(const char *path, const struct utimbuf *buf)
{
	char *e, *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __utime(%s, %p)",
	    path, buf));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_utime, path, buf);

	_gfs_hook_debug(gflog_info("GFS: Hooking __utime(%s)", url));
	if (buf == NULL)
		e = gfs_utimes(url, NULL);
	else {
		struct gfarm_timespec gt[2];

		gt[0].tv_sec = buf->actime;
		gt[0].tv_nsec= 0;
		gt[1].tv_sec = buf->modtime;
		gt[1].tv_nsec= 0;
		e = gfs_utimes(url, gt);
	}
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __utime: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_utime(const char *path, const struct utimbuf *buf)
{
	_gfs_hook_debug_v(gflog_info("Hooking _utime"));
	return (__utime(path, buf));
}

int
utime(const char *path, const struct utimbuf *buf)
{
	_gfs_hook_debug_v(gflog_info("Hooking utime"));
	return (__utime(path, buf));
}
#endif /* SYS_utime */

/*
 * mkdir
 */

int
__mkdir(const char *path, mode_t mode)
{
	const char *e;
	char *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __mkdir(%s, 0%o)",
				path, mode));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_mkdir, path, mode);

	_gfs_hook_debug(gflog_info("GFS: Hooking __mkdir(%s, 0%o)",
				path, mode));
	e = gfs_mkdir(url, mode);
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __mkdir: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_mkdir(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking _mkdir"));
	return (__mkdir(path, mode));
}

int
mkdir(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking mkdir"));
	return (__mkdir(path, mode));
}

/*
 * rmdir
 */

int
__rmdir(const char *path)
{
	const char *e;
	char *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __rmdir(%s)", path));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_rmdir, path);

	_gfs_hook_debug(gflog_info("GFS: Hooking __rmdir(%s)", path));
	e = gfs_rmdir(url);
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __rmdir: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_rmdir(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking _rmdir"));
	return (__rmdir(path));
}

int
rmdir(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking rmdir"));
	return (__rmdir(path));
}

/*
 * chdir
 */

int
__chdir(const char *path)
{
	const char *e;
	char *url;
	int r, errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __chdir(%s)", path));

	if (!gfs_hook_is_url(path, &url)) {
		if ((r = syscall(SYS_chdir, path)) == 0)
			gfs_hook_set_cwd_is_gfarm(0);
		return (r);
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __chdir(%s)", path));

	e = gfs_chdir(url);
	free(url);
	if (e == NULL) {
		gfs_hook_set_cwd_is_gfarm(1);
		errno = errno_save;
		return (0);
	}
	_gfs_hook_debug(gflog_info("GFS: __chdir: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chdir(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking _chdir"));
	return (__chdir(path));
}

int
chdir(const char *path)
{
	_gfs_hook_debug_v(gflog_info("Hooking chdir"));
	return (__chdir(path));
}

/*
 * fchdir
 */

int
__fchdir(int filedes)
{
	const char *e;
	int r, errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __fchdir(%d)", filedes));

	if (gfs_hook_is_open(filedes) == NULL) {
		if ((r = syscall(SYS_fchdir, filedes)) == 0)
			gfs_hook_set_cwd_is_gfarm(0);
		return (r);
	}

	if (gfs_hook_gfs_file_type(filedes) != GFS_DT_DIR) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(
		gflog_info("GFS: Hooking __fchdir(%d)", filedes));

	e = gfs_chdir(gfs_hook_get_gfs_url(filedes));
	if (e == NULL) {
		gfs_hook_set_cwd_is_gfarm(1);
		errno = errno_save;
		return (0);
	}
error:

	_gfs_hook_debug(gflog_info("GFS: __fchdir: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchdir(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking _fchdir"));
	return (__fchdir(filedes));
}

int
fchdir(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking fchdir"));
	return (__fchdir(filedes));
}

/*
 * getcwd
 */

char *
__getcwd(char *buf, size_t size)
{
	const char *e;
	char *p;
	int alloced = 0;
	int prefix_size;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info(
	    "Hooking __getcwd(%p, %lu)", buf, (unsigned long)size));

	if (!gfs_hook_get_cwd_is_gfarm())
		return (gfs_hook_syscall_getcwd(buf, size));

	_gfs_hook_debug(gflog_info(
	    "GFS: Hooking __getcwd(%p, %lu)" ,buf, (unsigned long)size));

	if (buf == NULL) {
		size = 2048;
		GFARM_MALLOC_ARRAY(buf, size);
		if (buf == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto error;
		}
		alloced = 1;
	}
	e = gfs_hook_get_prefix(buf, size);
	if (e != NULL)
		goto error;
	prefix_size = strlen(buf);
	e = gfs_getcwd(buf + prefix_size, size - prefix_size);
	if (e == NULL) {
		/* root in Gfarm FS is a special case. '/gfarm/' -> '/gfarm' */
		if (buf[0] == '/' &&
		    buf[prefix_size] == '/' && buf[prefix_size + 1] == '\0')
			buf[prefix_size] = '\0';
		if (alloced) {
			GFARM_REALLOC_ARRAY(p, buf, strlen(buf) + 1);
			if (p != NULL)
				return (p);
		}
		errno = errno_save;
		return (buf);
	}
error:
	if (alloced)
		free(buf);
	_gfs_hook_debug(gflog_info("GFS: __getcwd: %s", e));
	errno = gfarm_error_to_errno(e);
	return (NULL);
}

char *
_getcwd(char *buf, size_t size)
{
	_gfs_hook_debug_v(gflog_info("Hooking _getcwd"));
	return (__getcwd(buf, size));
}

char *
getcwd(char *buf, size_t size)
{
	_gfs_hook_debug_v(gflog_info("Hooking getcwd"));
	return (__getcwd(buf, size));
}

/*
 * chmod
 */

int
__chmod(const char *path, mode_t mode)
{
	const char *e;
	char *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __chmod(%s, 0%o)",
				path, mode));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_chmod, path, mode);

	_gfs_hook_debug(gflog_info("GFS: Hooking __chmod(%s, 0%o)",
				path, mode));
	e = gfs_chmod(url, mode);
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __chmod: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking _chmod"));
	return (__chmod(path, mode));
}

int
chmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking chmod"));
	return (__chmod(path, mode));
}

/*
 * lchmod
 */

#ifdef SYS_lchmod
int
__lchmod(const char *path, mode_t mode)
{
	const char *e;
	char *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __lchmod(%s, 0%o)",
				path, mode));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_lchmod, path, mode);

	_gfs_hook_debug(gflog_info("GFS: Hooking __lchmod(%s, 0%o)",
				path, mode));
	e = gfs_chmod(url, mode);
	free(url);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __lchmod: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_lchmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking _lchmod"));
	return (__lchmod(path, mode));
}

int
lchmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking lchmod"));
	return (__lchmod(path, mode));
}

#endif /* SYS_lchmod */

/*
 * fchmod
 */

int
__fchmod(int filedes, mode_t mode)
{
	GFS_File gf;
	char *e, *url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __fchmod(%d, 0%o)",
				filedes, mode));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return syscall(SYS_fchmod, filedes, mode);

	_gfs_hook_debug(gflog_info("GFS: Hooking __fchmod(%d, 0%o)",
				filedes, mode));

	switch (gfs_hook_gfs_file_type(filedes)) {
	case GFS_DT_REG:
		e = gfs_fchmod(gf, mode);
		break;
	case GFS_DT_DIR:
		url = gfarm_url_prefix_add(gfs_dirname((GFS_Dir)gf));
		if (url == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		e = gfs_chmod(url, mode);
		free(url);
		break;
	default:
		_gfs_hook_debug(gflog_info(
			"GFS: Hooking __fchmod: couldn't get gf or dir"));
		errno = EBADF; /* XXX - something broken */
		return (-1);
	}
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __fchmod: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchmod(int filedes, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking _fchmod"));
	return (__fchmod(filedes, mode));
}

int
fchmod(int filedes, mode_t mode)
{
	_gfs_hook_debug_v(gflog_info("Hooking fchmod"));
	return (__fchmod(filedes, mode));
}

/*
 * chown - not well tested
 */

int
__syscall_chown(const char *path, uid_t owner, gid_t group)
{
#ifdef SYS_chown32
	return (syscall(SYS_chown32, path, owner, group));
#else
	return (syscall(SYS_chown, path, owner, group));
#endif
}

int
__chown(const char *path, uid_t owner, gid_t group)
{
	const char *e;
	char *url;
	struct gfs_stat s;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __chown(%s, %d, %d)",
				  path, owner, group));

	if (!gfs_hook_is_url(path, &url))
		return (__syscall_chown(path, owner, group));

	_gfs_hook_debug(gflog_info("GFS: Hooking __chown(%s, %d, %d)",
				path, owner, group));
	e = gfs_stat(url, &s);
	free(url);
	if (e == NULL) {
		if (strcmp(s.st_user, gfarm_get_global_username()) != 0)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
		/* XXX - do nothing */
		gfs_stat_free(&s);
	}
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __chown: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking _chown"));
	return (__chown(path, owner, group));
}

int
chown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking chown"));
	return (__chown(path, owner, group));
}

int
__syscall_lchown(const char *path, uid_t owner, gid_t group)
{
#ifdef SYS_lchown32
	return (syscall(SYS_lchown32, path, owner, group));
#else
	return (syscall(SYS_lchown, path, owner, group));
#endif
}

int
__lchown(const char *path, uid_t owner, gid_t group)
{
	const char *e;
	char *url;
	struct gfs_stat s;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __lchown(%s, %d, %d)",
				  path, owner, group));

	if (!gfs_hook_is_url(path, &url))
		return (__syscall_lchown(path, owner, group));

	_gfs_hook_debug(gflog_info("GFS: Hooking __lchown(%s, %d, %d)",
				path, owner, group));
	/* XXX - gfs_lstat is not supported */
	e = gfs_stat(url, &s);
	free(url);
	if (e == NULL) {
		if (strcmp(s.st_user, gfarm_get_global_username()) != 0)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
		/* XXX - do nothing */
		gfs_stat_free(&s);
	}
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __lchown: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_lchown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking _lchown"));
	return (__lchown(path, owner, group));
}

int
lchown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking lchown"));
	return (__lchown(path, owner, group));
}

int
__syscall_fchown(int fd, uid_t owner, gid_t group)
{
#ifdef SYS_fchown32
	return (syscall(SYS_fchown32, fd, owner, group));
#else
	return (syscall(SYS_fchown, fd, owner, group));
#endif
}

int
__fchown(int fd, uid_t owner, gid_t group)
{
	GFS_File gf;
	const char *e;
	int errno_save = errno;
	struct gfs_stat s;

	_gfs_hook_debug_v(gflog_info("Hooking __fchown(%d, %d, %d)",
				  fd, owner, group));

	if ((gf = gfs_hook_is_open(fd)) == NULL)
		return (__syscall_fchown(fd, owner, group));

	_gfs_hook_debug(gflog_info("GFS: Hooking __fchown(%d, %d, %d)",
				fd, owner, group));
	e = gfs_fstat(gf, &s);
	if (e == NULL) {
		if (strcmp(s.st_user, gfarm_get_global_username()) != 0)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
		/* XXX - do nothing */
		gfs_stat_free(&s);
	}
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __fchown: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchown(int fd, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking _fchown"));
	return (__fchown(fd, owner, group));
}

int
fchown(int fd, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(gflog_info("Hooking fchown"));
	return (__fchown(fd, owner, group));
}

/*
 * acl
 */
#ifdef SYS_acl
#ifdef OS_SOLARIS /* make `mv' command work with gfs_hook on Solaris */
#include <sys/acl.h>

int
__acl(const char *path, int cmd, int nentries, aclent_t *aclbuf)
{
	const char *e;
	char *url;
	struct gfs_stat s;
	gfarm_mode_t mode;
	int n, errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __acl(%s, 0x%x)", path, cmd));

	if (!gfs_hook_is_url(path, &url))
		return (syscall(SYS_acl, path, cmd, nentries, aclbuf));

	_gfs_hook_debug(gflog_info("GFS: Hooking __acl(%s, 0x%x)", path, cmd));

	e = gfs_stat(url, &s);
	free(url);
	if (e != NULL) {
		_gfs_hook_debug(gflog_info("GFS: __acl: %s", e));
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	mode = s.st_mode;
	gfs_stat_free(&s);

	switch (cmd) {
	case SETACL:
		errno = ENOSYS;
		return (-1);
	case GETACL:
		if (nentries < 4) {
			errno = nentries < 3 ? EINVAL : ENOSPC;
			return (-1);
		}
		aclbuf[0].a_type = USER_OBJ;
		aclbuf[0].a_id = getuid(); /* XXX */
		aclbuf[0].a_perm = (mode >> 6) & 07;

		aclbuf[1].a_type = GROUP_OBJ;
		aclbuf[1].a_id = getgid(); /* XXX */
		aclbuf[1].a_perm = (mode >> 3) & 07;

		aclbuf[2].a_type = CLASS_OBJ;
		aclbuf[2].a_id = 0; /* ??? */
		aclbuf[2].a_perm = (mode >> 3) & 07; /* ??? */

		aclbuf[3].a_type = OTHER_OBJ;
		aclbuf[3].a_id = 0;
		aclbuf[3].a_perm = mode & 07;
		errno = errno_save;
		return (4);
	case GETACLCNT:
		errno = errno_save;
		return (4); /* USER_OBJ, GROUP_OBJ, CLASS_OBJ, OTHER_OBJ */
	default:
		errno = EINVAL;
		return (-1);
	}
}

int
_acl(const char *path, int cmd, int nentries, aclent_t *aclbuf)
{
	_gfs_hook_debug_v(gflog_info("Hooking _acl"));
	return (__acl(path, cmd, nentries, aclbuf));
}

int
acl(const char *path, int cmd, int nentries, aclent_t *aclbuf)
{
	_gfs_hook_debug_v(gflog_info("Hooking acl"));
	return (__acl(path, cmd, nentries, aclbuf));
}

#endif /* OS_SOLARIS */
#endif /* acl */

/*
 * rename
 */

int
__rename(const char *oldpath, const char *newpath)
{
	const char *e;
	char *oldurl, *newurl;
	int old_is_url, new_is_url;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __rename(%s, %s)",
				  oldpath, newpath));

	old_is_url = gfs_hook_is_url(oldpath, &oldurl);
	new_is_url = gfs_hook_is_url(newpath, &newurl);
	if (!old_is_url || !new_is_url) {
		if (old_is_url)
			free(oldurl);
		if (new_is_url)
			free(newurl);
		if (old_is_url != new_is_url) {
			errno = EXDEV;
			return (-1);
		}
		errno = errno_save;
		return (syscall(SYS_rename, oldpath, newpath));
	}

	_gfs_hook_debug(gflog_info("GFS: Hooking __rename(%s, %s)",
				oldpath, newpath));

	e = gfs_rename(oldurl, newurl);
	free(oldurl);
	free(newurl);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __rename: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_rename(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking _rename"));
	return (__rename(oldpath, newpath));
}

int
rename(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking rename"));
	return (__rename(oldpath, newpath));
}

/*
 * symlink
 */

int
__symlink(const char *oldpath, const char *newpath)
{
	const char *e;
	char *url;

	_gfs_hook_debug_v(gflog_info("Hooking __symlink(%s, %s)",
				  oldpath, newpath));

	if (!gfs_hook_is_url(newpath, &url))
		return (syscall(SYS_symlink, oldpath, newpath));

	_gfs_hook_debug(gflog_info("GFS: Hooking __symlink(%s, %s)",
				oldpath, newpath));

	/*
	 * Gfarm file system does not support the creation of
	 * symbolic link yet.
	 */
	e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
	free(url);

	_gfs_hook_debug(gflog_info("GFS: __symlink: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_symlink(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking _symlink"));
	return (__symlink(oldpath, newpath));
}

int
symlink(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking symlink"));
	return (__symlink(oldpath, newpath));
}

/*
 * link
 */

int
__link(const char *oldpath, const char *newpath)
{
	const char *e;
	char *url;

	_gfs_hook_debug_v(gflog_info("Hooking __link(%s, %s)",
				  oldpath, newpath));

	if (!gfs_hook_is_url(newpath, &url))
		return (syscall(SYS_link, oldpath, newpath));

	_gfs_hook_debug(gflog_info("GFS: Hooking __link(%s, %s)",
				oldpath, newpath));

	/*
	 * Gfarm file system does not support the creation of
	 * hard link yet.
	 */
	e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
	free(url);

	_gfs_hook_debug(gflog_info("GFS: __link: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_link(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking _link"));
	return (__link(oldpath, newpath));
}

int
link(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(gflog_info("Hooking link"));
	return (__link(oldpath, newpath));
}


#ifdef __linux__ /* xattr related system calls */

/*
 * getxattr
 */
int
getxattr(const char *path, const char *name, void *value, size_t size)
{
	char *e, *gfarm_file;
	char *url;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking getxattr(%s, %s, %p, %lu)",
				  path, name, value, (unsigned long)size));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_getxattr
		return syscall(SYS_getxattr, path, name, value, size);
#else
	{
		errno = ENODATA;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking getxattr(%s, %s, %p, %lu)",
				path, name, value, (unsigned long)size));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(gflog_info("GFS: getxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * lgetxattr
 */
int
lgetxattr(const char *path, const char *name, void *value, size_t size)
{
	char *e, *gfarm_file;
	char *url;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking lgetxattr(%s, %s, %p, %lu)",
				  path, name, value, (unsigned long)size));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_lgetxattr
		return syscall(SYS_lgetxattr, path, name, value, size);
#else
	{
		errno = ENODATA;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking lgetxattr(%s, %s, %p, %lu)",
				path, name, value, (unsigned long)size));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(gflog_info("GFS: lgetxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * fgetxattr
 */
int
fgetxattr(int filedes, const char *name, void *value, size_t size)
{
	char *e;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking fgetxattr(%d, %s, %p, %lu)",
				  filedes, name, value, (unsigned long)size));

	if (!gfs_hook_is_open(filedes))
#ifdef SYS_fgetxattr
		return syscall(SYS_fgetxattr, filedes, name, value, size);
#else
	{
		errno = ENODATA;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking fgetxattr(%d, %s, %p, %lu)",
				filedes, name, value, (unsigned long)size));

	e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	_gfs_hook_debug(gflog_info("GFS: fgetxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * setxattr
 */
int
setxattr(const char *path, const char *name, void *value, size_t size,
	int flags)
{
	char *e, *gfarm_file;
	char *url;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking setxattr(%s, %s, %p, %lu, %d)",
				  path, name, value, (unsigned long)size,
				  flags));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_setxattr
		return syscall(SYS_setxattr, path, name, value, size, flags);
#else
	{
		errno = ENOTSUP;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking setxattr(%s, %s, %p, %lu, %d)",
				path, name, value, (unsigned long)size,
				flags));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(gflog_info("GFS: setxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * lsetxattr
 */
int
lsetxattr(const char *path, const char *name, void *value, size_t size,
	int flags)
{
	char *e, *gfarm_file;
	char *url;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking lsetxattr(%s, %s, %p, %lu, %d)",
				  path, name, value, (unsigned long)size,
				  flags));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_lsetxattr
		return syscall(SYS_lsetxattr, path, name, value, size, flags);
#else
	{
		errno = ENOTSUP;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking lsetxattr(%s, %s, %p, %lu, %d)",
				path, name, value, (unsigned long)size, flags));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(gflog_info("GFS: lsetxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

/*
 * fsetxattr
 */
int
fsetxattr(int filedes, const char *name, void *value, size_t size, int flags)
{
	char *e;

	_gfs_hook_debug_v(gflog_info(
				  "Hooking fsetxattr(%d, %s, %p, %lu, %d)",
				  filedes, name, value, (unsigned long)size,
				  flags));

	if (!gfs_hook_is_open(filedes))
#ifdef SYS_fsetxattr
		return syscall(SYS_fsetxattr, filedes, name, value, size,
			       flags);
#else
	{
		errno = ENOTSUP;
		return (-1);
	}
#endif

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking fsetxattr(%d, %s, %p, %lu, %d)",
				filedes, name, value, (unsigned long)size,
				flags));

	e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	_gfs_hook_debug(gflog_info("GFS: fsetxattr: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

#endif /* __linux__ - xattr related system calls */

/*
 * mknod
 */

#ifndef _MKNOD_VER

int
__mknod(const char *path, mode_t mode, dev_t dev)
{
	const char *e;
	char *url;
	struct gfs_stat gs;
	GFS_File gf;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __mknod"));

	if (!gfs_hook_is_url(path, &url))
		return (syscall(SYS_mknod, path, mode, dev));

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking __mknod(%s, %o)", path, mode));

	if (gfs_hook_get_current_view() == section_view)
		e = gfs_stat_section(url, gfs_hook_get_current_section(), &gs);
	else
		e = gfs_stat(url, &gs);
	if (e == NULL) {
		gfs_stat_free(&gs);
		errno = EEXIST;
		return (-1);
	} else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* fall through */
	} else {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	switch(mode & S_IFMT) {
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		errno = EPERM;
		return (-1);
#if defined(OS_SOLARIS) || defined(__linux__)
	case 0:
#endif
	case S_IFREG:
		if ((e = gfs_pio_create(url,
			GFARM_FILE_WRONLY|GFARM_FILE_EXCLUSIVE, mode, &gf))
			!= NULL) {
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
		if ((e = gfs_pio_close(gf)) != NULL) {
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
		errno = errno_save;
		return (0);
	}
	errno = EINVAL;
	return (-1);
}

int
_mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(gflog_info("Hooking _mknod"));
	return (__mknod(path, mode, dev));
}

int
mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(gflog_info("Hooking mknod"));
	return (__mknod(path, mode, dev));
}

#else /* defined _MKNOD_VER */

#ifdef __linux__

int
__mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(gflog_info("Hooking __mknod"));
	return (__xmknod(_MKNOD_VER, path, mode, &dev));
}

int
_mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(gflog_info("Hooking _mknod"));
	return (__xmknod(_MKNOD_VER, path, mode, &dev));
}

#endif /* __linux__ */

int
#ifdef OS_SOLARIS
__xmknod(const int ver, const char *path, mode_t mode, dev_t dev)
#else
__xmknod(int ver, const char *path, mode_t mode, dev_t *dev)
#endif
{
	const char *e;
	char *url;
	int errno_save = errno;
	struct gfs_stat gs;
	GFS_File gf;

	_gfs_hook_debug_v(gflog_info("Hooking __xmknod"));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_xmknod
		return (syscall(SYS_xmknod, ver, path, mode, dev));
#else /* !defined(SYS_xmknod) */
		return (syscall(SYS_mknod, path, mode, *dev));
#endif /* SYS_xmknod */

	_gfs_hook_debug(gflog_info(
				"GFS: Hooking __xmknod(%s, %o)", path, mode));

	if (gfs_hook_get_current_view() == section_view)
		e = gfs_stat_section(url, gfs_hook_get_current_section(), &gs);
	else
		e = gfs_stat(url, &gs);
	if (e == NULL) {
		gfs_stat_free(&gs);
		errno = EEXIST;
		return (-1);
	} else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* fall through */
	} else {
		errno = gfarm_error_to_errno(e);
		return (-1);
	}
	switch(mode & S_IFMT) {
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		errno = EPERM;
		return (-1);
#if defined(OS_SOLARIS) || defined(__linux__)
	case 0:
#endif
	case S_IFREG:
		if ((e = gfs_pio_create(url,
			GFARM_FILE_WRONLY|GFARM_FILE_EXCLUSIVE, mode, &gf))
			!= NULL) {
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
		if ((e = gfs_pio_close(gf)) != NULL) {
			errno = gfarm_error_to_errno(e);
			return (-1);
		}
		errno = errno_save;
		return (0);
	}
	errno = EINVAL;
	return (-1);
}

int
#ifdef OS_SOLARIS
_xmknod(const int ver, const char *path, mode_t mode, dev_t dev)
#else
_xmknod(int ver, const char *path, mode_t mode, dev_t *dev)
#endif
{
	_gfs_hook_debug_v(gflog_info("Hooking _xmknod"));
	return (__xmknod(ver, path, mode, dev));
}

#endif /* _MKNOD_VER */

/* XXX - Eventualy we always need fcntl() hook, but not for now... */
#if defined(F_FREESP) || defined(F_FREESP64)
int
__fcntl(int filedes, int cmd, ...)
{
	va_list ap;
	GFS_File gf;
	char *e;
	unsigned long val;
	int errno_save = errno;

	va_start(ap, cmd);
	val = va_arg(ap, unsigned long);
	va_end(ap);

	_gfs_hook_debug_v(gflog_info("Hooking __fcntl(%d, %d, %x)",
					filedes, cmd, val));

#ifdef F_FREESP
	if (cmd == F_FREESP)
		_gfs_hook_debug_v(gflog_info("flock.l_start:%ld",
			(long)(((struct flock *)val)->l_start)));
#endif
#ifdef F_FREESP64
	if (cmd == F_FREESP64)
		_gfs_hook_debug_v(gflog_info("flock64.l_start:%ld",
			(long)(((struct flock64 *)val)->l_start)));
#endif
	if (cmd == F_GETFD || cmd == F_SETFD ||
	    (gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_fcntl, filedes, cmd, val));

	_gfs_hook_debug(gflog_info(
		"GFS: Hooking __fcntl(%d(%d), %d, %x)",
		filedes, gfs_pio_fileno(gf), cmd, val));

	switch (cmd) {
#ifdef F_FREESP
	case F_FREESP:
		if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
			e = GFARM_ERR_IS_A_DIRECTORY;
			break;
		}
		e = gfs_pio_truncate(gf, ((struct flock *)val)->l_start);
		break;
#endif
#ifdef F_FREESP64
	case F_FREESP64:
		if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
			e = GFARM_ERR_IS_A_DIRECTORY;
			break;
		}
		e = gfs_pio_truncate(gf, ((struct flock64 *)val)->l_start);
		break;
#endif
	default:
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
		break;
	}
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __fcntl: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fcntl(int filedes, int cmd, ...)
{
	va_list ap;
	unsigned long val;

	va_start(ap, cmd);
	val = va_arg(ap, unsigned long);
	va_end(ap);
	_gfs_hook_debug_v(gflog_info("Hooking fcntl(%d, %d, %x)",
				  filedes, cmd, val));
	return (__fcntl(filedes, cmd, val));
}

int
fcntl(int filedes, int cmd, ...)
{
	va_list ap;
	unsigned long val;

	va_start(ap, cmd);
	val = va_arg(ap, unsigned long);
	va_end(ap);
	_gfs_hook_debug_v(gflog_info("Hooking fcntl(%d, %d, %x)",
				  filedes, cmd, val));
	return (__fcntl(filedes, cmd, val));
}
#endif /* defined(F_FREESP) || defined(F_FREESP64) */

int
__fsync(int filedes)
{
	GFS_File gf;
	char *e;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __fsync(%d)", filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
#ifdef SYS_fdsync /* Solaris */
		return (syscall(SYS_fdsync, filedes, FSYNC));
#else
		return (syscall(SYS_fsync, filedes));
#endif

	_gfs_hook_debug(gflog_info(
		"GFS: Hooking __fsync(%d(%d))",
				filedes, gfs_pio_fileno(gf)));

	e = gfs_pio_sync(gf);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __fsync: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fsync(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking _fsync(%d)", filedes));
	return (__fsync(filedes));
}

int
fsync(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking fsync(%d)", filedes));
	return (__fsync(filedes));
}

#if defined(SYS_fdatasync) || defined(SYS_fdsync)
int
__fdatasync(int filedes)
{
	GFS_File gf;
	char *e;
	int errno_save = errno;

	_gfs_hook_debug_v(gflog_info("Hooking __fdatasync(%d)",
				  filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
#ifdef SYS_fdsync /* Solaris */
		return (syscall(SYS_fdsync, filedes, FDSYNC));
#else
		return (syscall(SYS_fdatasync, filedes));
#endif

	_gfs_hook_debug(gflog_info(
		"GFS: Hooking __fdatasync(%d(%d))",
				filedes, gfs_pio_fileno(gf)));

	e = gfs_pio_datasync(gf);
	if (e == NULL) {
		errno = errno_save;
		return (0);
	}

	_gfs_hook_debug(gflog_info("GFS: __fdatasync: %s", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fdatasync(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking _fdatasync(%d)",
				  filedes));
	return (__fdatasync(filedes));
}

int
fdatasync(int filedes)
{
	_gfs_hook_debug_v(gflog_info("Hooking fdatasync(%d)", filedes));
	return (__fdatasync(filedes));
}
#endif /* SYS_fdatasync */

#if defined(HAVE_FDOPENDIR) && defined(OS_SOLARIS)
/*
 * opendir - this entry is needed to hook opendir on Solaris 9
 */

DIR *
opendir(const char *dirname)
{
	DIR *dirp;
	int d;

	d = open(dirname, O_RDONLY | O_NDELAY | O_LARGEFILE, 0);
	if (d < 0)
		return (NULL);
	dirp = fdopendir(d);
	if (dirp == NULL)
		(void)close(d);
	return (dirp);
}
#endif /* sun */

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
	return (gfs_hook_syscall_open(path, O_CREAT|O_TRUNC|O_WRONLY, mode));
}

off_t
gfs_hook_syscall_lseek(int filedes, off_t offset, int whence)
{
#ifdef USE_BSD_LSEEK_ARGUMENT
	return (__syscall((quad_t)SYS_lseek, filedes, 0, offset, whence));
#else
	return (syscall(SYS_lseek, filedes, offset, whence));
#endif
}

#ifdef SYS_pread
ssize_t
gfs_hook_syscall_pread(int filedes, void *buf, size_t nbyte, off_t offset)
{
#if defined(__NetBSD__)
	int64_t q;

	q = syscall((int64_t)SYS_pread, filedes, buf, nbyte, 0, offset);
#  ifndef WORDS_BIGENDIAN
	return ((int)q);
#  else
	if (sizeof(int64_t) == sizeof(register_t))
		return ((int)q);
	else
		return ((int)((uint64_t)q >> 32));
#  endif
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	return (syscall((int64_t)SYS_pread, filedes, buf, nbyte, 0, offset));
#else
	return (syscall(SYS_pread, filedes, buf, nbyte, offset));
#endif
}

#define SYSCALL_PREAD(filedes, buf, nbyte, offset)	\
	gfs_hook_syscall_pread(filedes, buf, nbyte, offset)
#define FUNC___PREAD	__pread
#define FUNC__PREAD	_pread
#define FUNC_PREAD	pread
#endif

#ifdef SYS_pwrite
ssize_t
gfs_hook_syscall_pwrite(int filedes, const void *buf, size_t nbyte, off_t offset)
{
#if defined(__NetBSD__)
	int64_t q;

	q = syscall((int64_t)SYS_pwrite, filedes, buf, nbyte, 0, offset);
#  ifndef WORDS_BIGENDIAN
	return ((int)q);
#  else
	if (sizeof(int64_t) == sizeof(register_t))
		return ((int)q);
	else
		return ((int)((uint64_t)q >> 32));
#  endif
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	return (syscall((int64_t)SYS_pwrite, filedes, buf, nbyte, 0, offset));
#else
	return (syscall(SYS_pwrite, filedes, buf, nbyte, offset));
#endif
}

#define SYSCALL_PWRITE(filedes, buf, nbyte, offset)	\
	gfs_hook_syscall_pwrite(filedes, buf, nbyte, offset)
#define FUNC___PWRITE	__pwrite
#define FUNC__PWRITE	_pwrite
#define FUNC_PWRITE	pwrite
#endif

#ifdef HOOK_GETDIRENTRIES
int
gfs_hook_syscall_getdirentries(int filedes, char *buf, int nbyte, long *offp)
{
	return (syscall(SYS_getdirentries, filedes, buf, nbyte, offp));
}
#elif !defined(__linux__) /* linux version is defined in sysdep/linux/ */
int
gfs_hook_syscall_getdents(int filedes, struct dirent *buf, size_t nbyte)
{
	return (syscall(SYS_getdents, filedes, buf, nbyte));
}
#endif /* !defined(__linux__) */

#ifdef SYS_truncate
int
gfs_hook_syscall_truncate(const char *path, off_t length)
{
#if defined(__NetBSD__)
	int64_t q;

	q = syscall((int64_t)SYS_truncate, path, 0, length);
#  ifndef WORDS_BIGENDIAN
	return (q);
#  else
	if (sizeof(int64_t) == sizeof(register_t))
		return (q);
	else
		return (((uint64_t)q >> 32));
#  endif
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	return (syscall((int64_t)SYS_truncate, path, 0, length));
#else
	return (syscall(SYS_truncate, path, length));
#endif
}

#define SYSCALL_TRUNCATE(path, length) \
	gfs_hook_syscall_truncate(path, length)
#define FUNC___TRUNCATE	__truncate
#define FUNC__TRUNCATE	_truncate
#define FUNC_TRUNCATE	truncate
#endif

#ifdef SYS_ftruncate
int
gfs_hook_syscall_ftruncate(int filedes, off_t length)
{
#if defined(__NetBSD__)
	int64_t q;

	q = syscall((int64_t)SYS_ftruncate, filedes, 0, length);
#  ifndef WORDS_BIGENDIAN
	return (q);
#  else
	if (sizeof(int64_t) == sizeof(register_t))
		return (q);
	else
		return (((uint64_t)q >> 32));
#  endif
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	return (syscall((int64_t)SYS_ftruncate, filedes, 0, length));
#else
	return (syscall(SYS_ftruncate, filedes, length));
#endif
}

#define SYSCALL_FTRUNCATE(filedes, length) \
	gfs_hook_syscall_ftruncate(filedes, length)
#define FUNC___FTRUNCATE	__ftruncate
#define FUNC__FTRUNCATE		_ftruncate
#define FUNC_FTRUNCATE		ftruncate
#endif

int
gfs_hook_syscall_stat(const char *path, struct stat *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_stat, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_xstat(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_lstat(const char *path, struct stat *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_lstat, path, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_lxstat(_STAT_VER, path, buf));
#endif
}

int
gfs_hook_syscall_fstat(int filedes, struct stat *buf)
{
#ifndef _STAT_VER
	return (syscall(SYS_fstat, filedes, buf));
#else /* SVR4 or Linux */
	return (gfs_hook_syscall_fxstat(_STAT_VER, filedes, buf));
#endif
}

#if defined(_STAT_VER) && defined(SYS_xstat)
/*
 * for SVR4.
 *
 * but not for Linux. (see sysdep/linux/xstat.c for Linux version)
 */

int
gfs_hook_syscall_xstat(int ver, const char *path, struct stat *buf)
{
	return (syscall(SYS_xstat, ver, path, buf));

}

int
gfs_hook_syscall_lxstat(int ver, const char *path, struct stat *buf)
{
	return (syscall(SYS_lxstat, ver, path, buf));

}

int
gfs_hook_syscall_fxstat(int ver, int filedes, struct stat *buf)
{
	return (syscall(SYS_fxstat, ver, filedes, buf));

}
#endif /* defined(_STAT_VER) && defined(SYS_xstat) */

#define OFF_T off_t

#define SYSCALL_OPEN(path, oflag, mode)	\
	gfs_hook_syscall_open(path, oflag, mode)
#define SYSCALL_CREAT(path, mode)	\
	gfs_hook_syscall_creat(path, mode)
#define SYSCALL_LSEEK(filedes, offset, whence)	\
	gfs_hook_syscall_lseek(filedes, offset, whence)

#define FUNC___OPEN	__open
#define FUNC__OPEN	_open
#define FUNC_OPEN	open
#define FUNC___CREAT	__creat
#define FUNC__CREAT	_creat
#define FUNC_CREAT	creat
#define FUNC__LIBC_CREAT	_libc_creat
#define FUNC___LSEEK	__lseek
#define FUNC__LSEEK	_lseek
#define FUNC_LSEEK	lseek


#ifdef HOOK_GETDIRENTRIES
#define SYSCALL_GETDENTS(filedes, buf, nbyte, offp) \
	gfs_hook_syscall_getdirentries(filedes, buf, nbyte, offp)
#define FUNC___GETDENTS	__getdirentries
#define FUNC__GETDENTS	_getdirentries
#define FUNC_GETDENTS	getdirentries
#else
#define SYSCALL_GETDENTS(filedes, buf, nbyte) \
	gfs_hook_syscall_getdents(filedes, buf, nbyte)
#define FUNC___GETDENTS	__getdents
#define FUNC__GETDENTS	_getdents
#define FUNC_GETDENTS	getdents
#endif

#define STRUCT_DIRENT	struct dirent
#define ALIGNMENT 8
#define ALIGN(p) (((unsigned long)(p) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

#include "hooks_common.c"

#undef ALIGNMENT

#if defined(HOOK_GETDIRENTRIES) && defined(HAVE_GETDENTS)
int
__getdent(int filedes, char *buf, unsigned int nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking __getdent: %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte, NULL));
}

int
_getdent(int filedes, char *buf, unsigned int nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking _getdent: %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte, NULL));
}

int
getdent(int filedes, char *buf, unsigned int nbyte)
{
	_gfs_hook_debug_v(gflog_info("Hooking getdent: %d",
				  filedes));
	return (FUNC___GETDENTS(filedes, (STRUCT_DIRENT *)buf, nbyte, NULL));
}
#endif /* defined(HOOK_GETDIRENTRIES) && defined(HAVE_GETDENTS) */


/* stat */

#define STRUCT_STAT	struct stat

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_stat(path, buf)
#define FUNC___STAT	__stat
#define FUNC__STAT	_stat
#define FUNC_STAT	stat
#define GFS_STAT	gfs_stat
#define GFS_STAT_SECTION gfs_stat_section
#define GFS_STAT_INDEX	gfs_stat_index

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_xstat(ver, path, buf)
#define FUNC___XSTAT	__xstat
#define FUNC__XSTAT	_xstat
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
#endif

/* lstat */

#define SYSCALL_STAT(path, buf)	\
	gfs_hook_syscall_lstat(path, buf)
#define FUNC___STAT	__lstat
#define FUNC__STAT	_lstat
#define FUNC_STAT	lstat
#define GFS_STAT	gfs_stat /* XXX - gfs_lstat is not implemented yet. */

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_XSTAT(ver, path, buf)	\
	gfs_hook_syscall_lxstat(ver, path, buf)
#define FUNC___XSTAT	__lxstat
#define FUNC__XSTAT	_lxstat
#endif

#include "hooks_stat.c"

/* fstat */

#define SYSCALL_FSTAT(path, buf)	\
	gfs_hook_syscall_fstat(path, buf)
#define FUNC___FSTAT	__fstat
#define FUNC__FSTAT	_fstat
#define FUNC_FSTAT	fstat
#define GFS_FSTAT	gfs_fstat

#ifdef _STAT_VER /* SVR4 or Linux */
#define SYSCALL_FXSTAT(ver, fd, buf)	\
	gfs_hook_syscall_fxstat(ver, fd, buf)
#define FUNC___FXSTAT	__fxstat
#define FUNC__FXSTAT	_fxstat
#endif

#include "hooks_fstat.c"

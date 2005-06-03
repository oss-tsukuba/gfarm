/*
 * Hooking system calls to utilize Gfarm file system.
 *
 * $Id$
 */

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
#include <gfarm/gfarm_error.h>
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
#define SYS_fstat SYS___fstat13
#define SYS_lstat SYS___lstat13
#endif

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
	int n;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __read(%d, , %lu)\n",
	    filedes, (unsigned long)nbyte));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return syscall(SYS_read, filedes, buf, nbyte);

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		_gfs_hook_debug(fprintf(stderr,
		    "GFS: Hooking __read(%d, , %lu)\n",
		    filedes, (unsigned long)nbyte));

		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __read(%d(%d), , %lu)\n",
	    filedes, gfs_pio_fileno(gf), (unsigned long)nbyte));

	e = gfs_pio_read(gf, buf, nbyte, &n);
	if (e == NULL) {
		_gfs_hook_debug_v(fprintf(stderr,
		    "GFS: Hooking __read --> %d\n", n));
		return (n);
	}
error:

	_gfs_hook_debug(fprintf(stderr, "GFS: __read: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

ssize_t
_read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(fputs("Hooking _read\n", stderr));
	return (__read(filedes, buf, nbyte));
}

ssize_t
read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(fputs("Hooking read\n", stderr));
	return (__read(filedes, buf, nbyte));
}

ssize_t
_libc_read(int filedes, void *buf, size_t nbyte)
{
	_gfs_hook_debug_v(fputs("Hooking _libc_read\n", stderr));
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

	/* 
	 * DO NOT put the following line here. This causes infinite loop!
	 *
	 * _gfs_hook_debug_v(fprintf(stderr, "Hooking __write(%d, , %lu)\n",
	 *     filedes, (unsigned long)nbyte));
	 */

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_write, filedes, buf, nbyte));

	if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
		/*
		 * DO NOT put the following line here, which results
		 * in infinite loop.
		 * 
		 * _gfs_hook_debug(fprintf(stderr,
		 *			"GFS: Hooking __write(%d, , %d)\n",
		 *			filedes, nbyte));
		 */
		e = GFARM_ERR_IS_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __write(%d(%d), , %lu)\n",
	    filedes, gfs_pio_fileno(gf), (unsigned long)nbyte));

	e = gfs_pio_write(gf, buf, nbyte, &n);
	if (e == NULL) {
		_gfs_hook_debug_v(fprintf(stderr,
		    "GFS: Hooking __write --> %d\n", n));
		return (n);
	}
error:
	/*
	 * DO NOT put the following line here.
	 *
	 * _gfs_hook_debug(fprintf(stderr, "GFS: __write: %s\n", e));
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
	char *e;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __close(%d)\n", filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (__syscall_close(filedes));

	switch (gfs_hook_gfs_file_type(filedes)) {
	case GFS_DT_REG:
		_gfs_hook_debug(fprintf(stderr,
					"GFS: Hooking __close(%d(%d))\n",
					filedes, gfs_pio_fileno(gf)));
		break;
	case GFS_DT_DIR:
		_gfs_hook_debug(fprintf(stderr,
					"GFS: Hooking __close(%d)\n",
					filedes));
		break;
	default:
		_gfs_hook_debug(fprintf(stderr,
			"GFS: Hooking __close: couldn't get gf or dir\n"));
		errno = EBADF; /* XXX - something broken */
		return (-1);			
	}

	e = gfs_hook_clear_gfs_file(filedes);
	if (e == NULL)
		return (0);
	_gfs_hook_debug(fprintf(stderr, "GFS: __close: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_close(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking _close\n", stderr));
	return (__close(filedes));
}

int
close(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking close\n", stderr));
	return (__close(filedes));
}

int
_libc_close(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking close\n", stderr));
	return (__close(filedes));
}

int
_private_close(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking close\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __unlink(%s)\n", path));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_unlink, path);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __unlink(%s)\n", path));
	if (gfs_hook_get_current_view() == section_view) {
		e = gfs_unlink_section(url, gfs_hook_get_current_section());
	} else {	
		struct gfs_stat gs;

		e = gfs_stat(url, &gs);
		if (e != NULL) {
			_gfs_hook_debug(fprintf(stderr,
			    "GFS: Hooking __unlink: gfs_stat: %s\n", e));
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

	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __unlink: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_unlink(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking _unlink\n", stderr));
	return (__unlink(path));
}

int
unlink(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking unlink\n", stderr));
	return (__unlink(path));
}

/*
 * access
 */

int
__access(const char *path, int type)
{
	char *e, *url;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __access(%s, %d)\n",
	    path, type));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_access, path, type);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __access(%s, %d)\n",
				path, type));
	e = gfs_access(url, type);
	free(url);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __access: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_access(const char *path, int type)
{
	_gfs_hook_debug_v(fputs("Hooking _access\n", stderr));
	return (__access(path, type));
}

int
access(const char *path, int type)
{
	_gfs_hook_debug_v(fputs("Hooking access\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr,
		"Hooking __mmap(%p, %lu, %d, %d, %d, %ld)\n",
		addr, (unsigned long)len, prot, flags, fildes, (long)off));

	if ((gf = gfs_hook_is_open(fildes)) == NULL)
		return (void *)syscall(
			SYS_mmap, addr, len, prot, flags, fildes, off);

	_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking __mmap(%p, %lu, %d, %d, %d, %ld)\n",
		addr, (unsigned long)len, prot, flags, fildes, (long)off));

	gfs_fd = gfs_pio_fileno(gf);
	return (void *)syscall(SYS_mmap, addr, len, prot, flags, gfs_fd, off);
}

void *
_mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	_gfs_hook_debug_v(fputs("Hooking _mmap\n", stderr));
	return (__mmap(addr, len, prot, flags, fildes, off));
}

void *
mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
	_gfs_hook_debug_v(fputs("Hooking mmap\n", stderr));
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
	
	_gfs_hook_debug_v(fprintf(stderr, "Hooking __dup2(%d, %d)\n",
				  oldfd, newfd));

	gf1 = gfs_hook_is_open(oldfd);
	gf2 = gfs_hook_is_open(newfd);
	if (gf1 == NULL && gf2 ==  NULL)
		return syscall(SYS_dup2, oldfd, newfd);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __dup2(%d, %d)\n",
				oldfd, newfd));

	if (gf1 != NULL) {
		/* flush the buffer */
		(void)gfs_pio_flush(gf1);
		if (gf2 == NULL)
			/* this file may be accessed by the child process */
			gfs_hook_mode_calc_digest(gf1);
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

	return (newfd);
}

int
_dup2(int oldfd, int newfd)
{
	_gfs_hook_debug_v(fputs("Hooking _dup2\n", stderr));
	return (__dup2(oldfd, newfd));
}

int
dup2(int oldfd, int newfd)
{
	_gfs_hook_debug_v(fputs("Hooking dup2\n", stderr));
	return (__dup2(oldfd, newfd));
}
#endif /* SYS_dup2 */

int
__dup(int oldfd)
{
	struct _gfs_file_descriptor *d;
	int newfd;
	GFS_File gf;
	
	_gfs_hook_debug_v(fprintf(stderr, "Hooking __dup(%d)\n", oldfd));

	if ((gf = gfs_hook_is_open(oldfd)) == NULL)
		return syscall(SYS_dup, oldfd);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __dup(%d)\n", oldfd));

	/* flush the buffer */
	(void)gfs_pio_flush(gf);
	/* this file may be accessed by the child process */
	gfs_hook_mode_calc_digest(gf);

	newfd = syscall(SYS_dup, oldfd);
	if (newfd == -1)
		return (-1);
	d = gfs_hook_dup_descriptor(oldfd);
	gfs_hook_set_descriptor(newfd, d);

	return (newfd);
}

int
_dup(int oldfd)
{
	_gfs_hook_debug_v(fputs("Hooking _dup\n", stderr));
	return (__dup(oldfd));
}

int
dup(int oldfd)
{
	_gfs_hook_debug_v(fputs("Hooking dup\n", stderr));
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

	_gfs_hook_debug(fprintf(stderr, "Hooking __execve(%s)\n", filename));

	if (!gfs_hook_is_url(filename, &url)) {
		if (gfs_hook_num_gfs_files() > 0) {
			_gfs_hook_debug(
			    fprintf(stderr, "GFS: __execve(%s) - fork : %d\n",
				    filename, gfs_hook_num_gfs_files()));
			/* flush all of buffer */
			gfs_hook_flush_all();
			/* all files may be accessed by the child process */
			gfs_hook_mode_calc_digest_all();
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
					 fprintf(stderr, "%s(%d): %s\n",
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
				 fprintf(stderr,
				  "%s(%d): signal %d received%s.\n",
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
			_gfs_hook_debug(fprintf(stderr, "close_all: %s\n", e));
		exit(status);
	}
	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __execve(%s)\n", url));
	e = gfs_execve(url, argv, envp);
	free(url);
	_gfs_hook_debug(fprintf(stderr, "GFS: __execve: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(fputs("Hooking _execve\n", stderr));
	return (__execve(filename, argv, envp));
}

int
execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(fputs("Hooking execve\n", stderr));
	return (__execve(filename, argv, envp));
}

int
_private_execve(const char *filename, char *const argv [], char *const envp[])
{
	_gfs_hook_debug_v(fputs("Hooking execve\n", stderr));
	return (__execve(filename, argv, envp));
}

/*
 * utimes & utime
 */

int
__utimes(const char *path, const struct timeval *tvp)
{
	char *e, *url;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __utimes(%s, %p)\n",
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

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __utimes(%s)\n", url));
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
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __utimes: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int 
_utimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(fputs("Hooking _utimes\n", stderr));
	return (__utimes(path, tvp));
}

int 
utimes(const char *path, const struct timeval *tvp)
{
	_gfs_hook_debug_v(fputs("Hooking utimes\n", stderr));
	return (__utimes(path, tvp));
}

#ifdef SYS_utime
int
__utime(const char *path, const struct utimbuf *buf)
{
	char *e, *url;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __utime(%s, %p)\n",
	    path, buf));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_utime, path, buf);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __utime(%s)\n", url));
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
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __utime: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int 
_utime(const char *path, const struct utimbuf *buf)
{
	_gfs_hook_debug_v(fputs("Hooking _utime\n", stderr));
	return (__utime(path, buf));
}

int 
utime(const char *path, const struct utimbuf *buf)
{
	_gfs_hook_debug_v(fputs("Hooking utime\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __mkdir(%s, 0%o)\n",
				path, mode));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_mkdir, path, mode);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __mkdir(%s, 0%o)\n",
				path, mode));
	e = gfs_mkdir(url, mode);
	free(url);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __mkdir: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_mkdir(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking _mkdir\n", stderr));
	return (__mkdir(path, mode));
}

int
mkdir(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking mkdir\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __rmdir(%s)\n", path));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_rmdir, path);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __rmdir(%s)\n", path));
	e = gfs_rmdir(url);
	free(url);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __rmdir: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_rmdir(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking _rmdir\n", stderr));
	return (__rmdir(path));
}

int
rmdir(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking rmdir\n", stderr));
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
	int r;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __chdir(%s)\n", path));

	if (!gfs_hook_is_url(path, &url)) {
		if ((r = syscall(SYS_chdir, path)) == 0)
			gfs_hook_set_cwd_is_gfarm(0);
		return (r);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __chdir(%s)\n", path));

	e = gfs_chdir(url);
	free(url);
	if (e == NULL) {
		gfs_hook_set_cwd_is_gfarm(1);
		return (0);
	}
	_gfs_hook_debug(fprintf(stderr, "GFS: __chdir: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chdir(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking _chdir\n", stderr));
	return (__chdir(path));
}

int
chdir(const char *path)
{
	_gfs_hook_debug_v(fputs("Hooking chdir\n", stderr));
	return (__chdir(path));
}

/*
 * fchdir
 */

int
__fchdir(int filedes)
{
	GFS_File gf;
	const char *e;
	char *url;
	int r;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fchdir(%d)\n", filedes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL) {
		if ((r = syscall(SYS_fchdir, filedes)) == 0)
			gfs_hook_set_cwd_is_gfarm(0);
		return (r);
	}

	if (gfs_hook_gfs_file_type(filedes) != GFS_DT_DIR) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		goto error;
	}

	_gfs_hook_debug(
		fprintf(stderr, "GFS: Hooking __fchdir(%d)\n", filedes));

	e = gfarm_path_canonical_to_url(
		gfs_hook_get_gfs_canonical_path(filedes), &url);
	if (e != NULL)
		goto error;

	e = gfs_chdir(url);
	free(url);	
	if (e == NULL) {
		gfs_hook_set_cwd_is_gfarm(1);
		return (0);
	}
error:

	_gfs_hook_debug(fprintf(stderr, "GFS: __fchdir: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchdir(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking _fchdir\n", stderr));
	return (__fchdir(filedes));
}

int
fchdir(int filedes)
{
	_gfs_hook_debug_v(fputs("Hooking fchdir\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, 
	    "Hooking __getcwd(%p, %lu)\n", buf, (unsigned long)size));

	if (!gfs_hook_get_cwd_is_gfarm())
		return (gfs_hook_syscall_getcwd(buf, size));

	_gfs_hook_debug(fprintf(stderr,
	    "GFS: Hooking __getcwd(%p, %lu)\n" ,buf, (unsigned long)size));

	if (buf == NULL) {
		size = 2048;
		buf = malloc(size);
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
			p = realloc(buf, strlen(buf) + 1);
			if (p != NULL)
				return (p);
		}
		return (buf);
	}
error:
	if (alloced)
		free(buf);
	_gfs_hook_debug(fprintf(stderr, "GFS: __getcwd: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (NULL);
}

char *
_getcwd(char *buf, size_t size)
{
	_gfs_hook_debug_v(fputs("Hooking _getcwd\n", stderr));
	return (__getcwd(buf, size));
}

char *
getcwd(char *buf, size_t size)
{
	_gfs_hook_debug_v(fputs("Hooking getcwd\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __chmod(%s, 0%o)\n",
				path, mode));

	if (!gfs_hook_is_url(path, &url))
		return syscall(SYS_chmod, path, mode);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __chmod(%s, 0%o)\n",
				path, mode));
	e = gfs_chmod(url, mode);
	free(url);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __chmod: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking _chmod\n", stderr));
	return (__chmod(path, mode));
}

int
chmod(const char *path, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking chmod\n", stderr));
	return (__chmod(path, mode));
}

/*
 * fchmod
 */

int
__fchmod(int filedes, mode_t mode)
{
	GFS_File gf;
	char *e, *url;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fchmod(%d, 0%o)\n",
				filedes, mode));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return syscall(SYS_fchmod, filedes, mode);

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __fchmod(%d, 0%o)\n",
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
		_gfs_hook_debug(fprintf(stderr,
			"GFS: Hooking __fchmod: couldn't get gf or dir\n"));
		errno = EBADF; /* XXX - something broken */
		return (-1);			
	}
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __fchmod: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchmod(int filedes, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking _fchmod\n", stderr));
	return (__fchmod(filedes, mode));
}

int
fchmod(int filedes, mode_t mode)
{
	_gfs_hook_debug_v(fputs("Hooking fchmod\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __chown(%s, %d, %d)\n",
				  path, uid, group));

	if (!gfs_hook_is_url(path, &url))
		return (__syscall_chown(path, owner, group));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __chown(%s, %d, %d)\n",
				path, owner, group));
	e = gfs_stat(url, &s);
	free(url);
	if (e == NULL) {
		if (strcmp(s.st_user, gfarm_get_global_username()) != 0)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
		/* XXX - do nothing */
		gfs_stat_free(&s);
	}	
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __chown: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_chown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking _chown\n", stderr));
	return (__chown(path, owner, group));
}

int
chown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking chown\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __lchown(%s, %d, %d)\n",
				  path, uid, group));

	if (!gfs_hook_is_url(path, &url))
		return (__syscall_lchown(path, owner, group));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __lchown(%s, %d, %d)\n",
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
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __lchown: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_lchown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking _lchown\n", stderr));
	return (__lchown(path, owner, group));
}

int
lchown(const char *path, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking lchown\n", stderr));
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
	struct gfs_stat s;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fchown(%d, %d, %d)\n",
				  fd, uid, group));

	if ((gf = gfs_hook_is_open(fd)) == NULL)
		return (__syscall_fchown(fd, owner, group));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __fchown(%d, %d, %d)\n",
				fd, owner, group));
	e = gfs_fstat(gf, &s);
	if (e == NULL) {
		if (strcmp(s.st_user, gfarm_get_global_username()) != 0)
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
		/* XXX - do nothing */
		gfs_stat_free(&s);
	}	
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __fchown: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fchown(int fd, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking _fchown\n", stderr));
	return (__fchown(fd, owner, group));
}

int
fchown(int fd, uid_t owner, gid_t group)
{
	_gfs_hook_debug_v(fputs("Hooking fchown\n", stderr));
	return (__fchown(fd, owner, group));
}

/*
 * rename
 */

int
__rename(const char *oldpath, const char *newpath)
{
	const char *e;
	char *oldurl, *newurl;
	int old_is_url, new_is_url;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __rename(%s, %s)\n",
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
		return (syscall(SYS_rename, oldpath, newpath));
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __rename(%s, %s)\n",
				oldpath, newpath));

	e = gfs_rename(oldurl, newurl);
	free(oldurl);
	free(newurl);
	if (e == NULL)
		return (0);

	_gfs_hook_debug(fprintf(stderr, "GFS: __rename: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_rename(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking _rename\n", stderr));
	return (__rename(oldpath, newpath));
}

int
rename(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking rename\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __symlink(%s, %s)\n",
				  oldpath, newpath));

	if (!gfs_hook_is_url(newpath, &url))
		return (syscall(SYS_symlink, oldpath, newpath));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __symlink(%s, %s)\n",
				oldpath, newpath));

	/*
	 * Gfarm file system does not support the creation of
	 * symbolic link yet.
	 */
	e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
	free(url);

	_gfs_hook_debug(fprintf(stderr, "GFS: __symlink: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_symlink(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking _symlink\n", stderr));
	return (__symlink(oldpath, newpath));
}

int
symlink(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking symlink\n", stderr));
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

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __link(%s, %s)\n",
				  oldpath, newpath));

	if (!gfs_hook_is_url(newpath, &url))
		return (syscall(SYS_link, oldpath, newpath));

	_gfs_hook_debug(fprintf(stderr, "GFS: Hooking __link(%s, %s)\n",
				oldpath, newpath));

	/*
	 * Gfarm file system does not support the creation of
	 * hard link yet.
	 */
	e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* EPERM */
	free(url);

	_gfs_hook_debug(fprintf(stderr, "GFS: __link: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_link(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking _link\n", stderr));
	return (__link(oldpath, newpath));
}

int
link(const char *oldpath, const char *newpath)
{
	_gfs_hook_debug_v(fputs("Hooking link\n", stderr));
	return (__link(oldpath, newpath));
}

/*
 * getxattr
 */
int
getxattr(const char *path, const char *name, void *value, size_t size)
{
	char *e, *gfarm_file;
	char *url;

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking getxattr(%s, %s, %p, %lu)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking getxattr(%s, %s, %p, %lu)\n",
				path, name, value, (unsigned long)size));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: getxattr: %s\n", e));
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

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking lgetxattr(%s, %s, %p, %lu)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking lgetxattr(%s, %s, %p, %lu)\n",
				path, name, value, (unsigned long)size));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: lgetxattr: %s\n", e));
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

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking fgetxattr(%d, %s, %p, %lu)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking fgetxattr(%d, %s, %p, %lu)\n",
				filedes, name, value, (unsigned long)size));

	e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	_gfs_hook_debug(fprintf(stderr, "GFS: fgetxattr: %s\n", e));
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

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking setxattr(%s, %s, %p, %lu, %d)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking setxattr(%s, %s, %p, %lu, %d)\n",
				path, name, value, (unsigned long)size,
				flags));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: setxattr: %s\n", e));
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

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking lsetxattr(%s, %s, %p, %lu, %d)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking lsetxattr(%s, %s, %p, %lu, %d)\n",
				path, name, value, (unsigned long)size, flags));

	e = gfarm_url_make_path(url, &gfarm_file);
	free(url);
	if (e == NULL) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		free(gfarm_file);
	}

	_gfs_hook_debug(fprintf(stderr, "GFS: lsetxattr: %s\n", e));
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

	_gfs_hook_debug_v(fprintf(stderr,
				  "Hooking fsetxattr(%d, %s, %p, %lu, %d)\n",
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

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking fsetxattr(%d, %s, %p, %lu, %d)\n",
				filedes, name, value, (unsigned long)size,
				flags));

	e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	_gfs_hook_debug(fprintf(stderr, "GFS: fsetxattr: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

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

	_gfs_hook_debug_v(fputs("Hooking __mknod\n", stderr));

	if (!gfs_hook_is_url(path, &url))
		return (syscall(SYS_mknod, path, mode, dev));
	
	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking __mknod(%s, %o)\n", path, mode));
	
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
		return 0;
	}
	errno = EINVAL;
	return (-1);
}

int
_mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(fputs("Hooking _mknod\n", stderr));
	return (__mknod(path, mode, dev));
}

int
mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(fputs("Hooking mknod\n", stderr));
	return (__mknod(path, mode, dev));
}

#else /* defined _MKNOD_VER */

#ifdef __linux__

int
__mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(fputs("Hooking __mknod\n", stderr));
	return (__xmknod(_MKNOD_VER, path, mode, &dev));
}

int
_mknod(const char *path, mode_t mode, dev_t dev)
{
	_gfs_hook_debug_v(fputs("Hooking _mknod\n", stderr));
	return (__xmknod(_MKNOD_VER, path, mode, &dev));
}

#endif /* __linux__ */

int
__xmknod(int ver, const char *path, mode_t mode, dev_t *dev)
{
	const char *e;
	char *url;
	struct gfs_stat gs;
	GFS_File gf;

	_gfs_hook_debug_v(fputs("Hooking __xmknod\n", stderr));

	if (!gfs_hook_is_url(path, &url))
#ifdef SYS_xmknod
		return (syscall(SYS_xmknod, ver, path, mode, dev));
#else /* !defined(SYS_xmknod) */
		return (syscall(SYS_mknod, path, mode, *dev));
#endif /* SYS_xmknod */

	_gfs_hook_debug(fprintf(stderr,
				"GFS: Hooking __xmknod(%s, %o)\n", path, mode));
	
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
		return 0;
	}
	errno = EINVAL;
	return (-1);
}

int
_xmknod(int ver, const char *path, mode_t mode, dev_t *dev)
{
	_gfs_hook_debug_v(fputs("Hooking _xmknod\n", stderr));
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

	va_start(ap, cmd);
	val = va_arg(ap, unsigned long);
	va_end(ap);

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fcntl(%d, %d, %x)\n",
					filedes, cmd, val));

#ifdef F_FREESP
	if (cmd == F_FREESP)
		_gfs_hook_debug_v(fprintf(stderr, "flock.l_start:%ld",
			(long)(((struc flock *)val)->l_start)));
#endif
#ifdef F_FREESP64
	if (cmd == F_FREESP64)
		_gfs_hook_debug_v(fprintf(stderr, "flock64.l_start:%ld",
			(long)(((struct flock64 *)val)->l_start)));
#endif

	if (cmd == F_GETFD || cmd == F_SETFD ||
	    (gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_fcntl, filedes, cmd, val));


	_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking __fcntl(%d(%d), %d, %x)\n",
		filedes, gfs_pio_fileno(gf), cmd, val));

	switch (cmd) {
#ifdef F_FREESP
	case F_FREESP:
		if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
			_gfs_hook_debug(fprintf(stderr,
					"GFS: Hooking __fcntl(%d, %d, %x)\n",
						filedes, cmd, val));
			e = GFARM_ERR_IS_A_DIRECTORY;
		}
		e = gfs_pio_truncate(gf, ((struct flock *)val)->l_start);
		break;
#endif
#ifdef F_FREESP64
	case F_FREESP64:
		if (gfs_hook_gfs_file_type(filedes) == GFS_DT_DIR) {
			_gfs_hook_debug(fprintf(stderr,
					"GFS: Hooking __fcntl(%d, %d, %x)\n",
						filedes, cmd, val));
			e = GFARM_ERR_IS_A_DIRECTORY;
		}
		e = gfs_pio_truncate(gf, ((struct flock64 *)val)->l_start);
		break;
#endif
	default:
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
		break;
	}	
	if (e == NULL)
		return (0); 

	_gfs_hook_debug(fprintf(stderr, "GFS: __fcntl: %s\n", e));
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
	_gfs_hook_debug_v(fprintf(stderr, "Hooking fcntl(%d, %d, %x)\n",
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
	_gfs_hook_debug_v(fprintf(stderr, "Hooking fcntl(%d, %d, %x)\n",
				  filedes, cmd, val));
	return (__fcntl(filedes, cmd, val));
}
#endif /* defined(F_FREESP) || defined(F_FREESP64) */

int
__fsync(int filedes)
{
	GFS_File gf;
	char *e;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fsync(%d)\n", fiiledes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_fsync, filedes));

	_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking __fsync(%d(%d))\n",
				filedes, gfs_pio_fileno(gf)));

	e = gfs_pio_sync(gf);
	if (e == NULL)
		return (0); 

	_gfs_hook_debug(fprintf(stderr, "GFS: __fsync: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fsync(int filedes)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking _fsync(%d)\n", filedes));
	return (__fsync(filedes));
}

int
fsync(int filedes)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking fsync(%d)\n", filedes));
	return (__fsync(filedes));
}

int
__fdatasync(int filedes)
{
	GFS_File gf;
	char *e;

	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fdatasync(%d)\n",
				  fiiledes));

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return (syscall(SYS_fdatasync, filedes));

	_gfs_hook_debug(fprintf(stderr,
		"GFS: Hooking __fdatasync(%d(%d))\n",
				filedes, gfs_pio_fileno(gf)));

	e = gfs_pio_datasync(gf);
	if (e == NULL)
		return (0); 

	_gfs_hook_debug(fprintf(stderr, "GFS: __fdatasync: %s\n", e));
	errno = gfarm_error_to_errno(e);
	return (-1);
}

int
_fdatasync(int filedes)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking _fdatasync(%d)\n",
				  filedes));
	return (__fdatasync(filedes));
}

int
fdatasync(int filedes)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking fdatasync(%d)\n", filedes));
	return (__fdatasync(filedes));
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

off_t
gfs_hook_syscall_lseek(int filedes, off_t offset, int whence)
{
#if defined(__NetBSD__)
	return (__syscall((quad_t)SYS_lseek, filedes, 0, offset, whence));
#else
	return (syscall(SYS_lseek, filedes, offset, whence));
#endif
}

#ifndef __linux__
int
gfs_hook_syscall_getdents(int filedes, struct dirent *buf, size_t nbyte)
{
	return (syscall(SYS_getdents, filedes, buf, nbyte));
}
#endif

#ifdef SYS_truncate
int
gfs_hook_syscall_truncate(const char *path, off_t length)
{
	return (syscall(SYS_truncate, path, length));
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
	return (syscall(SYS_ftruncate, filedes, length));
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
#define SYSCALL_GETDENTS(filedes, buf, nbyte) \
	gfs_hook_syscall_getdents(filedes, buf, nbyte)

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

#define FUNC___GETDENTS	__getdents
#define FUNC__GETDENTS	_getdents
#define FUNC_GETDENTS	getdents

#define STRUCT_DIRENT	struct dirent
#define ALIGNMENT 8
#define ALIGN(p) (((unsigned long)(p) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

#include "hooks_common.c"

#undef ALIGNMENT

/* stat */

#define STRUCT_STAT	struct stat
#define GFS_BLKSIZE	8192

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

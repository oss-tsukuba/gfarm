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
	e = gfs_unlink(url);
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
			_gfs_hook_debug(perror("fork"));
			status = 255;
			break;
		case 0:
			r = syscall(SYS_execve, filename, argv, envp);
			if (gfs_hook_num_gfs_files() == 0)
				return (r);
			_gfs_hook_debug(perror(filename));
			_exit(255);
		default:
			while ((r = waitpid(pid, &status, 0)) == -1 &&
			       errno == EINTR);
			if (r == -1) {
				_gfs_hook_debug(perror("waitpid"));
				/* execve fails. call again to set errno. */
				r = syscall(SYS_execve, filename, argv, envp);
				return (r);
			}
			else if (WIFSIGNALED(status)) {
				_gfs_hook_debug(
				 fprintf(stderr, "%s: signal %d received%s.\n",
				  gfarm_host_get_self_name(), WTERMSIG(status),
				  WCOREDUMP(status) ? " (core dumped)" : ""));
			}
			else if (WEXITSTATUS(status) == 255) {
				_gfs_hook_debug(
				 fprintf(stderr, "%s: status 255\n",
				  gfarm_host_get_self_name()));
				/* execve fails. call again to set errno. */
				r = syscall(SYS_execve, filename, argv, envp);
				return (r);
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
 * fork - flush all of iobuffer before fork().  This is necessary to
 * correctly execute a shell script such as 'configure'.
 */
pid_t
__fork(void)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking __fork()\n"));
	/* flush all of buffer */
	gfs_hook_flush_all();
	return syscall(SYS_fork);
}

pid_t
_fork(void)
{
	_gfs_hook_debug_v(fputs("Hooking _fork\n", stderr));
	return (__fork());
}

pid_t
fork(void)
{
	_gfs_hook_debug_v(fputs("Hooking fork\n", stderr));
	return (__fork());
}

#ifdef SYS_vfork
pid_t
__vfork(void)
{
	_gfs_hook_debug_v(fprintf(stderr, "Hooking __vfork()\n"));
	/* flush all of buffer */
	gfs_hook_flush_all();
	/* 
	 * SYS_fork is called instead of SYS_vfork.  This is needed
	 * for linux to avoid segfault in wrong configuration,
	 * e.g. libpthread-not-hidden is not pre-loaded.
	 */
	return syscall(SYS_fork);
}

pid_t
_vfork(void)
{
	_gfs_hook_debug_v(fputs("Hooking _vfork\n", stderr));
	return (__vfork());
}

pid_t
vfork(void)
{
	_gfs_hook_debug_v(fputs("Hooking vfork\n", stderr));
	return (__vfork());
}
#endif
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

#ifdef HAVE_ATTR_XATTR_H
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
#endif /* HAVE_ATTR_XATTR_H */

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

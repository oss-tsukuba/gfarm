/*
 * $Id$
 */

#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h> /* PATH_MAX */

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfs_misc.h"

#include <gfarm/gfs_hook.h>
#include "hooks_subr.h"

#define MAX_GFS_FILE_BUF	2048

struct _gfs_file_descriptor {
	int refcount;
	unsigned char d_type;   /* file types in <gfarm/gfs.h> */
        union {
		GFS_File f;
		struct {
			GFS_Dir dir;
			struct gfs_dirent *suspended;
			file_offset_t suspended_offset;
			struct gfs_stat gst;
			char *abs_url; /* for __fchdir() hook */
		} *d;
	} u;
};
static struct _gfs_file_descriptor *_gfs_file_buf[MAX_GFS_FILE_BUF];

/*
 * static function definitions
 */
static void gfs_hook_disable_hook(void);
static void gfs_hook_enable_hook(void);
static int gfs_hook_check_hook_disabled(void);
static int gfs_hook_init(void);
static void gfs_hook_set_current_view_local(void);
static void gfs_hook_set_current_view_index(int, int);
static void gfs_hook_set_current_view_global(void);
static void gfs_hook_set_current_view_section(char *, int);
static void gfs_hook_set_current_view_default(void);

/*
 *
 */
static void
gfs_hook_not_initialized(void)
{
	static int printed = 0;

	if (!printed) {
		printed = 1;
		gflog_info(
			"fatal error: gfarm_initialize() isn't called");
	}
}

/*
 * open flag management
 */
int
gfs_hook_open_flags_gfarmize(int open_flags)
{
	int gfs_flags;

	switch (open_flags & O_ACCMODE) {
	case O_RDONLY:	gfs_flags = GFARM_FILE_RDONLY; break;
	case O_WRONLY:	gfs_flags = GFARM_FILE_WRONLY; break;
	case O_RDWR:	gfs_flags = GFARM_FILE_RDWR; break;
	default: return (-1);
	}

#if 0 /* this is unnecessary */
	if ((open_flags & O_CREAT) != 0)
		gfs_flags |= GFARM_FILE_CREATE;
#endif
	if ((open_flags & O_TRUNC) != 0)
		gfs_flags |= GFARM_FILE_TRUNC;
	if ((open_flags & O_APPEND) != 0)
		gfs_flags |= GFARM_FILE_APPEND;
	if ((open_flags & O_EXCL) != 0)
		gfs_flags |= GFARM_FILE_EXCLUSIVE;
	/* open(2) and creat(2) should be unbuffered */
	gfs_flags |= GFARM_FILE_UNBUFFERED;
	return (gfs_flags);
}

/*
 * gfs_file_buf management
 *
 * XXX - need to manage list of opened file descriptors exactly to
 * efficiently execute *_all() functions.
 */

static int _gfs_hook_num_gfs_files;
static int _gfs_hook_gfs_files_max_plus_one;

static void
gfs_hook_num_gfs_files_check(void)
{
	/*
	 * The number of file descriptors is not necessary to be
	 * reset even when the process is forked.  Opened files by
	 * the parent process can be accessed by the descendants.
	 */
}

int
gfs_hook_num_gfs_files(void)
{
	gfs_hook_num_gfs_files_check();
	return (_gfs_hook_num_gfs_files);
}

static void
gfs_hook_num_gfs_files_inc(int fd)
{
	gfs_hook_num_gfs_files_check();
	if (fd >= _gfs_hook_gfs_files_max_plus_one)
		_gfs_hook_gfs_files_max_plus_one = fd + 1;
	++_gfs_hook_num_gfs_files;
}

static void
gfs_hook_num_gfs_files_dec(void)
{
	gfs_hook_num_gfs_files_check();
	if (_gfs_hook_num_gfs_files > 0)
		--_gfs_hook_num_gfs_files;
}

/*
 * Reserve several file descriptors for applications.  At least,
 * 'configure' uses 5 and 6.  Maybe, tcsh and zsh also.
 */
#define GFS_HOOK_MIN_FD	10
static int _gfs_hook_reserved_fd[GFS_HOOK_MIN_FD + 1];

void
gfs_hook_reserve_fd()
{
	int i, fd;

	i = 0;
	fd = open("/dev/null", O_WRONLY);
	while (fd >= 0 && fd < GFS_HOOK_MIN_FD) {
		_gfs_hook_reserved_fd[i++] = fd;
		fd = dup(fd);
	}
	_gfs_hook_reserved_fd[i] = -1;
	if (fd >= 0)
		close(fd);
}

void
gfs_hook_release_fd()
{
	int i;

	for (i = 0; _gfs_hook_reserved_fd[i] >= 0; ++i)
		close(_gfs_hook_reserved_fd[i]);
}

/* re-assign a file descriptor greater than MIN_FD */
static int
gfs_hook_adjust_fd(int fd)
{
	int fd2;

	if (fd >= 0 && fd < GFS_HOOK_MIN_FD) {
		fd2 = fcntl(fd, F_DUPFD, GFS_HOOK_MIN_FD);
		close(fd);
		fd = fd2;
	}
	return (fd);
}

int
gfs_hook_insert_gfs_file(GFS_File gf)
{
	int fd, save_errno;
	struct stat st;

	_gfs_hook_debug(gflog_info("GFS: insert_gfs_file: %p", gf));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
	fd = gfs_pio_fileno(gf);
	if (fstat(fd, &st) == -1) {
		save_errno = errno;
		goto error_close_gf;
	}
	if (S_ISREG(st.st_mode))
		fd = fcntl(fd, F_DUPFD, GFS_HOOK_MIN_FD);
	else /* don't return a socket, to make select(2) work with this fd */
		fd = gfs_hook_adjust_fd(open("/dev/null", O_RDWR));
	if (fd == -1) {
		save_errno = errno;
		goto error_close_gf;
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		save_errno = EMFILE;
		goto error_close_fd;
	}
	if (_gfs_file_buf[fd] != NULL) {
		save_errno = EBADF; /* XXX - something broken */
		goto error_close_fd;
	}
	GFARM_MALLOC(_gfs_file_buf[fd]);
	if (_gfs_file_buf[fd] == NULL) {
		save_errno = ENOMEM;
		goto error_close_fd;
	}
	_gfs_file_buf[fd]->refcount = 1;
	_gfs_file_buf[fd]->d_type = GFS_DT_REG;
	_gfs_file_buf[fd]->u.f = gf;
	gfs_hook_num_gfs_files_inc(fd);
	return (fd);

error_close_fd:
	__syscall_close(fd);
error_close_gf:
	gfs_pio_close(gf);

	_gfs_hook_debug(gflog_info("GFS: insert_gfs_file: %s",
		gfarm_errno_to_error(save_errno)));

	errno = save_errno;
	return (-1);
}

int
gfs_hook_insert_gfs_dir(GFS_Dir dir, char *url)
{
	int fd, save_errno;
	char *e, *abs_url;

	_gfs_hook_debug(gflog_info("GFS: insert_gfs_dir: %p", dir));

	/*
	 * A new file descriptor is needed to identify a hooked file
	 * descriptor.
	 */
	fd = gfs_hook_adjust_fd(open("/dev/null", O_RDONLY));
	if (fd == -1) {
		save_errno = errno;
		goto error_closedir;
	}
	if (fd >= MAX_GFS_FILE_BUF) {
		save_errno = EMFILE;
		goto error_close_fd;
	}
	if (_gfs_file_buf[fd] != NULL) {
		save_errno = EBADF; /* XXX - something broken */
		goto error_close_fd;
	}
	e = gfs_realpath(url, &abs_url);
	if (e != NULL) {
		save_errno = gfarm_error_to_errno(e);
		goto error_close_fd;
	}
	GFARM_MALLOC(_gfs_file_buf[fd]);
        if (_gfs_file_buf[fd] == NULL) {
		save_errno = ENOMEM;
		goto error_free_path;
        }
	GFARM_MALLOC(_gfs_file_buf[fd]->u.d);
	if (_gfs_file_buf[fd]->u.d == NULL) {
		save_errno = ENOMEM;
		goto error_free_file_buf;
        }
	e = gfs_stat(url, &_gfs_file_buf[fd]->u.d->gst);
	if (e != NULL) {
		save_errno = gfarm_error_to_errno(e);
		goto error_free_file_buf_u_d;
	}
	_gfs_file_buf[fd]->refcount = 1;
	_gfs_file_buf[fd]->d_type = GFS_DT_DIR;
	_gfs_file_buf[fd]->u.d->dir = dir;
	_gfs_file_buf[fd]->u.d->suspended = NULL;
	_gfs_file_buf[fd]->u.d->suspended_offset = 0;
	_gfs_file_buf[fd]->u.d->abs_url = abs_url;
	return (fd);

error_free_file_buf_u_d:
	free(_gfs_file_buf[fd]->u.d);
error_free_file_buf:
	free(_gfs_file_buf[fd]);
	_gfs_file_buf[fd] = NULL;
error_free_path:
	free(abs_url);
error_close_fd:
	__syscall_close(fd);
error_closedir:
	gfs_closedir(dir);

	_gfs_hook_debug(gflog_info("GFS: insert_gfs_dir: %s",
		gfarm_errno_to_error(save_errno)));

	errno = save_errno;
	return (-1);
}

unsigned char
gfs_hook_gfs_file_type(int fd)
{
	return (_gfs_file_buf[fd]->d_type);
}

char *
gfs_hook_clear_gfs_file(int fd)
{
	GFS_File gf;
	char *e = NULL;

	_gfs_hook_debug(gflog_info("GFS: clear_gfs_file: %d", fd));

	gf = gfs_hook_is_open(fd);
	if (gf == NULL) {
		_gfs_hook_debug(gflog_info(
			"GFS: ERROR: not a Gfarm file: %d", fd));
		return ("not a Gfarm file");
	}

	if (--_gfs_file_buf[fd]->refcount > 0) {
	  	/* fd is duplicated, skip closing the file. */
		_gfs_hook_debug(gflog_info(
					"GFS: clear_gfs_file: skipped"));
	} else {
		if (gfs_hook_gfs_file_type(fd) == GFS_DT_REG) {
			e = gfs_pio_close(gf);
			gfs_hook_num_gfs_files_dec();
		} else if (gfs_hook_gfs_file_type(fd) == GFS_DT_DIR) {
			_gfs_file_buf[fd]->u.d->dir = NULL;
			_gfs_file_buf[fd]->u.d->suspended = NULL;
			_gfs_file_buf[fd]->u.d->suspended_offset = 0;
			gfs_stat_free(&_gfs_file_buf[fd]->u.d->gst);
			free(_gfs_file_buf[fd]->u.d->abs_url); 
			free(_gfs_file_buf[fd]->u.d);
			e = gfs_closedir((GFS_Dir)gf);
		}
		free(_gfs_file_buf[fd]);
	}
	__syscall_close(fd);
	_gfs_file_buf[fd] = NULL;

	if (e != NULL)
		_gfs_hook_debug(
			gflog_info("GFS: clear_gfs_file: %s", e));

	return (e);
}

/*
 * XXX - need to manage list of opened file descriptors exactly to
 * efficiently execute *_all() functions.
 */
void
gfs_hook_unset_calc_digest_all(void)
{
	int fd;
	GFS_File gf;

	for (fd = 0; fd < _gfs_hook_gfs_files_max_plus_one; ++fd)
		if (((gf = gfs_hook_is_open(fd)) != NULL) &&
		    (gfs_hook_gfs_file_type(fd) == GFS_DT_REG))
			gfs_pio_unset_calc_digest(gf);
	return;
}

char *
gfs_hook_flush_all(void)
{
	int fd;
	char *e, *e_save = NULL;
	GFS_File gf;

	for (fd = 0; fd < _gfs_hook_gfs_files_max_plus_one; ++fd) {
		if ((gf = gfs_hook_is_open(fd)) != NULL) {
			e = gfs_pio_flush(gf);
			if (e_save == NULL)
				e_save = e;
		}
	}
	if (e_save != NULL)
		_gfs_hook_debug(
			gflog_info("GFS: hook_flush_all: %s", e_save));

	return (e_save);
}

char *
gfs_hook_close_all(void)
{
	int fd;
	char *e, *e_save = NULL;

	/*
	 * flush stdio buffer.  this is needed especially by flex
	 * that creates the output file via stdout.
	 */
	fflush(stdout);
	fflush(stderr);

	for (fd = 0; fd < _gfs_hook_gfs_files_max_plus_one; ++fd) {
		if (gfs_hook_is_open(fd)) {
			e = gfs_hook_clear_gfs_file(fd);
			if (e_save == NULL)
				e_save = e;
		}
	}
	if (e_save != NULL)
		_gfs_hook_debug(
			gflog_info("GFS: hook_close_all: %s", e_save));

	return (e_save);
}

void
gfs_hook_terminate(void)
{
	char *e;

	gfs_hook_close_all();
	gfs_hook_disable_hook();
	e = gfarm_terminate();
	if (e != NULL)
		_gfs_hook_debug(
		    gflog_info("GFS: gfarm_terminate: %s", e));
}

struct _gfs_file_descriptor *gfs_hook_dup_descriptor(int fd)
{
	if (gfs_hook_is_open(fd) == NULL)
		return (NULL);
	++_gfs_file_buf[fd]->refcount;
	return (_gfs_file_buf[fd]);
}

void gfs_hook_set_descriptor(int fd, struct _gfs_file_descriptor *d)
{
	if (gfs_hook_is_open(fd) != NULL)
		gfs_hook_clear_gfs_file(fd);
	_gfs_file_buf[fd] = d;
}

#if 0
int
gfs_hook_dup_filedes(int oldfd, int newfd)
{
	_gfs_hook_debug(
	   gflog_info("GFS: dpu_filedes: %d, %d", oldfd, newfd));

#if 0
	if (_gfs_file_buf[oldfd] == _gfs_file_buf[newfd])
		return (newfd);		
#endif
		_gfs_file_buf[newfd] = _gfs_file_buf[oldfd];
	}

	return (newfd);
}
#endif

/*  printf and puts should not be put into the following function. */
void *
gfs_hook_is_open(int fd)
{
	if (fd < 0 || fd >= MAX_GFS_FILE_BUF)
		return (NULL);

	if (_gfs_file_buf[fd] == NULL)
		return (NULL);

	switch (gfs_hook_gfs_file_type(fd)) {
	case GFS_DT_REG:
		return (_gfs_file_buf[fd]->u.f);
	case GFS_DT_DIR:
		return (_gfs_file_buf[fd]->u.d->dir);
	default:
		return (NULL);
	}
}

void
gfs_hook_set_suspended_gfs_dirent(int fd,
	struct gfs_dirent *entry, file_offset_t offset)
{
	_gfs_file_buf[fd]->u.d->suspended = entry;
	_gfs_file_buf[fd]->u.d->suspended_offset = offset;
}

struct gfs_dirent *
gfs_hook_get_suspended_gfs_dirent(int fd, file_offset_t *offsetp)
{
	struct gfs_dirent *ent = _gfs_file_buf[fd]->u.d->suspended;

	if (ent == NULL)
		return (NULL);
	*offsetp = _gfs_file_buf[fd]->u.d->suspended_offset;
	return (ent);
}

struct gfs_stat *
gfs_hook_get_gfs_stat(int fd)
{
	return (&_gfs_file_buf[fd]->u.d->gst);
}

char *
gfs_hook_get_gfs_url(int fd)
{
	return (_gfs_file_buf[fd]->u.d->abs_url);
}

/*
 *  Check the current working directory is included in Gfarm file system.
 */
static int _gfs_hook_cwd_is_gfarm = -1;

int
gfs_hook_set_cwd_is_gfarm(int c)
{
	return (_gfs_hook_cwd_is_gfarm = c);
}

static void
gfs_hook_check_cwd_is_gfarm(void)
{
	char *cwd;
	/*
	 * Honor 'PWD', which is set by bash or tcsh with Gfarm
	 * syscall hook library, if it points to a path in Gfarm file
	 * system.
	 */
	cwd = getenv("PWD");
	if (cwd != NULL) {
		char *url;

		if (gfs_hook_is_url(cwd, &url)) {
			gfs_chdir(url); /* here, cwd is changed. */
			gfs_hook_set_cwd_is_gfarm(1);
			free(url);
			return;
		}
	}
	gfs_hook_set_cwd_is_gfarm(0);
}

int
gfs_hook_get_cwd_is_gfarm(void)
{
	if (_gfs_hook_cwd_is_gfarm == -1)
		gfs_hook_check_cwd_is_gfarm();
	else {
		/*
		 * hook functions can be called even after
		 * gfarm_terminate() is called.  If initialization
		 * succeeds, go ahead.  Otherwise, set 0.
		 */
		if (!gfs_hook_init())
			gfs_hook_set_cwd_is_gfarm(0);
	}
	return (_gfs_hook_cwd_is_gfarm);
}

/*
 *  Check whether pathname is gfarm url or not.
 *
 *  Gfarm URL:  gfarm:[:section:]pathname
 *
 *  It is necessary to free the memory space for *url.
 */
static char gfs_mntdir[] = "/gfarm";
static char *received_prefix = NULL;

static int
gfs_hook_is_null_or_slash(const char c)
{
	return (c == '\0' || c == '/');
}

static int
gfs_hook_is_null_or_slash_or_colon(const char c)
{
	return (gfs_hook_is_null_or_slash(c) || c == ':');
}

static int
gfs_hook_is_mount_point0(const char *path)
{
	return (*path == '/' &&
		memcmp(path, gfs_mntdir, sizeof(gfs_mntdir) - 1) == 0 &&
		gfs_hook_is_null_or_slash_or_colon(
			path[sizeof(gfs_mntdir) - 1]));
}

static int
gfs_hook_is_mount_point(const char *path, int *sizeof_prefix)
{
	const char *p = path;
	int ret;

	while (*p == '/')
		++p;
	if (p > path)
		--p;
	ret = gfs_hook_is_mount_point0(p);
	if (ret && sizeof_prefix)
		*sizeof_prefix = sizeof(gfs_mntdir) + p - path;
	return (ret);
}

static int
gfs_hook_strmatchlen(const char *s1, const char *s2, size_t n)
{
	int c = 0;

	while (s1[c] && s2[c] && s1[c] == s2[c] && c < n)
		++c;
	return (c);
}

/* strncmp(s1 + '/' + s2, t1, n) */
static int
gfs_hook_strncmp2(const char *s1, const char *s2, const char *t1, size_t n)
{
	int m;

	m = gfs_hook_strmatchlen(s1, t1, n);
	if (m == n)
		return (0);
	if (s1[m])
		return (s1[m] - t1[m]);
	t1 += m;
	n -= m;
	if (m > 0 && s1[m - 1] != '/') {
		const char *slash = "/";
		m = gfs_hook_strmatchlen(slash, t1, n);
		if (m == n)
			return (0);
		if (slash[m])
			return (slash[m] - t1[m]);
		t1 += m;
		n -= m;
	}
	m = gfs_hook_strmatchlen(s2, t1, n);
	if (m == n)
		return (0);
	return (s2[m] - t1[m]);
}	

static const char *
gfs_hook_is_mount_point_relative(const char *path)
{
	char cwd[PATH_MAX + 1], *r;
	int mntdirlen, cwdlen;

	gfs_hook_disable_hook();
	r = getcwd(cwd, sizeof(cwd));
	gfs_hook_enable_hook();
	if (r == NULL)
		return (NULL);

	/* XXX - neither '..' nor '.' is properly handled. */
	mntdirlen = sizeof(gfs_mntdir) - 1;
	cwdlen = strlen(cwd);
	if (*path != '/'
	    && gfs_hook_strncmp2(cwd, path, gfs_mntdir, mntdirlen) == 0
	    && gfs_hook_is_null_or_slash_or_colon(path[mntdirlen - cwdlen]))
		return (&path[mntdirlen - cwdlen]);
	else
		return (NULL);
}

static int
set_received_prefix(const char *path)
{
	char *end, *p;
	int len;

	if (received_prefix != NULL) {
		free(received_prefix);
		received_prefix = NULL;
	}
	if (gfs_hook_is_mount_point(path, NULL)) {
		received_prefix = strdup(gfs_mntdir);
		if (received_prefix == NULL)
			return (0); /* XXX - should return ENOMEM */
		return (1);
	}
	/* path is either 'gfarm:' or 'gfarm@'. */
	if ((end = strchr(path, ':')) == NULL)
		end = strchr(path, '@');
	if (end == NULL)
		return (0); /* XXX */

	len = end - path + 1;
	GFARM_MALLOC_ARRAY(p, len + 1);
	if (p == NULL)
		return (0); /* XXX - should return ENOMEM */
	received_prefix = p;
	strncpy(received_prefix, path, len);
	received_prefix[len] = '\0';
	return (1);
}

/*
 * bypassing mechanism for hooking library
 */
static int gfs_hook_is_disabled = 0;

static void
gfs_hook_disable_hook(void)
{
	gfs_hook_is_disabled = 1;
}

static void
gfs_hook_enable_hook(void)
{
	gfs_hook_is_disabled = 0;
}

static int
gfs_hook_check_hook_disabled(void)
{
	return (gfs_hook_is_disabled);
}

static int
gfs_hook_init(void)
{
	static int gfs_hook_initialized = 0;

	if (gfs_hook_initialized && gfarm_initialized())
		return (1);

	gfs_hook_disable_hook();
	if (gfs_hook_initialize() != NULL) {
		gfs_hook_not_initialized();
		return (0); /* don't perform gfarm operation */
	}
	gfs_hook_enable_hook();
	gfs_hook_initialized = 1;
	return (1);
}

/*
 * Gfarm path starts with '<mount_point>', 'gfarm:' or 'gfarm@'.
 *
 * When the Gfarm path starts with '<mount_point>', the path is
 * considered to be an absolute path instead of a relative path.
 *
 * '<mount_point>/~username' such as '/gfarm/~tatebe' can be used to
 * specify a home directory in Gfarm file system.
 *
 * '/gfarm' is a special point to force to uncache directory cache.
 */
int
gfs_hook_is_url(const char *path, char **urlp)
{
	static char prefix[] = "gfarm:";
	int sizeof_gfarm_prefix = sizeof(prefix);
	int sizeof_prefix = sizeof_gfarm_prefix;
	const char *path_save;
	int errno_save = errno;
	int is_mount_point = 0, remove_slash = 0, add_slash = 0;
	int add_dot_slash = 0;
	/*
	 * ROOT patch:
	 *   'gfarm@' is also considered as a Gfarm URL
	 */
	static char gfarm_url_prefix_for_root[] = "gfarm@";
	char *sec = NULL;

	if (gfs_hook_check_hook_disabled())
		return (0);

	/* 'Bad address' check to prevent segmentation fault */
	gfs_hook_disable_hook();
	if ((access(path, F_OK) == -1 && errno == EFAULT) || *path == '\0') {
		gfs_hook_enable_hook();
		errno = errno_save;
		return (0);
	}
	gfs_hook_enable_hook();

	path_save = path;
	if (gfs_hook_is_mount_point(path, &sizeof_prefix))
		is_mount_point = 1;
	if (is_mount_point || gfarm_is_url(path) ||
	    /* ROOT patch */
	    memcmp(path, gfarm_url_prefix_for_root,
	    sizeof(gfarm_url_prefix_for_root) - 1) == 0) {
		if (!gfs_hook_init()) {
			errno = errno_save;
			return (0); /* don't perform gfarm operation */
		}
		/*
		 * extension for accessing individual sections
		 *   gfarm::section:pathname
		 */
		path += sizeof_prefix - 1;
		if (path[0] == ':') {
			const char *p = path + 1;
			int secsize = strcspn(p, "/:");
			int urlsize;

			if (p[secsize] != ':') {
				errno = errno_save;
				return (0); /* gfarm::foo/:bar or gfarm::foo */
			}
			/*
			 * '/gfarm/~' and '/gfarm/.' will be translated
			 * to 'gfarm:~' and 'gfarm:.', respectively.
			 */
			if (is_mount_point && p[secsize + 1] == '/' &&
			    (p[secsize + 2] == '~' ||
			     (p[secsize + 2] == '.' &&
			      gfs_hook_is_null_or_slash(p[secsize + 3]))))
				remove_slash = 1;
			/* '/gfarm' will be translated to 'gfarm:/'. */
			if (is_mount_point && p[secsize + 1] == '\0') {
				add_slash = 1;
				/*gfs_uncachedir();*/
			}
			urlsize = sizeof_gfarm_prefix - 1 + add_slash
				+ strlen(p + secsize + remove_slash + 1);
			GFARM_MALLOC_ARRAY(*urlp, urlsize + 1);
			GFARM_MALLOC_ARRAY(sec, secsize + 1);
			if (*urlp == NULL || sec == NULL) {
				if (*urlp != NULL)
					free(*urlp);
				if (sec != NULL)
					free(sec);
				errno = errno_save;
				return (0); /* XXX - should return ENOMEM */
			}
			memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
			if (add_slash)
				strcpy(*urlp + sizeof_gfarm_prefix - 1, "/");
			strcpy(*urlp + sizeof_gfarm_prefix - 1 + add_slash,
			       p + secsize + remove_slash + 1);
			memcpy(sec, p, secsize);
			sec[secsize] = '\0';
			/* It is not necessary to free memory space of 'sec'. */
			gfs_hook_set_current_view_section(sec, 1);
		}
		else {
			/*
			 * '/gfarm/~' and '/gfarm/.' will be translated
			 * to 'gfarm:~' and 'gfarm:.', respectively.
			 */
			if (is_mount_point && path[0] == '/' &&
			    (path[1] == '~' ||
			     (path[1] == '.' &&
			      gfs_hook_is_null_or_slash(path[2]))))
				remove_slash = 1;
			/* '/gfarm' will be translated to 'gfarm:/'. */
			if (is_mount_point && path[0] == '\0') {
				add_slash = 1;
				/*gfs_uncachedir();*/
			}
			GFARM_MALLOC_ARRAY(*urlp, 
				sizeof_gfarm_prefix - 1 + add_slash
				+ strlen(path + remove_slash) + 1);
			if (*urlp == NULL) {
				errno = errno_save;
				return (0) ; /* XXX - should return ENOMEM */
			}
			/*
			 * the reason why we don't just call strcpy(*url, path)
			 * is because the path may be "gfarm@path/name".
			 * (ROOT patch)
			 */
			memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
			if (add_slash)
				strcpy(*urlp + sizeof_gfarm_prefix - 1, "/");
			strcpy(*urlp + sizeof_gfarm_prefix - 1 + add_slash,
			       path + remove_slash);

			gfs_hook_set_current_view_default();
		}
		if (!set_received_prefix(path_save)) {
			errno = errno_save;
			return (0);
		}
		return (1);
	}
	/* The current directory is in the Gfarm file system */
	if (*path_save != '/' && gfs_hook_get_cwd_is_gfarm()) {
		/*
		 * gfarm_initialize() should be called in
		 * gfs_hook_get_cwd_is_gfarm() if it returns 1.
		 */
		/* just '~filename' should be 'gfarm:./~filename' */
		if (*path_save == '~')
			add_dot_slash = 2;
		GFARM_MALLOC_ARRAY(*urlp, 
			sizeof_gfarm_prefix + add_dot_slash +
			strlen(path_save));
		if (*urlp == NULL) {
			errno = errno_save;
			return (0) ; /* XXX - should return ENOMEM */
		}
		memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
		if (add_dot_slash)
			strcpy(*urlp + sizeof_gfarm_prefix - 1, "./");
		strcpy(*urlp + sizeof_gfarm_prefix - 1 + add_dot_slash,
		       path_save);
		/* It is not necessary to change the current view. */
		return (1);
	}
	/* The current directory is *not* in the Gfarm file system */
	if (*path_save != '/'
	    && (path = gfs_hook_is_mount_point_relative(path_save))) {
		if (!gfs_hook_init()) {
			errno = errno_save;
			return (0); /* don't perform gfarm operation */
		}
		/*
		 * '/gfarm/~' and '/gfarm/.' will be translated
		 * to 'gfarm:~' and 'gfarm:.', respectively.
		 */
		if (path[0] == '/' &&
		    (path[1] == '~' ||
		     (path[1] == '.' && gfs_hook_is_null_or_slash(path[2]))))
			remove_slash = 1;
		/* '/gfarm' will be translated to 'gfarm:/'. */
		if (path[0] == '\0') {
			add_slash = 1;
			/*gfs_uncachedir();*/
		}
		GFARM_MALLOC_ARRAY(*urlp,
			sizeof_gfarm_prefix - 1 + add_slash
			+ strlen(path + remove_slash) + 1);
		if (*urlp == NULL) {
			errno = errno_save;
			return (0) ; /* XXX - should return ENOMEM */
		}
		memcpy(*urlp, prefix, sizeof_gfarm_prefix - 1);
		if (add_slash)
			strcpy(*urlp + sizeof_gfarm_prefix - 1, "/");
		strcpy(*urlp + sizeof_gfarm_prefix - 1 + add_slash,
		       path + remove_slash);
		/* It is not necessary to change the current view. */
		return (1);
	}
	errno = errno_save;
	return (0);
}

char *
gfs_hook_get_prefix(char *buf, size_t size)
{
	if (received_prefix == NULL)
		return GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING;
	if (size <= strlen(received_prefix))
		return GFARM_ERR_NUMERICAL_RESULT_OUT_OF_RANGE;
	strcpy(buf, received_prefix);
	return (NULL);
}	

/*
 * default and current file view manipulation
 */
static enum gfs_hook_file_view _gfs_hook_default_view = local_view;
static int _gfs_hook_default_index = 0;
static int _gfs_hook_default_nfrags = GFARM_FILE_DONTCARE;
static char *_gfs_hook_default_section = NULL;

static enum gfs_hook_file_view _gfs_hook_current_view = local_view;
static int _gfs_hook_current_index = 0;
static int _gfs_hook_current_nfrags = GFARM_FILE_DONTCARE;
static char *_gfs_hook_current_section = NULL;

void
gfs_hook_set_default_view_local(void)
{
	_gfs_hook_default_view = local_view;
}

void
gfs_hook_set_default_view_index(int index, int nfrags)
{
	_gfs_hook_default_view = index_view;
	_gfs_hook_default_index = index;
	_gfs_hook_default_nfrags = nfrags;
}

void
gfs_hook_set_default_view_global(void)
{
	_gfs_hook_default_view = global_view;
}

void
gfs_hook_set_default_view_section(char *section)
{
	_gfs_hook_default_view = section_view;
	if (_gfs_hook_default_section != NULL)
		free(_gfs_hook_current_section);
	_gfs_hook_default_section = strdup(section);
}

static void
gfs_hook_set_current_view_local(void)
{
	_gfs_hook_current_view = local_view;
}

static void
gfs_hook_set_current_view_index(int index, int nfrags)
{
	_gfs_hook_current_view = index_view;
	_gfs_hook_current_index = index;
	_gfs_hook_current_nfrags = nfrags;
}

static void
gfs_hook_set_current_view_global(void)
{
	_gfs_hook_current_view = global_view;
}

static void
gfs_hook_set_current_view_section(char *section, int needfree)
{
	static int space_need_to_be_freeed;
	_gfs_hook_current_view = section_view;
	if (_gfs_hook_current_section != NULL && space_need_to_be_freeed)
		free(_gfs_hook_current_section);
	space_need_to_be_freeed = needfree;
	_gfs_hook_current_section = section;
}

static void
gfs_hook_set_current_view_default(void)
{
	switch (_gfs_hook_default_view) {
	case local_view:
		gfs_hook_set_current_view_local();
		break;
	case index_view:
		gfs_hook_set_current_view_index(
			_gfs_hook_default_index, _gfs_hook_default_nfrags);
		break;
	case global_view:
		gfs_hook_set_current_view_global();
		break;
	case section_view:
		gfs_hook_set_current_view_section(_gfs_hook_default_section, 0);
		break;
	}
}

enum gfs_hook_file_view
gfs_hook_get_current_view(void)
{
	return (_gfs_hook_current_view);
}

int
gfs_hook_get_current_index(void)
{
	return (_gfs_hook_current_index);
}

int
gfs_hook_get_current_nfrags(void) {
	return (_gfs_hook_current_nfrags);
}

char *
gfs_hook_get_current_section(void) {
	return (_gfs_hook_current_section);
}

/*
 * gfs_hook_set_view
 */
char *
gfs_hook_set_view_local(int filedes, int flag)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_local(gf, flag)) != NULL) {
		_gfs_hook_debug(gflog_info(
			"GFS: set_view_local: %s", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_index(int filedes, int nfrags, int index, 
			char *host, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_index(gf, nfrags, index, host, flags))
	    != NULL) {
		_gfs_hook_debug(gflog_info(
			"GFS: set_view_index: %s", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_global(int filedes, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_global(gf, flags)) != NULL) {
		_gfs_hook_debug(gflog_info(
			"GFS: set_view_global: %s", e));
		return e;
	}
	return NULL;
}

char *
gfs_hook_set_view_section(int filedes, char *section, char *host, int flags)
{
	GFS_File gf;
	char *e;

	if ((gf = gfs_hook_is_open(filedes)) == NULL)
		return "not a Gfarm file";

	if ((e = gfs_pio_set_view_section(gf, section, host, flags)) != NULL) {
		_gfs_hook_debug(gflog_info(
			"GFS: set_view_section: %s", e));
		return (e);
	}
	return (NULL);
}

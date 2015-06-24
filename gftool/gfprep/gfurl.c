/*
 * $Id$
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfarm_path.h"
#include "lookup.h"

#include "gfmsg.h"
#include "gfurl.h"

struct gfurl {
	char *original_path;  /* malloc'ed */
	char *url;  /* malloc'ed */
	const char *path;  /* remove scheme and host:port */
	const char *epath; /* effective path, for functions */
	int scheme;
	struct gfurl_functions func;
};

int
gfurl_vasprintf(char **strp, const char *format, va_list ap)
{
	int n, size = 32;
	char *p, *np;
	va_list aq;

	GFARM_MALLOC_ARRAY(p, size);
	if (p == NULL)
		return (-1);
	for (;;) {
		va_copy(aq, ap);
		n = vsnprintf(p, size, format, aq);
		va_end(aq);
		if (n > -1 && n < size) {
			*strp = p;
			return (strlen(p));
		}
		if (n > -1) /* glibc 2.1 */
			size = n + 1;
		else /* n == -1: glibc 2.0 */
			size *= 2;
		GFARM_REALLOC_ARRAY(np, p, size);
		if (np == NULL) {
			free(p);
			*strp = NULL;
			return (-1);
		} else
			p = np;
	}
}

int
gfurl_asprintf(char **strp, const char *format, ...)
{
	va_list ap;
	int retv;

	va_start(ap, format);
	retv = gfurl_vasprintf(strp, format, ap);
	va_end(ap);
	return (retv);
}

char *
gfurl_asprintf2(const char *format, ...)
{
	va_list ap;
	int retv;
	char *str;

	va_start(ap, format);
	retv = gfurl_vasprintf(&str, format, ap);
	va_end(ap);
	if (retv == -1)
		return (NULL);
	return (str);
}

/* malloc'ed */
char *
gfurl_path_dirname(const char *path)
{
	char *tmp = strdup(path), *dpath;

	if (tmp == NULL)
		return (NULL);
	dpath = strdup(dirname(tmp));
	free(tmp);
	return (dpath);
}

/* malloc'ed */
char *
gfurl_path_basename(const char *path)
{
	char *tmp = strdup(path), *bname;

	if (tmp == NULL)
		return (NULL);
	bname = strdup(basename(tmp));
	free(tmp);
	return (bname);
}

/* //abc -> /abc */
static const char *
path_skip_slash(const char *path)
{
	const char *p = path;

	if (*p != '/')
		return (p);

	while (*p == '/')
		p++;
	p--;
	return (p);
}

/* malloc'ed */
char *
gfurl_path_combine(const char *path, const char *name)
{
	int retv;
	char *str;
	const char *n = path_skip_slash(name);

	if (*n == '\0')
		retv = gfurl_asprintf(&str, "%s", path);
	else
		retv = gfurl_asprintf(&str, "%s%s%s",
		    path, (path[strlen(path) - 1] == '/' ? "" : "/"), n);
	if (retv == -1)
		return (NULL);
	return (str);
}

static gfarm_error_t
gfurl_local_realpath(const char *path, char **realpathp)
{
	gfarm_error_t e;
	char *rv, *parent, *p;
	const char *base;
	int overflow = 0;
	size_t len;
	char buf[PATH_MAX];

	rv = realpath(path, buf);
	if (rv) {
		gfmsg_debug("realpath(%s)=%s", path, rv);
		*realpathp = strdup(rv);
		if (*realpathp == NULL)
			return (GFARM_ERR_NO_MEMORY);
		return (GFARM_ERR_NO_ERROR);
	}
	if (errno != ENOENT) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug("realpath(%s): %s", path, gfarm_error_string(e));
		return (e);
	}
	if (path[0] == '\0' || /* "" */
	    (path[0] == '/' && path[1] == '\0') || /* "/" */
	    (path[0] == '.' && (path[1] == '\0' || /* "." */
			(path[1] == '.' && path[2] == '\0')))) /* ".." */
		return (GFARM_ERR_INVALID_ARGUMENT); /* unexpected */

	/* to create new entry */
	parent = gfarm_path_dir(path); /* malloc'ed */
	if (parent == NULL)
		return (GFARM_ERR_NO_MEMORY);
	p = realpath(parent, buf);
	if (p == NULL) {
		e = gfarm_errno_to_error(errno);
		gfmsg_debug("realpath(%s): %s", parent, gfarm_error_string(e));
		free(parent);
		return (e);
	}
	free(parent);
	base = gfarm_path_dir_skip(path);
	gfmsg_debug("dirname=%s, basename=%s", buf, base);
	len = gfarm_size_add(&overflow, strlen(p), strlen(base));
	if (overflow)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	len = gfarm_size_add(&overflow, len, 2); /* '/' and '\0' */
	if (overflow)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	GFARM_MALLOC_ARRAY(rv, len);
	if (rv == NULL)
		return (GFARM_ERR_NO_MEMORY);
	strcpy(rv, p);
	if (strcmp(p, "/") != 0)
		strcat(rv, "/");
	strcat(rv, base);
	*realpathp = rv;
	return (GFARM_ERR_NO_ERROR);
}

static GFURL
gfurl_alloc(const char *path)
{
	gfarm_error_t e;
	char *p, *url;
	size_t len;
	int overflow = 0;
	GFURL u;

	GFARM_MALLOC(u);
	if (u == NULL)
		return (NULL);

	u->original_path = strdup(path);
	if (u->original_path == NULL) {
		free(u);
		return (NULL);
	}
	u->url = NULL;
	u->path = NULL;
	u->epath = NULL;
	u->scheme = GFURL_SCHEME_UNKNOWN;

	if (gfurl_path_is_local(path))
		u->scheme = GFURL_SCHEME_LOCAL;
	else if (gfurl_path_is_gfarm(path))
		u->scheme = GFURL_SCHEME_GFARM;
	else if (gfurl_path_is_hpss(path))
		u->scheme = GFURL_SCHEME_HPSS;

	if (u->scheme != GFURL_SCHEME_UNKNOWN) {
		u->url = strdup(path);
		if (u->url == NULL) {
			free(u->original_path);
			free(u);
			return (NULL);
		}
		return (u);
	}

	e = gfarm_realpath_by_gfarm2fs(path, &u->url);
	if (e == GFARM_ERR_NO_ERROR) {
		u->scheme = GFURL_SCHEME_GFARM;
		return (u); /* gfarm://host:port/... */
	}

	e = gfurl_local_realpath(path, &p);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error("gfarm_local_realpath: %s: %s",
		    path, gfarm_error_string(e));
		exit(EXIT_FAILURE); /* exit */
	}
	/* file://...<real>...\0 */
	len = gfarm_size_add(&overflow, strlen(p),
	    GFURL_LOCAL_PREFIX_LENGTH + 2 + 1);
	if (overflow)
		gfmsg_fatal("gfurl_alloc: overflow"); /* exit */
	GFARM_MALLOC_ARRAY(url, len);
	if (url == NULL) {
		free(u->original_path);
		free(u);
		return (NULL);
	}
	strcpy(url, GFURL_LOCAL_PREFIX);
	strcat(url, "//");
	strcat(url, p);
	free(p);
	u->url = url;
	u->scheme = GFURL_SCHEME_LOCAL;
	return (u);
}

GFURL
gfurl_init(const char *path)
{
	GFURL u = gfurl_alloc(path);

	if (u == NULL)
		return (NULL);

	if (u->scheme == GFURL_SCHEME_LOCAL) {
		u->func = gfurl_func_local;
		u->path = path_skip_slash(u->url + GFURL_LOCAL_PREFIX_LENGTH);
		u->epath = u->path;
	} else if (u->scheme == GFURL_SCHEME_GFARM) {
		u->func = gfurl_func_gfarm;

		if (strcmp(u->url, "gfarm:") == 0 ||
		    strcmp(u->url, "gfarm:/") == 0 ||
		    strcmp(u->url, "gfarm://") == 0) {
			free(u->url);
			u->url = strdup("gfarm:///");
			if (u->url == NULL) {
				gfurl_free(u);
				return (NULL);
			}
		}

		u->path = path_skip_slash(
		    gfarm_url_prefix_hostname_port_skip(u->url));
		u->epath = u->url;
	} else if (u->scheme == GFURL_SCHEME_HPSS) {
		u->func = gfurl_func_hpss;
		u->path = path_skip_slash(u->url + GFURL_HPSS_PREFIX_LENGTH);
		u->epath = u->path;
	} else {
		gfmsg_error("unexpected URL scheme");
		gfurl_free(u);
		return (NULL);
	}

	return (u);
}

GFURL
gfurl_dup(GFURL u)
{
	return (gfurl_init(u->url));
}

void
gfurl_free(GFURL u)
{
	if (u != NULL) {
		free(u->original_path);
		free(u->url);
		free(u);
	}
}

const char *
gfurl_url(GFURL u)
{
	return (u->url);
}

const char *
gfurl_epath(GFURL u)
{
	return (u->epath);
}

static const char *
gfurl_scheme_string(GFURL u)
{
	const char *p;
	const char unknown[] = "unknown:";

	switch (u->scheme) {
	case GFURL_SCHEME_LOCAL:
		p = GFURL_LOCAL_PREFIX;
		break;
	case GFURL_SCHEME_GFARM:
		p = GFARM_URL_PREFIX;
		break;
	case GFURL_SCHEME_HPSS:
		p = GFURL_HPSS_PREFIX;
		break;
	default:
		p = unknown;
	}
	return (p);
}

static char *
gfurl_dirurl_str(GFURL url)
{
	char *dpath, *durl;

	dpath = gfurl_path_dirname(url->path);
	if (dpath == NULL)
		return (NULL);
	durl = gfurl_asprintf2("%s//%s", gfurl_scheme_string(url), dpath);
	free(dpath);
	return (durl);
}

GFURL
gfurl_parent(GFURL url)
{
	GFURL parent;
	char *durl;

	if (url->scheme == GFURL_SCHEME_GFARM)
		durl = gfarm_url_dir(url->url);
	else
		durl = gfurl_dirurl_str(url);
	if (durl == NULL)
		return (NULL);

	parent = gfurl_init(durl);
	if (parent == NULL)
		return (NULL);
	free(durl);
	return (parent);
}

GFURL
gfurl_child(GFURL url, const char *name)
{
	char *str;
	GFURL returl;

	str = gfurl_path_combine(url->url, name);
	if (str == NULL)
		return (NULL);
	returl = gfurl_init(str);
	free(str);
	return (returl);
}

int
gfurl_is_rootdir(GFURL u)
{
	return (strcmp(u->path, "/") == 0);
}

int
gfurl_is_local(GFURL u)
{
	return (u->scheme == GFURL_SCHEME_LOCAL);
}

int
gfurl_is_gfarm(GFURL u)
{
	return (u->scheme == GFURL_SCHEME_GFARM);
}

int
gfurl_is_hpss(GFURL u)
{
	return (u->scheme == GFURL_SCHEME_HPSS);
}

int
gfurl_is_same_gfmd(GFURL src, GFURL dst)
{
	gfarm_error_t e;
	char *src_hostname, *dst_hostname;
	int src_port, dst_port;
	int retv;

	if (src == NULL || dst == NULL)
		return (0);  /* different */
	if (src->scheme != dst->scheme)
		return (0);  /* different */

	e = gfarm_get_hostname_by_url(src->url, &src_hostname, &src_port);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_error_e(e, "gfarm_get_hostname_by_url(%s)", src->url);
		return (1); /* error */
	}
	e = gfarm_get_hostname_by_url(dst->url, &dst_hostname, &dst_port);
	if (e != GFARM_ERR_NO_ERROR) {
		free(src_hostname);
		gfmsg_error_e(e, "gfarm_get_hostname_by_url(%s)", dst->url);
		return (1); /* error */
	}
	if (src_port != dst_port) {
		retv = 0; /* different */
		goto end;
	} else if (strcmp(src_hostname, dst_hostname) != 0) {
		retv = 0; /* different */
		goto end;
	}
	/* same */
	retv = 1;
end:
	free(src_hostname);
	free(dst_hostname);
	return (retv);
}

int
gfurl_is_same_dir(GFURL src, GFURL dst)
{
	gfarm_error_t e;
	struct stat st;
	struct gfs_stat gst;
	int retv;
	gfarm_ino_t g_src_ino, g_dst_ino;
	gfarm_uint64_t g_src_gen, g_dst_gen;

	if (src->scheme != dst->scheme)
		return (0); /* different dir */
	else if (gfurl_is_local(src)) {
		char *d = (char *)dst->path, *d2, *d_tmp, *d_tmp2;
		ino_t src_ino;
		dev_t src_dev;

		assert(gfurl_is_local(dst));

		retv = lstat(src->path, &st);
		if (retv == -1) {
			gfmsg_error("lstat(%s): %s",
			    src->path, strerror(errno));
			return (1); /* unexpected */
		}
		src_ino = st.st_ino;
		src_dev = st.st_dev;
		d_tmp = strdup(d);
		if (d_tmp == NULL) {
			gfmsg_error("no memory");
			return (1); /* error */
		}
		for (;;) {
			retv = lstat(d, &st);
			if (retv == -1) {
				free(d_tmp);
				gfmsg_error("lstat(%s): %s",
					d, strerror(errno));
				return (1); /* unexpected */
			}
			if (src_dev == st.st_dev &&
			    src_ino == st.st_ino) {
				free(d_tmp);
				return (1); /* same dir */
			}
			d_tmp2 = strdup(d);
			if (d_tmp2 == NULL) {
				free(d_tmp);
				gfmsg_fatal("no memory");
			}
			d2 = dirname(d_tmp2);
			if (strcmp(d, d2) == 0) {
				free(d_tmp);
				free(d_tmp2);
				break;
			}
			free(d_tmp); /* not use d */
			d_tmp = d_tmp2;
			d = d2;
		}
		return (0); /* different dir */
	} else if (gfurl_is_hpss(src)) {
		assert(gfurl_is_hpss(dst));
		gfmsg_fatal("assert: unsupported HPSS source URL");
	}
	assert(gfurl_is_gfarm(src));
	assert(gfurl_is_gfarm(dst));

	if (!gfurl_is_same_gfmd(src, dst))
		return (0); /* different */

	/* same gfmd */
	e = gfs_lstat(src->url, &gst);
	if (e != GFARM_ERR_NO_ERROR) {
		gfmsg_fatal_e(e, "gfs_lstat(%s)", src->url);
		return (1); /* unexpected */
	} else {
		char *d = strdup(dst->url), *d2;

		if (d == NULL) {
			gfmsg_fatal("no memory");
			return (1); /* error */
		}
		g_src_ino = gst.st_ino;
		g_src_gen = gst.st_gen;
		gfs_stat_free(&gst);
		for (;;) {
			e = gfs_lstat(d, &gst);
			if (e != GFARM_ERR_NO_ERROR) {
				gfmsg_fatal_e(e, "gfs_lstat(%s)", d);
				free(d);
				return (1); /* unexpected */
			}
			g_dst_ino = gst.st_ino;
			g_dst_gen = gst.st_gen;
			gfs_stat_free(&gst);
			if (g_src_ino == g_dst_ino && g_src_gen == g_dst_gen) {
				free(d);
				return (1); /* same dir */
			}
			d2 = gfarm_url_dir(d);
			if (d2 == NULL)
				gfmsg_fatal("no memory");
			if (strcmp(d, d2) == 0) {
				free(d);
				free(d2);
				return (0); /* different dir */
			}
			free(d);
			d = d2;
		}
	}
}

gfarm_error_t
gfurl_lstat(GFURL u, struct gfurl_stat *stp)
{
	return (u->func.lstat(u->epath, stp));
}

gfarm_error_t
gfurl_exist(GFURL u)
{
	return (u->func.exist(u->epath));
}

int
gfurl_stat_file_type(struct gfurl_stat *stp)
{
	if (GFARM_S_ISREG(stp->mode))
		return (GFS_DT_REG);
	else if (GFARM_S_ISDIR(stp->mode))
		return (GFS_DT_DIR);
	else if (GFARM_S_ISLNK(stp->mode))
		return (GFS_DT_LNK);
	return (GFS_DT_UNKNOWN);
}

gfarm_error_t
gfurl_lutimens(
	GFURL u, struct gfarm_timespec *atimep,
	struct gfarm_timespec *mtimep)
{
	assert(atimep != NULL);
	assert(mtimep != NULL);
	return (u->func.lutimens(u->epath, atimep, mtimep));
}

gfarm_error_t
gfurl_set_mtime(GFURL u, struct gfarm_timespec *mtimep)
{
	if (mtimep == NULL)
		return (GFARM_ERR_NO_ERROR);
	return (u->func.lutimens(u->epath, mtimep, mtimep));
}

/* do not call this for symlinks */
gfarm_error_t
gfurl_chmod(GFURL u, int mode)
{
	return (u->func.chmod(u->epath, mode));
}

gfarm_error_t
gfurl_mkdir(GFURL u, int mode, int skip_existing)
{
	return (u->func.mkdir(u->epath, mode, skip_existing));
}

gfarm_error_t
gfurl_rmdir(GFURL u)
{
	return (u->func.rmdir(u->epath));
}

gfarm_error_t
gfurl_unlink(GFURL u)
{
	return (u->func.unlink(u->epath));
}

gfarm_error_t
gfurl_readlink(GFURL u, char **targetp)
{
	return (u->func.readlink(u->epath, targetp));
}

gfarm_error_t
gfurl_symlink(GFURL u, char *target)
{
	return (u->func.symlink(u->epath, target));
}

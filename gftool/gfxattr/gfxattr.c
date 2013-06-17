/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <strings.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>
#include "gfutil.h"

#include "gfarm_path.h"

#define DEFAULT_ALLOC_SIZE (64 * 1024)

static gfarm_error_t
set_xattr(int xmlMode, int nofollow, char *path, char *xattrname,
	char *filename, int flags)
{
	const size_t count = 65536;
	ssize_t sz;
	size_t buf_sz, msg_sz = 0;
	char *buf = NULL, *tbuf;
	gfarm_error_t e;
	int fd, need_close = 0, save_errno;
	int overflow;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	tbuf = NULL;
#endif

	if (filename != NULL) {
		fd = open(filename, O_RDONLY);
		need_close = 1;
	} else
		fd = STDIN_FILENO;
	if (fd == -1) {
		save_errno = errno;
		if (filename != NULL)
			fprintf(stderr, "%s: %s\n", filename,
				strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	buf_sz = count;
	overflow = 0;
	buf_sz = gfarm_size_add(&overflow, buf_sz, 1);
	if (!overflow)
		buf = malloc(buf_sz);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);

	while ((sz = read(fd, buf + msg_sz, count)) > 0) {
		msg_sz += sz;
		buf_sz = gfarm_size_add(&overflow, buf_sz, count);
		if (!overflow)
			tbuf = realloc(buf, buf_sz);
		if (overflow || (tbuf == NULL)) {
			e = GFARM_ERR_NO_MEMORY;
			goto free_buf;
		}
		buf = tbuf;
	}
	buf[msg_sz] = '\0';
	if (need_close)
		close(fd);

	if (xmlMode) {
		e = (nofollow ? gfs_lsetxmlattr : gfs_setxmlattr)
			(path, xattrname, buf, msg_sz + 1, flags);
	} else {
		e = (nofollow ? gfs_lsetxattr : gfs_setxattr)
			(path, xattrname, buf, msg_sz, flags);
	}
free_buf:
	free(buf);
	return (e);
}

static gfarm_error_t
get_xattr_alloc(int xmlMode, int nofollow, char *path, char *xattrname,
		void **valuep, size_t *size)
{
	gfarm_error_t e;
	void *value;

	value = malloc(*size);
	if (value == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (xmlMode)
		e = (nofollow ? gfs_lgetxmlattr : gfs_getxmlattr)
			(path, xattrname, value, size);
	else
		e = (nofollow ? gfs_lgetxattr : gfs_getxattr)
			(path, xattrname, value, size);

	if (e == GFARM_ERR_NO_ERROR)
		*valuep = value;
	else
		free(value);
	return (e);
}

static gfarm_error_t
get_xattr(int xmlMode, int nofollow, char *path, char *xattrname,
	char *filename)
{
	gfarm_error_t e;
	FILE *f;
	int need_close = 0, save_errno;
	void *value = NULL;
	size_t size, wsize;

	size = DEFAULT_ALLOC_SIZE;
	e = get_xattr_alloc(xmlMode, nofollow, path, xattrname, &value, &size);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = get_xattr_alloc(xmlMode, nofollow, path, xattrname, &value,
			&size);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (filename != NULL) {
		f = fopen(filename, "w");
		need_close = 1;
	} else
		f = stdout;
	if (f == NULL) {
		save_errno = errno;
		free(value);
		if (filename != NULL)
			fprintf(stderr, "%s: %s\n", filename,
				strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	wsize = fwrite(value, 1, size, f);
	if (wsize != size) {
		perror("fwrite");
	}
	fflush(f);
	if (need_close)
		fclose(f);
	free(value);
	return (e);
}

static gfarm_error_t
remove_xattr(int xmlMode, int nofollow, char *path, char *xattrname)
{
	gfarm_error_t e;

	if (xmlMode) {
		e = (nofollow ? gfs_lremovexmlattr : gfs_removexmlattr)
			(path, xattrname);
	} else {
		e = (nofollow ? gfs_lremovexattr : gfs_removexattr)
			(path, xattrname);
	}
	return (e);
}

static gfarm_error_t
list_xattr_alloc(int xmlMode, int nofollow, char *path, char **listp,
	size_t *size)
{
	gfarm_error_t e;
	char *list;

	list = malloc(*size);
	if (list == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (xmlMode)
		e = (nofollow ? gfs_llistxmlattr : gfs_listxmlattr)
			(path, list, size);
	else
		e = (nofollow ? gfs_llistxattr : gfs_listxattr)
			(path, list, size);

	if (e == GFARM_ERR_NO_ERROR)
		*listp = list;
	else
		free(list);
	return (e);
}

static gfarm_error_t
list_xattr(int xmlMode, int nofollow, char *path)
{
	gfarm_error_t e;
	char *list = NULL, *base, *p, *last;
	size_t size;

	size = DEFAULT_ALLOC_SIZE;
	e = list_xattr_alloc(xmlMode, nofollow, path, &list, &size);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = list_xattr_alloc(xmlMode, nofollow, path, &list, &size);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	base = list;
	last = base + size;
	while ((base < last) && ((p = strchr(base, '\0')) != NULL)) {
		printf("%s\n", base);
		base = p + 1;
	}
	free(list);
	return (e);
}

void
usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [ -s | -g | -r | -l ]"
#ifdef ENABLE_XMLATTR
		" [ -x ]"
#endif
		" [ -c | -m ]"
		" [ -f xattrfile ] [ -h ] file [xattrname]\n", prog_name);
	fprintf(stderr, "\t-s\tset extended attribute\n");
	fprintf(stderr, "\t-g\tget extended attribute\n");
	fprintf(stderr, "\t-r\tremove extended attribute\n");
	fprintf(stderr, "\t-l\tlist extended attribute\n");
#ifdef ENABLE_XMLATTR
	fprintf(stderr, "\t-x\thandle XML extended attribute\n");
#endif
	fprintf(stderr, "\t-c\tfail if xattrname already exists "
		"(use with -s)\n");
	fprintf(stderr, "\t-m\tfail if xattrname does not exist "
		"(use with -s)\n");
	fprintf(stderr, "\t-h\tprocess symbolic link instead of "
		"any referenced file\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = basename(argv[0]);
	char *filename = NULL, *c_path, *c_realpath = NULL, *xattrname = NULL;
	enum { NONE, SET_MODE, GET_MODE, REMOVE_MODE, LIST_MODE } mode = NONE;
	int c, xmlMode = 0, nofollow = 0, flags = 0;
	gfarm_error_t e;
	const char *opts = "f:gsrlcmh?"
#ifdef ENABLE_XMLATTR
		"x"
#endif
		;

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'f':
			filename = optarg;
			break;
#ifdef ENABLE_XMLATTR
		case 'x':
			xmlMode = 1;
			break;
#endif
		case 'g':
			mode = GET_MODE;
			break;
		case 's':
			mode = SET_MODE;
			break;
		case 'r':
			mode = REMOVE_MODE;
			break;
		case 'l':
			mode = LIST_MODE;
			break;
		case 'c':
			if (flags == 0)
				flags = GFS_XATTR_CREATE;
			else
				usage(prog_name);
			break;
		case 'm':
			if (flags == 0)
				flags = GFS_XATTR_REPLACE;
			else
				usage(prog_name);
			break;
		case 'h':
			nofollow = 1;
			break;
		case '?':
		default:
			usage(prog_name);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1 || mode == NONE)
		usage(prog_name);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_realpath_by_gfarm2fs(argv[0], &c_realpath);
	if (e == GFARM_ERR_NO_ERROR)
		c_path = c_realpath;
	else
		c_path = argv[0];
	if (argc > 1) {
		xattrname = argv[1];
	}

	switch (mode) {
	case SET_MODE:
		if (argc != 2)
			usage(prog_name);
		e = set_xattr(xmlMode, nofollow, c_path, xattrname, filename,
			flags);
		break;
	case GET_MODE:
		if (argc != 2)
			usage(prog_name);
		e = get_xattr(xmlMode, nofollow, c_path, xattrname, filename);
		break;
	case REMOVE_MODE:
		if (argc != 2)
			usage(prog_name);
		e = remove_xattr(xmlMode, nofollow, c_path, xattrname);
		break;
	case LIST_MODE:
		if (argc != 1)
			usage(prog_name);
		e = list_xattr(xmlMode, nofollow, c_path);
		break;
	default:
		usage(prog_name);
		break;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		exit(1);
	}
	free(c_realpath);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}
	exit(0);
}

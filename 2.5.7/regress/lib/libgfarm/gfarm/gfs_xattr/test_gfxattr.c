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

#define DEFAULT_ALLOC_SIZE (64 * 1024)

static gfarm_error_t
set_xattr(int xmlMode, char *path, char *xattrname, char *filename, int flags)
{
	const size_t count = 65536;
	ssize_t sz;
	size_t buf_sz, msg_sz = 0;
	char *buf = NULL, *tbuf;
	gfarm_error_t e;
	int fd, need_close = 0;
	int overflow;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	tbuf = NULL;
#endif
	if (filename != NULL) {
		fd = open(filename, O_RDONLY);
		need_close = 1;
	}
	else
		fd = STDIN_FILENO;
	if (fd == -1) {
		if (filename != NULL)
			fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		return (gfarm_errno_to_error(errno));
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
		e = gfs_setxmlattr(path, xattrname, buf, msg_sz + 1, flags);
	} else {
		e = gfs_setxattr(path, xattrname, buf, msg_sz, flags);
	}
free_buf:
	free(buf);
	return (e);
}

static gfarm_error_t
get_xattr_alloc(int xmlMode, char *path, char *xattrname,
		void **valuep, size_t *size)
{
	gfarm_error_t e;
	void *value;

	value = malloc(*size);
	if (value == NULL)
		return GFARM_ERR_NO_ERROR;

	if (xmlMode)
		e = gfs_getxmlattr(path, xattrname, value, size);
	else
		e = gfs_getxattr(path, xattrname, value, size);

	if (e == GFARM_ERR_NO_ERROR)
		*valuep = value;
	else
		free(value);
	return e;
}

static gfarm_error_t
get_xattr(int xmlMode, char *path, char *xattrname, char *filename)
{
	gfarm_error_t e;
	FILE *f;
	int need_close = 0;
	void *value = NULL;
	size_t size, wsize;

	size = DEFAULT_ALLOC_SIZE;
	e = get_xattr_alloc(xmlMode, path, xattrname, &value, &size);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = get_xattr_alloc(xmlMode, path, xattrname, &value, &size);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (filename != NULL) {
		f = fopen(filename, "w");
		need_close = 1;
	}
	else
		f = stdout;
	if (f == NULL) {
		free(value);
		if (filename != NULL)
			fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		return (gfarm_errno_to_error(errno));
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
remove_xattr(int xmlMode, char *path, char *xattrname)
{
	gfarm_error_t e;

	if (xmlMode) {
		e = gfs_removexmlattr(path, xattrname);
	} else {
		e = gfs_removexattr(path, xattrname);
	}
	return (e);
}

static gfarm_error_t
list_xattr_alloc(int xmlMode, char *path, char **listp, size_t *size)
{
	gfarm_error_t e;
	char *list;

	list = malloc(*size);
	if (list == NULL)
		return GFARM_ERR_NO_MEMORY;

	if (xmlMode)
		e = gfs_listxmlattr(path, list, size);
	else
		e = gfs_listxattr(path, list, size);

	if (e == GFARM_ERR_NO_ERROR)
		*listp = list;
	else
		free(list);
	return e;
}

static gfarm_error_t
list_xattr(int xmlMode, char *path)
{
	gfarm_error_t e;
	char *list = NULL, *base, *p, *last;
	size_t size;

	size = DEFAULT_ALLOC_SIZE;
	e = list_xattr_alloc(xmlMode, path, &list, &size);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = list_xattr_alloc(xmlMode, path, &list, &size);
	}
	if (e != GFARM_ERR_NO_ERROR)
		return e;

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
		" [ -f xattrfile] [-n xattrname] path...\n", prog_name);
	fprintf(stderr, "\t-s\tset extended attribute\n");
	fprintf(stderr, "\t-g\tget extended attribute\n");
	fprintf(stderr, "\t-r\tremove extended attribute\n");
	fprintf(stderr, "\t-l\tlist extended attribute\n");
#ifdef ENABLE_XMLATTR
	fprintf(stderr, "\t-x\thandle XML extended attribute\n");
#endif
	fprintf(stderr, "\t-c\tfail if xattrname already exists (use with -s)\n");
	fprintf(stderr, "\t-m\tfail if xattrname does not exist (use with -s)\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = basename(argv[0]);
	char *filename = NULL, *c_path = NULL, *xattrname = NULL;
	enum { NONE, SET_MODE, GET_MODE, REMOVE_MODE, LIST_MODE } mode = NONE;
	int c, i, xmlMode = 0, flags = 0, ret = 0;
	gfarm_error_t e;
	const char *opts = "f:n:gsrlcmh?"
#ifdef ENABLE_XMLATTR
		"x"
#endif
		;

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 'n':
			xattrname = optarg;
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

	if (mode != LIST_MODE && xattrname == NULL)
		usage(prog_name);

	for (i = 0; i < argc; i++) {
		c_path = argv[i];

		switch (mode) {
		case SET_MODE:
			e = set_xattr(xmlMode, c_path, xattrname, filename,
			    flags);
			break;
		case GET_MODE:
			e = get_xattr(xmlMode, c_path, xattrname, filename);
			break;
		case REMOVE_MODE:
			e = remove_xattr(xmlMode, c_path, xattrname);
			break;
		case LIST_MODE:
			e = list_xattr(xmlMode, c_path);
			break;
		default:
			usage(prog_name);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n",
			    c_path, gfarm_error_string(e));
			ret = 1;
		}
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}
	exit(ret);
}

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
#include <limits.h>

#include <gfarm/gfarm.h>
#include "gfutil.h"
#include "gfarm_path.h"

static gfarm_error_t
read_file(char *filename, char **bufp)
{
	gfarm_error_t e;
	int fd;
	struct stat st;
	ssize_t sz, msg_sz = 0;
	char *buf = NULL;
	size_t allocsz;
	int overflow;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return (gfarm_errno_to_error(errno));

	if (fstat(fd, &st) != 0)
		e = gfarm_errno_to_error(errno);
	else {
		overflow = 0;
		allocsz = gfarm_size_add(&overflow, st.st_size, 1);
		if (!overflow)
			buf = malloc(allocsz);
		if (buf != NULL) {
			while ((sz = read(fd, buf + msg_sz, st.st_size - msg_sz)) > 0) {
				msg_sz += sz;
			}
			if (sz == 0) {
				buf[msg_sz] = '\0';
				*bufp = buf;
				e = GFARM_ERR_NO_ERROR;
			} else
				e = gfarm_errno_to_error(errno);
		} else
			e = GFARM_ERR_NO_MEMORY;
	}

	close(fd);

	return e;
}

void
usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [ -d depth ] [ -F delim ] "
			"{expr | -f expr-file-path} path\n",
		prog_name);
	fprintf(stderr, "\t-d\tsearch directory depth (>= 0)\n");
	fprintf(stderr, "\t-F\tdelimiter of path and attrname (default is TAB)\n");
	fprintf(stderr, "\t-f\tXPath script filename\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = basename(argv[0]);
	int c, depth = INT_MAX;
	char *filename = NULL, *path, *realpath = NULL, *expr, *delim = "\t";
	struct gfs_xmlattr_ctx *ctxp = NULL;
	char *fpath, *attrname;
	gfarm_error_t e;
	int ret = 0;
#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	path = expr = NULL;
#endif

	while ((c = getopt(argc, argv, "d:f:F:h?")) != -1) {
		switch (c) {
		case 'd':
			depth = atoi(optarg);
			if (depth < 0)
				usage(prog_name);
			break;
		case 'f':
			filename = optarg;
			break;
		case 'F':
			delim = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage(prog_name);
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}
	if (filename == NULL) {
		if (argc < 2)
			usage(prog_name);
		expr = argv[0];
		e = gfarm_realpath_by_gfarm2fs(argv[1], &realpath);
		if (e == GFARM_ERR_NO_ERROR)
			path = realpath;
		else
			path = argv[1];
	} else if (argc != 1) {
		usage(prog_name);
	} else {
		e = read_file(filename, &expr);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n", prog_name,
					gfarm_error_string(e), filename);
			exit(1);
		}
		e = gfarm_realpath_by_gfarm2fs(argv[0], &realpath);
		if (e == GFARM_ERR_NO_ERROR)
			path = realpath;
		else
			path = argv[0];
	}

	e = gfs_findxmlattr(path, expr, depth, &ctxp);
	if (filename != NULL)
		free(expr);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		exit(1);
	}
	free(realpath);

	while ((e = gfs_getxmlent(ctxp, &fpath, &attrname))
			== GFARM_ERR_NO_ERROR) {
		if (fpath == NULL)
			break;
		printf("%s%s%s\n", fpath, delim, attrname);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		ret = 1;
	}

	e = gfs_closexmlattr(ctxp);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		ret = 1;
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}

	return ret;
}

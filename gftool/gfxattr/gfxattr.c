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

gfarm_error_t
set_xattr(char *path, char *filename)
{
	const size_t count = 65536;
	struct gfarm_path_info_xattr attr;
	ssize_t sz, buf_sz, msg_sz = 0;
	char *buf, *tbuf;
	gfarm_error_t e;
	int fd, need_close = 0;

	if (filename != NULL) {
		fd = open(filename, O_RDONLY);
		need_close = 1;
	}
	else
		fd = STDIN_FILENO;
	if (fd == -1)
		return (gfarm_errno_to_error(errno));

	buf_sz = count;
	buf = malloc(buf_sz + 1 /* for the last '\0' */);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);

	while ((sz = read(fd, buf, count)) > 0) {
		msg_sz += sz;
		buf_sz += count;
		tbuf = realloc(buf, buf_sz);
		if (tbuf == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto free_buf;
		}
		buf = tbuf;
	}
	buf[msg_sz] = '\0';
	if (need_close)
		close(fd);

	attr.pathname = path;
	attr.xattr = buf;
	e = gfarm_path_info_xattr_set(&attr);
	if (e == GFARM_ERR_ALREADY_EXISTS)
		e = gfarm_path_info_xattr_replace(&attr);
free_buf:
	free(buf);
	return (e);
}

gfarm_error_t
get_xattr(char *path, char *filename)
{
	struct gfarm_path_info_xattr attr;
	gfarm_error_t e;
	FILE *f;
	int need_close = 0;

	attr.pathname = path;
	e = gfarm_path_info_xattr_get(path, &attr);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (filename != NULL) {
		f = fopen(filename, "w");
		need_close = 1;
	}
	else
		f = stdout;
	if (f == NULL)
		return (gfarm_errno_to_error(errno));

	fprintf(f, "%s", attr.xattr);
	fflush(f);
	if (need_close)
		fclose(f);
	gfarm_path_info_xattr_free(&attr);
	return (e);
}

void
usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [ -s | -g | -r ]"
		" [ -f xattrfile ] file\n", prog_name);
	fprintf(stderr, "\t-s\tset extended metadeata\n");
	fprintf(stderr, "\t-g\tget extended metadeata\n");
	fprintf(stderr, "\t-r\tremove extended metadeata\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = basename(argv[0]);
	char c, *filename = NULL, *c_path;
	enum { NONE, SET_MODE, GET_MODE, REMOVE_MODE } mode = NONE;
	gfarm_error_t e;

	while ((c = getopt(argc, argv, "f:ghsr?")) != -1) {
		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 'g':
			mode = GET_MODE;
			break;
		case 's':
			mode = SET_MODE;
			break;
		case 'r':
			mode = REMOVE_MODE;
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
		fprintf(stderr, "%s: %s\n", prog_name, e);
		exit(1);
	}

	e = gfarm_url_make_path(*argv, &c_path);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n",
			*argv, gfarm_error_string(e)), exit(1);

	switch (mode) {
	case SET_MODE:
		if ((e = gfs_access(*argv, W_OK)) == GFARM_ERR_NO_ERROR)
			e = set_xattr(c_path, filename);
		break;
	case GET_MODE:
		if ((e = gfs_access(*argv, R_OK)) == GFARM_ERR_NO_ERROR)
			e = get_xattr(c_path, filename);
		break;
	case REMOVE_MODE:
		if ((e = gfs_access(*argv, W_OK)) == GFARM_ERR_NO_ERROR)
			e = gfarm_path_info_xattr_remove(c_path);
		break;
	default:
		usage(prog_name);
	}
	free(c_path);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}
	exit(0);
}

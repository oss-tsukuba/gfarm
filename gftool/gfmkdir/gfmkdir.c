/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_path.h"

char *program_name = "gfmkdir";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-p] directory...\n", program_name);
	exit(1);
}

static gfarm_error_t
gfs_mkdir_p(char *path, gfarm_mode_t mode)
{
	gfarm_error_t e;
	struct gfs_stat sb;
	char *parent, *realpath = NULL;
	int is_dir = 0;

	e = gfarm_realpath_by_gfarm2fs(path, &realpath);
	if (e == GFARM_ERR_NO_ERROR)
		path = realpath;
	parent = gfarm_url_dir(path);
	if (parent == NULL) {
		free(realpath);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfs_stat(parent, &sb);
	if (e == GFARM_ERR_NO_ERROR) {
		is_dir = GFARM_S_ISDIR(sb.st_mode);
		gfs_stat_free(&sb);
	} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		e = gfs_mkdir_p(parent, mode);
		is_dir = 1;
	}
	free(parent);
	if (e != GFARM_ERR_NO_ERROR)
		;
	else if (!is_dir)
		e = GFARM_ERR_ALREADY_EXISTS;
	else if ((e = gfs_mkdir(path, mode)) == GFARM_ERR_ALREADY_EXISTS &&
	    gfs_stat(path, &sb) == GFARM_ERR_NO_ERROR &&
	    GFARM_S_ISDIR(sb.st_mode))
		e = GFARM_ERR_NO_ERROR;
	free(realpath);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	int option_parent = 0;
	char *path = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "hp?")) != -1) {
		switch (c) {
		case 'p':
			option_parent = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0)
		usage();

	for (i = 0; i < argc; i++) {
		if (option_parent)
			e = gfs_mkdir_p(argv[i], 0755);
		else {
			e = gfarm_realpath_by_gfarm2fs(argv[i], &path);
			if (e == GFARM_ERR_NO_ERROR)
				argv[i] = path;
			e = gfs_mkdir(argv[i], 0755);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
		free(path);
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

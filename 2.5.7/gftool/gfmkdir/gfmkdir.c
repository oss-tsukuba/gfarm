/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

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
	char *parent = gfarm_url_dir(path);
	int is_dir = 0;

	if (parent == NULL)
		return (GFARM_ERR_NO_MEMORY);
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
		return (e);
	if (!is_dir)
		return (GFARM_ERR_ALREADY_EXISTS);

	e = gfs_mkdir(path, mode);
	if (e == GFARM_ERR_ALREADY_EXISTS &&
	    gfs_stat(path, &sb) == GFARM_ERR_NO_ERROR &&
	    GFARM_S_ISDIR(sb.st_mode))
		e = GFARM_ERR_NO_ERROR;
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	int option_parent = 0;

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
		else
			e = gfs_mkdir(argv[i], 0755);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

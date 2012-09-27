/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_path.h"

char *program_name = "gfrmdir";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s directory...\n", program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	char *path = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
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
		e = gfarm_realpath_by_gfarm2fs(argv[i], &path);
		if (e == GFARM_ERR_NO_ERROR)
			argv[i] = path;
		e = gfs_rmdir(argv[i]);
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

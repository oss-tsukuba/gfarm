/*
 * $Id: gfmv.c 2640 2006-05-29 22:38:29Z soda $
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_path.h"

char *program_name = "gfln";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-s] target link_name\n", program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	int opt_symlink = 0;
	char *oldpath = NULL, *newpath = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "sh?")) != -1) {
		switch (c) {
		case 's':
			opt_symlink = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage();

	if (!opt_symlink) {
		e = gfarm_realpath_by_gfarm2fs(argv[0], &oldpath);
		if (e == GFARM_ERR_NO_ERROR)
			argv[0] = oldpath;
	}
	e = gfarm_realpath_by_gfarm2fs(argv[1], &newpath);
	if (e == GFARM_ERR_NO_ERROR)
		argv[1] = newpath;
	e = (opt_symlink ? gfs_symlink : gfs_link)(argv[0], argv[1]);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s%s %s %s: %s\n",
		    program_name, opt_symlink ? " -s" : "",
		    argv[0], argv[1], gfarm_error_string(e));
		status = 1;
	}
	free(oldpath);
	free(newpath);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

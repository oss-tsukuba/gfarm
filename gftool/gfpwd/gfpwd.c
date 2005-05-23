/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

char *program_name = "gfpwd";

int
main(int argc, char **argv)
{
	char *e, *path;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfs_realpath(".", &path);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	printf("%s\n", path);
	free(path);
	return (0);
}

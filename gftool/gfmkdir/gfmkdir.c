/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfs_client.h"

char *program_name = "gfmkdir";

static void
usage()
{
	fprintf(stderr, "Usage: %s directory...\n", program_name);
	exit(1);
}

struct args {
	char *path;
	gfarm_int32_t mode;
};

char *
gfmkdir(struct gfs_connection *gfs_server, void *args)
{
	struct args *a = args;

	return (gfs_client_mkdir(gfs_server, a->path, a->mode));
}

int
main(int argc, char **argv)
{
	char *e, *canonic_path;
	int i; 

	if (argc <= 1)
		usage();
	program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	for (i = 1; i < argc; i++) {
		struct args a;

		e = gfarm_canonical_path_for_creation(argv[i], &canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		a.path = canonic_path;
		a.mode = 0755;
		e = gfs_client_apply_all_hosts(gfmkdir, &a, program_name);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		free(canonic_path);
	}
	e = gfarm_terminate();
	return (0);
}

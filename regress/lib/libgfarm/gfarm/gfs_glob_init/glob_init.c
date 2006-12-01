#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfs_glob_t types;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 1) {
		fprintf(stderr, "Usage: glob_init\n");
		return (2);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	gfs_glob_free(&types);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

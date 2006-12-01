#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfs_glob_t types;
	int i;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc < 2) {
		fprintf(stderr, "Usage: glob_add <type> [<type> ...]\n");
		return (2);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_glob_init: %s\n", gfarm_error_string(e));
		return (2);
	}
	for (i = 1; i < argc; i++) {
		e = gfs_glob_add(&types, atol(argv[i]));
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s\n", gfarm_error_string(e));
			return (1);
		}	
	}
	for (i = 0; i < gfs_glob_length(&types); i++) {
		printf("%d", gfs_glob_elem(&types, i));
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

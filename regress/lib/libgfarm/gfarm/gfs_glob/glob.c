#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int i;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 2) {
		fprintf(stderr, "Usage: glob <pattern>\n");
		return (2);
	}
	e = gfarm_stringlist_init(&paths);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_stringlist_init: %s\n",
			gfarm_error_string(e));
		return (2);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_glob_init: %s\n", gfarm_error_string(e));
		return (2);
	}
	e = gfs_glob(argv[1], &paths, &types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	gfs_glob_free(&types);
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		printf("%s", gfarm_stringlist_elem(&paths, i));
	}
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

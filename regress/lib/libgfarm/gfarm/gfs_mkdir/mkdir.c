#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	struct gfs_stat gst;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 3) {
		fprintf(stderr, "Usage: mkdir <gfarm_url> <mode>\n");
		return (2);
	}
	e = gfs_mkdir(argv[1], strtoul(argv[2], NULL, 0));
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	e = gfs_stat(argv[1], &gst);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_stat: %s\n", gfarm_error_string(e));
		return (2);
	}
	printf("%o", gst.st_mode);
	e = gfarm_terminate();
	gfs_stat_free(&gst);

	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

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

	if (argc != 2 && argc != 6) {
		fprintf(stderr, "Usage: utimes <gfarm_url> [ <atime_sec> <atime_nsec> <mtime_sec> <mtime_nsec> ]\n");
		return (2);
	}
	e = gfs_stat(argv[1], &gst);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_stat: %s\n", gfarm_error_string(e));
		return (2);
	}
	if (argc == 2) {
		e = gfs_utimes(argv[1], NULL);
	} else {
		struct gfarm_timespec gt[2];

		gt[0].tv_sec = strtoul(argv[2], NULL, 0);
		gt[0].tv_nsec= strtoul(argv[3], NULL, 0);
		gt[1].tv_sec = strtoul(argv[4], NULL, 0);
		gt[1].tv_nsec= strtoul(argv[5], NULL, 0);
		e = gfs_utimes(argv[1], gt);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	e = gfs_stat(argv[1], &gst);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_stat: %s\n", gfarm_error_string(e));
		return (2);
	}
	printf("%u%u%u%u",
	       gst.st_atimespec.tv_sec, gst.st_atimespec.tv_nsec,
	       gst.st_mtimespec.tv_sec, gst.st_mtimespec.tv_nsec);
	gfs_stat_free(&gst);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

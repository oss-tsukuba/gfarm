#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 5) {
		fprintf(stderr,
		    "Usage: file_busy <gfarm_url> <open_flags> "
		    "<dst_host> <replicate_flags>\n");
		return (2);
	}
	e = gfs_pio_open(argv[1], strtol(argv[2], NULL, 0), &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_open: %s\n", gfarm_error_string(e));
		return (2);
	}
	e = gfs_replicate_file_to_request(argv[1], argv[3],
	    strtol(argv[4], NULL, 0));
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_replicate_file_to_request: %s\n",
		    gfarm_error_string(e));
		return (1);
	}
	e = gfs_pio_close(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_close: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

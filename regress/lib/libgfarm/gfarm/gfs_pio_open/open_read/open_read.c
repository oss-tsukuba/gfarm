#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	char buffer[8192];
	int rv;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 3) {
		fprintf(stderr,
		    "Usage: open_read <gfarm_url> <open_flags>\n");
		return (2);
	}
	e = gfs_pio_open(argv[1], strtol(argv[2], NULL, 0), &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_open: %s\n", gfarm_error_string(e));
		return (2);
	}

	for (;;) {
		e = gfs_pio_read(gf, buffer, sizeof buffer, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_pio_read: %s\n",
			    gfarm_error_string(e));
			return (2);
		}
		if (rv == 0)
			break;
		fwrite(buffer, 1, rv, stdout);
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

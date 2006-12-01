#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	file_offset_t result;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 5) {
		fprintf(stderr, "Usage: seek <gfarm_url> <offset> <whence> <return position?>\n");
		return (2);
	}
	e = gfs_pio_create(argv[1],
			   GFARM_FILE_RDWR|GFARM_FILE_TRUNC, 0666, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_create: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	e = gfs_pio_seek(gf, atol(argv[2]), atol(argv[3]),
		strcmp(argv[4], "RETURN_RESULT") == 0 ? &result : NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	printf("%lld\n", result);
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

	if (argc != 3) {
		fprintf(stderr, "Usage: pio_eof <gfarm_url> "
				"<read/non-read>\n");
		return (2);
	}
	e = gfs_pio_create(argv[1],
			   GFARM_FILE_RDWR|GFARM_FILE_TRUNC, 0666, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_create: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	if (strcmp(argv[2], "read") == 0) {
		char buf;
		int n;

		e = gfs_pio_read(gf, &buf, 1, &n);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_pio_read: %s\n",
				gfarm_error_string(e));
			return (2);
			
		}
	}	
	printf("%d\n", gfs_pio_eof(gf));
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

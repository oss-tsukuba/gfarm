#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	int i;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 3) {
		fprintf(stderr, "Usage: sync <gfarm_url> <string>\n");
		return (2);
	}

	e = gfs_pio_create(argv[1],
			   GFARM_FILE_RDWR|GFARM_FILE_TRUNC, 0666, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_create: %s\n", gfarm_error_string(e));
		return (2);
	}
	for (i = 0; i < strlen(argv[2]); i++) {
		e = gfs_pio_putc(gf, argv[2][i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_pio_putc: %s\n",
				gfarm_error_string(e));
			return (2);
		}	
	}
	e = gfs_pio_sync(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	return (0);
}

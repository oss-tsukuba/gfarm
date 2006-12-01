#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_File gf;
	char *bufp = NULL;
	size_t size;
	size_t len;
	
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (2);
	}

	if (argc != 5) {
		fprintf(stderr, "Usage: readdelim <gfarm_url> <bufsize> <delimiter> <delimlen>\n");
		return (2);
	}
	e = gfs_pio_create(argv[1],
			   GFARM_FILE_RDWR|GFARM_FILE_TRUNC, 0666, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_create: %s\n",
		    gfarm_error_string(e));
		return (2);
	}
	size = atol(argv[2]); 
	if (size > 0) {
		bufp = malloc(size);
		if (bufp == NULL) {
			fprintf(stderr, "%s\n",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (2);
		}	
	}
	e = gfs_pio_readdelim(gf, &bufp, &size, &len, argv[3], atol(argv[4]));
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	printf("%s:%d:%d\n", bufp, size, len);
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

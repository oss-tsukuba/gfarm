#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

char *program_name = "size0";

void
usage(void)
{
	fprintf(stderr, "Usage: %s <path> <xattr>\n", program_name);
	exit(2);
}

int
main(int argc, char **argv)
{
	int c, do_caching = 0;
	char *path, *xattr;
	size_t sz0, sz1, sz2;
	gfarm_error_t e;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	while ((c = getopt(argc, argv, "c")) != -1) {
		switch (c) {
		case 'c':
			do_caching = 1;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
				program_name, c);
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage();
	path = argv[0];
	xattr = argv[1];	

	if (do_caching) {
		if (!gfarm_xattr_caching(xattr))
			gfarm_xattr_caching_pattern_add(xattr);
	}

	sz0 = 0;
	e = gfs_getxattr(path, xattr, NULL, &sz0);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_getxattr(\"%s\", \"%s\", , &0): %s\n",
		    path, xattr, gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	
	sz1 = 0;
	e = gfs_getxattr_caching(path, xattr, NULL, &sz1);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr,
		    "gfs_getxattr_caching(\"%s\", \"%s\", , &0): %s\n",
		    path, xattr, gfarm_error_string(e));
		return (EXIT_FAILURE);
	}

	sz2 = 0;
	e = gfs_getxattr_cached(path, xattr, NULL, &sz2);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr,
		    "gfs_getxattr_cached(\"%s\", \"%s\", , &0): %s\n",
		    path, xattr, gfarm_error_string(e));
		return (EXIT_FAILURE);
	}

	if (sz0 != sz1 || sz0 != sz2) {
		fprintf(stderr,
		    "path %s, xattr %s: size mismatch %d vs %d vs %d\n",
		    path, xattr, (int)sz0, (int)sz1, (int)sz2);
		return (EXIT_FAILURE);
	}
	printf("path %s, xattr %s: size %d\n", path, xattr, (int)sz0);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (4);
	}
	return (0);
}

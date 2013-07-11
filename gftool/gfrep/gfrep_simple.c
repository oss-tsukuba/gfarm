/*
 * $Id$
 */

#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdio.h>

#include <gfarm/gfarm.h>

char *program_name = "gfrep_simple";

static int
usage()
{
	fprintf(stderr, "Usage: %s [-s srchost] -d dsthost file\n",
		program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	char *src = NULL, *dst = NULL, *f, c;
	gfarm_error_t e;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	while ((c = getopt(argc, argv, "d:s:h?")) != -1) {
		switch (c) {
		case 'd':
			dst = optarg;
			break;
		case 's':
			src = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (dst == NULL)
		usage();

	f = *argv;
	if (src == NULL)
		e = gfs_replicate_file_to(f, dst, 0);
	else
		e = gfs_replicate_file_from_to(f, src, dst, 0);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", f, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (0);
}

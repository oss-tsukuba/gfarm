/*
 * $Id$
 */

#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdio.h>

#include <gfarm/gfarm.h>

#include "metadb_server.h"

char *program_name = "gfrep";

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
	struct gfarm_host_info sinfo, dinfo;
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

	if (src != NULL) {
		e = gfarm_host_info_get_by_name_alias(src, &sinfo);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", src,
				gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
	}
	e = gfarm_host_info_get_by_name_alias(dst, &dinfo);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", dst, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	f = *argv;
	e = gfs_replicate_from_to(f, src, sinfo.port, dst, dinfo.port);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", f, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if (src != NULL)
		gfarm_host_info_free(&sinfo);
	gfarm_host_info_free(&dinfo);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (0);
}

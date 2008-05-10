/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#ifdef HAVE_GSI
#include "auth.h"
#endif

char *program_name = "gfwhoami";

#ifdef HAVE_GSI
#define GFWHOAMI_OPTIONS "hv"
#else
#define GFWHOAMI_OPTIONS "h"
#endif

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-" GFWHOAMI_OPTIONS "]\n", program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c;
#ifdef HAVE_GSI
	int verbose_flag = 0;
#endif

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n", program_name,
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, GFWHOAMI_OPTIONS)) != -1) {
		switch (c) {
#ifdef HAVE_GSI
		case 'v':
			verbose_flag = 1;
			break;
#endif
		case 'h':
		default:
			usage();
		}
	}

	if (argc - optind > 0)
		usage();

	printf("%s", gfarm_get_global_username());
#ifdef HAVE_GSI
	if (verbose_flag)
		printf(" %s", gfarm_gsi_client_cred_name());
#endif
	printf("\n");

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n", program_name,
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (0);
}

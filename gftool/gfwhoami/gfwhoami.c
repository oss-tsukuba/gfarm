/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#include "auth.h"

char *program_name = "gfwhoami";

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-hv]\n", program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	char *e;
	int c;
	extern char *optarg;
	int verbose_flag = 0;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "hv")) != -1) {
		switch (c) {
		case 'v':
			verbose_flag = 1;
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (argc - optind > 0)
		usage();

	printf("%s", gfarm_get_global_username());
	if (verbose_flag == 1)
		printf(" %s", gfarm_gsi_client_cred_name());
	printf("\n");

	return (0);
}

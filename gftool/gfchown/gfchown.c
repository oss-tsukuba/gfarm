/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

char *program_name = "gfchmod";
int opt_chgrp = 0;

static void
usage(void)
{
	if (!opt_chgrp) {
		fprintf(stderr, "Usage: %s <owner>[:<group>] <path>...\n",
		    program_name);
		fprintf(stderr, "       %s :<group> <path>...\n",
		    program_name);
	} else {
		fprintf(stderr, "Usage: %s <group> <path>...\n", program_name);
	}
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, i, status = 0;
	char *s, *user = NULL, *group = NULL;
	extern int optind;

	if (argc > 0)
		program_name = basename(argv[0]);
	if (strcasecmp(program_name, "gfchgrp") == 0)
		opt_chgrp = 1;
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 1)
		usage();

	if (!opt_chgrp) {
		if (argv[0][0] == ':') {
			group = &argv[0][1];
		} else if ((s = strchr(argv[0], ':')) != NULL) {
			*s = '\0';
			user = argv[0];
			group = s + 1;
		} else {
			user = argv[0];
		}
	} else {
		group = argv[0];
	}

	for (i = 1; i < argc; i++) {
		e = gfs_chown(argv[i], user, group);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

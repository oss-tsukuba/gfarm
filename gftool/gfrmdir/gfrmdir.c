/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/socket.h>
#include <gfarm/gfarm.h>
#include "gfs_client.h"

char *program_name = "gfrmdir";

static void
usage()
{
	fprintf(stderr, "Usage: %s directory...\n", program_name);
	exit(1);
}

struct args {
	char *path;
};

char *
gfrmdir(struct gfs_connection *gfs_server, void *args)
{
	struct args *a = args;
	char *e = gfs_client_rmdir(gfs_server, a->path);

	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/*
		 * We don't treat this as error to remove path_info in MetaDB
		 */
		fprintf(stderr, "%s on %s: %s\n", program_name,
		    gfs_client_hostname(gfs_server), e);
		return (NULL);
	}
	return (e);
}

int
main(int argc, char **argv)
{
	char *e, *canonic_path;
	int i, c, remove_directory_in_spool = 0, status = 0;
	extern int optind;

	if (argc <= 1)
		usage();
	program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "ah?")) != EOF) {
		switch (c) {
		case 'a':
			remove_directory_in_spool = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	for (i = 0; i < argc; i++) {
		char *e;
		struct args a;
		int nhosts_succeed;

		/*
		 * canonic_path is needed to be obtained before
		 * gfs_rmdir() for gfs_client_apply_all_hosts().
		 */
		e = gfarm_url_make_path(argv[i], &canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			continue;
		}
		e = gfs_rmdir(argv[i]);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			free(canonic_path);
			continue;
		}
		if (!remove_directory_in_spool) {
			free(canonic_path);
			continue;
		}

		a.path = canonic_path;
		e = gfs_client_apply_all_hosts(gfrmdir, &a, program_name, 1,
			&nhosts_succeed);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			status = 1;
		}
		free(canonic_path);
	}
	e = gfarm_terminate();
	return (status);
}

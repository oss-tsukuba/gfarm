/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_metadb.h>
#include <gfarm/gfarm_error.h>
#include "gfs_client.h"

char *program_name = "gfmkdir";

static void
usage()
{
	fprintf(stderr, "Usage: %s directory...\n", program_name);
	exit(1);
}

struct args {
	char *path;
	gfarm_int32_t mode;
};

char *
gfmkdir(struct gfs_connection *gfs_server, void *args)
{
	struct args *a = args;

	return (gfs_client_mkdir(gfs_server, a->path, a->mode));
}

int
main(int argc, char **argv)
{
	char *e, *canonic_path;
	int i; 
	char *user;

	if (argc <= 1)
		usage();
	program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	user = gfarm_get_global_username();
	if (user == NULL) {
		fprintf(stderr, "%s:%s%s\n", program_name,
			"programming error, gfarm library isn't properly ",
			"initialized");
		exit(1);
	}
	for (i = 1; i < argc; i++) {
		struct args a;
		struct gfarm_path_info pi;
		struct timeval now;
		int nhosts_succeed;

		e = gfarm_canonical_path_for_creation(argv[i], &canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				e);
			exit(1);
		}
		if (gfarm_path_info_get(canonic_path, &pi) == 0) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
			    GFARM_ERR_ALREADY_EXISTS);
			gfarm_path_info_free(&pi);
			continue;
		}

		a.path = canonic_path;
		a.mode = 0755;
		e = gfs_client_apply_all_hosts(gfmkdir, &a, program_name,
			&nhosts_succeed);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				e);
			if (nhosts_succeed == 0) {
				free(canonic_path);			
				continue;
			}
		}
		gettimeofday(&now, NULL);
		pi.pathname = canonic_path;
		pi.status.st_mode = (GFARM_S_IFDIR | a.mode);
		pi.status.st_user = user;
		pi.status.st_group = "*"; /* XXX for now */
		pi.status.st_atimespec.tv_sec =
		pi.status.st_mtimespec.tv_sec =
		pi.status.st_ctimespec.tv_sec = now.tv_sec;
		pi.status.st_atimespec.tv_nsec =
		pi.status.st_mtimespec.tv_nsec =
		pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
		pi.status.st_size = 0;
		pi.status.st_nsections = 0;
		e = gfarm_path_info_set(canonic_path, &pi);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				e);
			exit(1);
		}
		free(canonic_path);
	}
	e = gfarm_terminate();
	return (0);
}

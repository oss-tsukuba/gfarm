/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_metadb.h>
#include <gfarm/gfarm_error.h>
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
	int i, c;

	if (argc <= 1)
		usage();
	program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "h")) != EOF) {
		switch (c) {
		case 'h':
		default:
			usage();
		}
	}

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	for (i = 1; i < argc; i++) {
		char *e;
		struct args a;
		struct gfarm_path_info pi;
		GFS_Dir dir;
		struct gfs_dirent *entry;
		int nhosts_succeed;

		e = gfarm_url_make_path(argv[i], &canonic_path);
		/* We permit missing gfarm: prefix here as a special case */
		if (e == GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING)
			e = gfarm_canonical_path(argv[i],
			    &canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			continue;
		}
		e = gfarm_path_info_get(canonic_path, &pi);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		if (!GFARM_S_ISDIR(pi.status.st_mode)) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				GFARM_ERR_NOT_A_DIRECTORY);
			free(canonic_path);
			gfarm_path_info_free(&pi);
			continue;
		}
		gfarm_path_info_free(&pi);
		e = gfs_opendir(argv[i], &dir);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				e);
			exit(1);
		}
		e = gfs_readdir(dir, &entry);
		if (e != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				e);
			exit(1);
		}
		if (entry != NULL) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
				"directory not empty");
			free(canonic_path);
			continue;			
		}
		a.path = canonic_path;
		e = gfs_client_apply_all_hosts(gfrmdir, &a, program_name,
			&nhosts_succeed);
#if 0 /* XXX - gfrmdir may fail.  In the current release, Only problem
       * is that a new directory cannot be created in the other
       * owner/permission.  */
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
#endif
		e = gfarm_path_info_remove(canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		free(canonic_path);
	}
	e = gfarm_terminate();
	return (0);
}

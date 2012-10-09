/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

char *program_name = "gfchown";
int opt_chgrp = 0;

static void
usage(void)
{
	if (!opt_chgrp) {
		fprintf(stderr, "Usage: %s [-h] <owner>[:<group>] <path>...\n",
		    program_name);
		fprintf(stderr, "       %s [-h] :<group> <path>...\n",
		    program_name);
	} else {
		fprintf(stderr, "Usage: %s [-h] <group> <path>...\n",
		    program_name);
	}
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h\t"
	    "affect symbolic links instead of referenced files\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, i, n, follow_symlink = 1, status = 0;
	char *s, *user = NULL, *group = NULL;
	gfarm_stringlist paths;
	gfs_glob_t types;

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
			follow_symlink = 0;
			break;
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

	if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	} else if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	} else {
		for (i = 1; i < argc; i++)
			gfs_glob(argv[i], &paths, &types);

		n = gfarm_stringlist_length(&paths);
		for (i = 0; i < n; i++) {
			s = gfarm_stringlist_elem(&paths, i);
			e = (follow_symlink ? gfs_chown : gfs_lchown)(s,
			    user, group);
			switch (e) {
			case GFARM_ERR_NO_ERROR:
				break;
			case GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY:
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, s,
				    gfarm_error_string(e));
				status = 1;
				break;
			default:
				fprintf(stderr, "%s: %s%s%s: %s\n",
				    program_name,
				    user != NULL ? user : "",
				    user != NULL && group != NULL ? ":" : "",
				    group != NULL ? group : "",
				    gfarm_error_string(e));
				status = 1;
				break;
			}
		}
		gfs_glob_free(&types);
		gfarm_stringlist_free_deeply(&paths);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

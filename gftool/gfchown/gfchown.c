/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfchown";
static int opt_chgrp = 0;
static int opt_follow_symlink = 1;
static int opt_recursive = 0;

static void
usage(void)
{
	if (!opt_chgrp) {
		fprintf(stderr, "Usage: %s [-hR] <owner>[:<group>] <path>...\n",
		    program_name);
		fprintf(stderr, "       %s [-hR] :<group> <path>...\n",
		    program_name);
	} else
		fprintf(stderr, "Usage: %s [-hR] <group> <path>...\n",
		    program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h\t"
	    "affect symbolic links instead of referenced files\n");
	fprintf(stderr, "\t-R\t"
	    "change owner/group recursively\n");
	exit(2);
}

struct gfchown_arg {
	char *user, *group;
};

static gfarm_error_t
gfchown(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	struct gfchown_arg *a = arg;
	char *u = a->user, *g = a->group;

	e = (opt_follow_symlink ? gfs_chown : gfs_lchown)(path, u, g);
	switch (e) {
	case GFARM_ERR_NO_ERROR:
		break;
	case GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY:
		fprintf(stderr, "%s: %s: %s\n", program_name, path,
			gfarm_error_string(e));
		break;
	default:
		fprintf(stderr, "%s: %s%s%s: %s\n", program_name,
			u != NULL ? u : "",
			u != NULL && g != NULL ? ":" : "",
			g != NULL ? g : "",
			gfarm_error_string(e));
		break;
	}
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int c, i, n;
	char *s, *si = NULL;
	struct gfchown_arg arg = { NULL, NULL };
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct gfs_stat st;

	if (argc > 0)
		program_name = basename(argv[0]);
	if (strcasecmp(program_name, "gfchgrp") == 0)
		opt_chgrp = 1;
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "hR?")) != -1) {
		switch (c) {
		case 'h':
			opt_follow_symlink = 0;
			break;
		case 'R':
			opt_recursive = 1;
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
			arg.group = &argv[0][1];
		} else if ((s = strchr(argv[0], ':')) != NULL) {
			*s = '\0';
			arg.user = argv[0];
			arg.group = s + 1;
		} else {
			arg.user = argv[0];
		}
	} else
		arg.group = argv[0];

	if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 1; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		s = gfarm_stringlist_elem(&paths, i);
		e = gfarm_realpath_by_gfarm2fs(s, &si);
		if (e == GFARM_ERR_NO_ERROR)
			s = si;
		if ((e = gfs_lstat(s, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", s, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISDIR(st.st_mode) && opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
					gfchown, gfchown, NULL, s, &arg);
			else
				e = gfchown(s, &st, &arg);
			gfs_stat_free(&st);
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(si);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}

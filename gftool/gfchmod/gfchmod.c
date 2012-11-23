/*
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfchmod";

static int opt_follow_symlink = 1;
static int opt_recursive = 0;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-hR] <mode> <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h\t"
	    "affect symbolic links instead of referenced files\n");
	fprintf(stderr, "\t-R\t"
	    "change mode recursively\n");
	exit(2);
}

struct gfchmod_arg {
	gfarm_mode_t mode;
};

static gfarm_error_t
gfchmod(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	struct gfchmod_arg *a = arg;
	gfarm_mode_t mode = a->mode;

	e = (opt_follow_symlink ? gfs_chmod : gfs_lchmod)(path, mode);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s: %s\n", program_name, path,
			gfarm_error_string(e));
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int c, i, n;
	char *si = NULL, *s;
	struct gfchmod_arg arg;
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct gfs_stat st;

	if (argc > 0)
		program_name = basename(argv[0]);
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

	errno = 0;
	arg.mode = strtol(argv[0], &s, 8);
	if (errno != 0 || s == argv[0] || *s != '\0') {
		fprintf(stderr, "%s: %s: %s\n", program_name, argv[0],
		    errno != 0 ? strerror(errno)
		    : "<mode> must be an octal number");
		exit(EXIT_FAILURE);
	}
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
					gfchmod, gfchmod, NULL, s, &arg);
			else
				e = gfchmod(s, &st, &arg);
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"

char *program_name = "gfwhere";

static void
display_name(char *name)
{
	static int print_ln;

	if (print_ln)
		printf("\n");
	else
		print_ln = 1;

	printf("%s:\n", name);
}

static gfarm_error_t
display_copy(char *path, int do_not_display_name)
{
	int n, i;
	char **hosts;
	gfarm_error_t e;

	e = gfs_replica_list_by_name(path, &n, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (!do_not_display_name)
		display_name(path);

	i = 0;
	if (i < n)
		printf("%s", hosts[i++]);
	for (; i < n; ++i)
		printf(" %s", hosts[i]);
	printf("\n");

	for (i = 0; i < n; ++i)
		free(hosts[i]);
	free(hosts);

	return (e);
}

static gfarm_error_t
display_replica_catalog(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	gfarm_mode_t mode;
	int do_not_display_name = *(int *)arg;

	mode = st->st_mode;
	if (GFARM_S_ISDIR(mode))
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	else if (st->st_ncopy == 0 && st->st_size > 0)
		/* XXX - GFARM_ERR_NO_REPLICA */
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = display_copy(path, do_not_display_name);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", path, gfarm_error_string(e));
	return (e);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-r, -R\t\tdisplay subdirectories recursively\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, n, ch, opt_recursive = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "rR?")) != -1) {
		switch (ch) {
		case 'r':
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

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	if (argc == 0) {
		usage();
	}

	e = gfarm_stringlist_init(&paths);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	gfs_glob_free(&types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *p = gfarm_stringlist_elem(&paths, i);
		struct gfs_stat st;
		int do_not_display = 0;

		if ((e = gfs_stat(p, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", p, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISREG(st.st_mode)) {
				do_not_display = (n == 1);
				e = display_replica_catalog(p, &st,
					&do_not_display);
			}
			else if (opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
					display_replica_catalog, NULL, NULL,
					p, &do_not_display);
			else
				fprintf(stderr, "%s: not a file\n", p);
			gfs_stat_free(&st);
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
	}

	gfarm_stringlist_free_deeply(&paths);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? 0 : 1);
}

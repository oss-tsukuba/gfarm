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

static gfarm_error_t
display_name(char *name, struct gfs_stat *st, void *arg)
{
	static int print_ln;

	if (print_ln)
		printf("\n");
	else
		print_ln = 1;

	printf("%s:\n", name);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
display_copy(char *path)
{
	int n, i;
	char **hosts;
	gfarm_error_t e;

	e = gfs_replica_list_by_name(path, &n, &hosts);
	if (e == GFARM_ERR_NO_ERROR) {
		i = 0;
		if (i < n)
			printf("%s", hosts[i++]);
		for (; i < n; ++i)
			printf(" %s", hosts[i]);
		for (i = 0; i < n; ++i)
			free(hosts[i]);
		free(hosts);
	}
	printf("\n");
	return (e);
}

static gfarm_error_t
display_replica_catalog(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_mode_t mode;

	display_name(path, st, arg);

	mode = st->st_mode;
	if (GFARM_S_ISDIR(mode))
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;

	if (e == GFARM_ERR_NO_ERROR)
		e = display_copy(path);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s\n", gfarm_error_string(e));
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

		if ((e = gfs_stat(p, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", p, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISREG(st.st_mode)) 
				e = display_replica_catalog(p, &st, NULL);
			else if (opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
					display_replica_catalog, display_name,
					NULL, p, NULL);
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

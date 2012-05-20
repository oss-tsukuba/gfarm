/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"

char *program_name = "gfrm";

struct options {
	char *host;
	int force;
	int noexecute;
	int recursive;
};

struct files {
	gfarm_stringlist files, dirs;
};

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-r] [-n] [-f] [-h hostname] file...\n",
	    program_name);
	exit(EXIT_FAILURE);
}

static gfarm_error_t
add_file(char *file, struct gfs_stat *st, void *arg)
{
	struct files *a = arg;
	char *f;

	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(&a->files, f));
}

static gfarm_error_t
is_valid_dir(char *file, struct gfs_stat *st, void *arg)
{
	const char *f = gfarm_url_dir_skip(file);

	if (f[0] == '.' && (f[1] == '\0' || (f[1] == '.' && f[2] == '\0')))
		return (GFARM_ERR_INVALID_ARGUMENT);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
do_not_add_dir(char *file, struct gfs_stat *st, void *arg)
{
	return (GFARM_ERR_IS_A_DIRECTORY);
}

static gfarm_error_t
add_dir(char *file, struct gfs_stat *st, void *arg)
{
	struct files *a = arg;
	char *f;

	f = strdup(file);
	if (f == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (gfarm_stringlist_add(&a->dirs, f));
}

static gfarm_error_t
remove_files(gfarm_stringlist *files, gfarm_stringlist *dirs,
	struct options *options)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, e2 = GFARM_ERR_NO_ERROR;
	int i, nerr = 0;

	for(i = 0; i < gfarm_stringlist_length(files); i++) {
		char *file = gfarm_stringlist_elem(files, i);

		if (options->noexecute)
			printf("%s\n", file);
		else if (options->host == NULL)
			e = gfs_unlink(file);
		else
			e = gfs_replica_remove_by_file(file, options->host);

		if (e != GFARM_ERR_NO_ERROR &&
		    (!options->force ||
		     e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) &&
		    (!options->recursive || e != GFARM_ERR_NO_SUCH_OBJECT)) {
			fprintf(stderr, "%s: %s: %s\n",
				program_name, file, gfarm_error_string(e));
			if (e2 == GFARM_ERR_NO_ERROR)
				e2 = e;
			nerr++;
		}
	}

	if (options->host != NULL)
		goto skip_directory_remove;
	/* remove directories only if the -h option is not specified */
	for (i = 0; i < gfarm_stringlist_length(dirs); i++) {
		char *dir = gfarm_stringlist_elem(dirs, i);

		if (options->noexecute)
			printf("%s\n", dir);
		else
			e = gfs_rmdir(dir);

		if (e != GFARM_ERR_NO_ERROR &&
		    (!options->force ||
		     e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)) {
			fprintf(stderr, "%s: %s: %s\n",
				program_name, dir, gfarm_error_string(e));
			if (e2 == GFARM_ERR_NO_ERROR)
				e2 = e;
			nerr++;
		}
	}
skip_directory_remove:
	return (nerr == 0 ? GFARM_ERR_NO_ERROR : e2);
}

static int
error_check(gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return 0;

	fprintf(stderr, "%s: %s\n", program_name, gfarm_error_string(e));
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, n, c, status = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct files files;
	struct options options;
	gfarm_error_t (*op_dir_before)(char *, struct gfs_stat *, void *);

	options.host = NULL;
	options.force = options.noexecute = options.recursive = 0;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	error_check(e);

	while ((c = getopt(argc, argv, "fh:nr?")) != -1) {
		switch (c) {
		case 'f':
			options.force = 1;
			break;
		case 'h':
			options.host = optarg;
			break;
		case 'n':
			options.noexecute = 1;
			break;
		case 'r':
			options.recursive = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0)
		usage();

	if (options.recursive)
		op_dir_before = is_valid_dir;
	else
		op_dir_before = do_not_add_dir;
	e = gfarm_stringlist_init(&files.files);
	error_check(e);

	e = gfarm_stringlist_init(&files.dirs);
	error_check(e);

	e = gfarm_stringlist_init(&paths);
	error_check(e);

	e = gfs_glob_init(&types);
	error_check(e);

	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	gfs_glob_free(&types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *file = gfarm_stringlist_elem(&paths, i);

		e = gfarm_foreach_directory_hierarchy(
			add_file, op_dir_before, add_dir, file, &files);

		if (e != GFARM_ERR_NO_ERROR &&
		    (!options.force ||
		     e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, file, gfarm_error_string(e));
			status = 1;
		}

	}
	gfarm_stringlist_free_deeply(&paths);

	if (remove_files(&files.files, &files.dirs, &options) !=
	    GFARM_ERR_NO_ERROR)
		status = 1; /* error message is already printed */

	gfarm_stringlist_free_deeply(&files.dirs);
	gfarm_stringlist_free_deeply(&files.files);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

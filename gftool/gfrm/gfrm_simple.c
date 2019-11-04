/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfrm";

struct options {
	char *host, *domain;
	int force;
	int noexecute;
	int recursive;
};

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-r] [-n] [-f] [-h hostname] "
	    "[-D domainname] file...\n", program_name);
	exit(EXIT_FAILURE);
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
err_is_a_dir(char *file, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e = GFARM_ERR_IS_A_DIRECTORY;

	fprintf(stderr, "%s: %s\n", file, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfs_replica_remove_by_domain(const char *path, const char *domain)
{
	const char *host;
	int i, flags = 0;
	struct gfs_replica_info *ri;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;

	e = gfs_replica_info_by_name(path, flags, &ri);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < gfs_replica_info_number(ri); ++i) {
		host = gfs_replica_info_nth_host(ri, i);
		if (gfarm_host_is_in_domain(host, domain)) {
			e = gfs_replica_remove_by_file(path, host);
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
	}
	gfs_replica_info_free(ri);
	return (e_save);
}

static gfarm_error_t
remove_file(char *file, struct gfs_stat *st, void *arg)
{
	struct options *options = arg;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if ((options->host != NULL || options->domain != NULL)
	    && !GFARM_S_ISREG(st->st_mode))
		return (e);

	if (options->noexecute)
		printf("%s\n", file);
	else if (options->host != NULL)
		e = gfs_replica_remove_by_file(file, options->host);
	else if (options->domain != NULL)
		e = gfs_replica_remove_by_domain(file, options->domain);
	else
		e = gfs_unlink(file);

	if (options->force &&
	    (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY ||
	     e == GFARM_ERR_NO_SUCH_OBJECT))
		e = GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", file, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
remove_dir(char *dir, struct gfs_stat *st, void *arg)
{
	struct options *options = arg;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (options->host != NULL || options->domain != NULL)
		return (e);

	if (options->noexecute)
		printf("%s\n", dir);
	else
		e = gfs_rmdir(dir);

	if (options->force && e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		e = GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", dir, gfarm_error_string(e));
	return (e);
}

static int
error_check(gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return (0);

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
	struct options options;
	gfarm_error_t (*is_dir)(char *, struct gfs_stat *, void *)
		= err_is_a_dir;

	options.host =
	options.domain = NULL;
	options.force = options.noexecute = options.recursive = 0;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	error_check(e);

	while ((c = getopt(argc, argv, "D:fh:nr?")) != -1) {
		switch (c) {
		case 'D':
			options.domain = optarg;
			break;
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
		is_dir = is_valid_dir;

	e = gfarm_stringlist_init(&paths);
	error_check(e);

	e = gfs_glob_init(&types);
	error_check(e);

	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	gfs_glob_free(&types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *file = gfarm_stringlist_elem(&paths, i), *rpath = NULL;
		struct gfs_stat st;

		e = gfarm_realpath_by_gfarm2fs(file, &rpath);
		if (e == GFARM_ERR_NO_ERROR)
			file = rpath;
		if ((e = gfs_lstat(file, &st)) == GFARM_ERR_NO_ERROR) {
			e = gfarm_foreach_directory_hierarchy(remove_file,
				is_dir, remove_dir, file, &options);
			gfs_stat_free(&st);
		} else if (options.force &&
			   e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
			e = GFARM_ERR_NO_ERROR;
		else
			fprintf(stderr, "%s: %s\n", file,
			    gfarm_error_string(e));
		if (e != GFARM_ERR_NO_ERROR)
			status = 1;
		free(rpath);
	}
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

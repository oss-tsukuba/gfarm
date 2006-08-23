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

static int opt_size;

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

static char *
display_section_copies(char *gfarm_file, char *section)
{
	int ncopies, j;
	struct gfarm_file_section_copy_info *copies;
	struct gfarm_file_section_info sinfo;
	char *e;

	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e != NULL)
		return (e);

	if (opt_size) {
		e = gfarm_file_section_info_get(gfarm_file, section, &sinfo);
		if (e != NULL)
			goto free_copy_info;
	}
	printf("%s", section);
	if (opt_size) {
		printf(" [%" PR_FILE_OFFSET " bytes]", sinfo.filesize);
		gfarm_file_section_info_free(&sinfo);
	}
	printf(":");
	for (j = 0; j < ncopies; ++j)
		printf(" %s", copies[j].hostname);
	printf("\n");
free_copy_info:
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	return (e);
}

static char *
display_replica_catalog_section(
	char *gfarm_url, struct gfs_stat *st, void *arg)
{
	char *gfarm_file, *e;
	char *section = arg;

	display_name(gfarm_url);

	if (GFARM_S_ISDIR(st->st_mode))
		return (NULL);
	if (section == NULL) {
		e = "invalid section";
		fprintf(stderr, "%s\n", e);
		return (e);
	}
	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e == NULL) {
		e = display_section_copies(gfarm_file, section);
		if (e != NULL)
			fprintf(stderr, "%s: %s\n", section, e);
		free(gfarm_file);
	}
	return (e);
}

static char *
display_replica_catalog(char *gfarm_url, struct gfs_stat *st, void *arg)
{
	char *gfarm_file, *e, *e_save = NULL;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	gfarm_mode_t mode;

	display_name(gfarm_url);

	mode = st->st_mode;
	if (GFARM_S_ISDIR(mode))
		return (NULL);
	else if (!GFARM_S_ISREG(mode)) {
		e = "invalid file";
		fprintf(stderr, "%s\n", e);
		return (e);
	}

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		return (e);
	}

	if ((mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0) { /* program? */
		e = gfarm_file_section_info_get_all_by_file(
		    gfarm_file, &nsections, &sections);
	} else {
		e = gfarm_file_section_info_get_sorted_all_serial_by_file(
		    gfarm_file, &nsections, &sections);
	}
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		goto free_gfarm_file;
	}
	for (i = 0; i < nsections; i++) {
		e = display_section_copies(gfarm_file, sections[i].section);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			fprintf(stderr, "%s: %s\n", sections[i].section, e);
		}
	}
	e = e_save;
	gfarm_file_section_info_free_all(nsections, sections);
free_gfarm_file:
	free(gfarm_file);

	return (e);
}

char *
do_single_file(
	char *(*op_file)(char *, struct gfs_stat *, void *),
	char *(*op_dir1)(char *, struct gfs_stat *, void *),
	char *(*op_dir2)(char *, struct gfs_stat *, void *),
	char *file, void *arg)
{
	struct gfs_stat st;
	char *e;

	e = gfs_stat(file, &st);
	if (e != NULL) {
		display_name(file);
		fprintf(stderr, "%s\n", e);
		return (e);
	}

	if (GFARM_S_ISDIR(st.st_mode)) {
		display_name(file);
		e = GFARM_ERR_IS_A_DIRECTORY;
		fprintf(stderr, "%s\n", e);
	}
	else if (GFARM_S_ISREG(st.st_mode))
		e = (*op_file)(file, &st, arg);

	gfs_stat_free(&st);
	return (e);
}

char *
gfwhere(gfarm_stringlist *list, char *section, int recursive)
{
	int n, i;
	char *e, *e_save = NULL;
	char *(*display)(char *, struct gfs_stat *, void *);
	char *(*foreach)(char *(*)(char *, struct gfs_stat *, void *),
			 char *(*)(char *, struct gfs_stat *, void *),
			 char *(*)(char *, struct gfs_stat *, void *),
			 char *, void *);

	if (section != NULL)
		display = display_replica_catalog_section;
	else
		display = display_replica_catalog;
	if (recursive)
		foreach = gfarm_foreach_directory_hierarchy;
	else
		foreach = do_single_file;

	n = gfarm_stringlist_length(list);
	for (i = 0; i < n; i++) {
		char *p = gfarm_stringlist_elem(list, i);

		e = foreach(display, display, NULL, p, section);
		if (e_save == NULL)
			e_save = e;
	}
	return (e_save);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-I <fragment>\t"
		"specify fragment index to be displayed\n");
	fprintf(stderr, "\t-s\t\t"
		"display file size of each file fragment\n");
	fprintf(stderr, "\t-r, -R\t\tdisplay subdirectories recursively\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *section = NULL;
	int i, ch, opt_recursive = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "srI:R?")) != -1) {
		switch (ch) {
		case 's':
			opt_size = 1;
			break;
		case 'r':
		case 'R':
			opt_recursive = 1;
			break;
		case 'I':
			section = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	if (argc == 0) {
		usage();
	}

	e = gfarm_stringlist_init(&paths);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	gfs_glob_free(&types);

	(void)gfwhere(&paths, section, opt_recursive);

	gfarm_stringlist_free_deeply(&paths);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (e == NULL ? 0 : 1);
}

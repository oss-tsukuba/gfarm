#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfwhere";

static char *
display_replica_catalog(char *gfarm_url);

static char *
display_replica_catalog_section(char *gfarm_url, char *section)
{
	char *gfarm_file, *e;
	int j, ncopies;
	struct gfarm_file_section_copy_info *copies;
	struct gfs_stat st;

	if (section == NULL)
		return (display_replica_catalog(gfarm_url));

	e = gfs_stat(gfarm_url, &st);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		return (e);
	}
	gfs_stat_free(&st);

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		return (e);
	}
	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		free(gfarm_file);
		return (e);
	}
	j = 0;
	printf("%s", copies[j].hostname);
	for (++j; j < ncopies; ++j)
		printf(" %s", copies[j].hostname);
	printf("\n");
	gfarm_file_section_copy_info_free_all(ncopies, copies);
	free(gfarm_file);

	return (NULL);
}

static char *
display_replica_catalog(char *gfarm_url)
{
	char *gfarm_file, *e, *e_save = NULL;
	int i, j, nsections;
	struct gfarm_file_section_info *sections;
	struct gfs_stat st;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		return (e);
	}
	e = gfs_stat(gfarm_url, &st);
	if (e != NULL) {
		free(gfarm_file);
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		return (e);
	}
	if (!GFARM_S_ISREG(st.st_mode)) {
		free(gfarm_file);
		gfs_stat_free(&st);
		fprintf(stderr, "%s: not a file\n", gfarm_url);
		return (e);
	}
	if ((st.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0) { /* program? */
		e = gfarm_file_section_info_get_all_by_file(
		    gfarm_file, &nsections, &sections);
	} else {
		e = gfarm_file_section_info_get_sorted_all_serial_by_file(
		    gfarm_file, &nsections, &sections);
	}
	gfs_stat_free(&st);
	if (e != NULL) {
		free(gfarm_file);
		fprintf(stderr, "%s: %s\n", gfarm_url, e);
		return (e);
	}
	for (i = 0; i < nsections; i++) {
		int ncopies;
		struct gfarm_file_section_copy_info *copies;

		e = gfarm_file_section_copy_info_get_all_by_section(
		    gfarm_file, sections[i].section, &ncopies, &copies);
		if (e != NULL) {
			fprintf(stderr, "%d: %s\n", i, e);
			if (e_save == NULL)
				e_save = e;
			continue;
		}
		printf("%s:", sections[i].section);
		for (j = 0; j < ncopies; j++)
			printf(" %s", copies[j].hostname);
		gfarm_file_section_copy_info_free_all(ncopies, copies);
		printf("\n");
	}
	gfarm_file_section_info_free_all(nsections, sections);
	free(gfarm_file);
	return (e_save);	
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-I <fragment>\t"
		"display filesystem nodes specified by <fragment>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *section = NULL;
	int i, ch, error = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	int argc_expanded;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "I:")) != -1) {
		switch (ch) {
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

	argc_expanded = gfarm_stringlist_length(&paths);
	for (i = 0; i < argc_expanded; i++) {
		char *p = gfarm_stringlist_elem(&paths, i);

		if (argc_expanded > 1)
			printf("%s:\n", p);
		if (display_replica_catalog_section(p, section) != NULL)
			error = 1;
		if (argc_expanded > 1 && i < argc_expanded - 1)
			printf("\n");
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (error);
}

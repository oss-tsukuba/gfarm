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

static int opt_size = 0;

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
display_replica_catalog_section(char *gfarm_url, char *section)
{
	char *gfarm_file, *e;

	if (section == NULL)
		return ("invalid section");

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = display_section_copies(gfarm_file, section);

	free(gfarm_file);
	return (e);
}

static char *
display_replica_catalog(char *gfarm_url)
{
	char *gfarm_file, *e, *e_save;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	struct gfs_stat st;
	gfarm_mode_t mode;

	e = gfs_stat(gfarm_url, &st);
	if (e != NULL)
		return (e);
	mode = st.st_mode;
	gfs_stat_free(&st);

	if (!GFARM_S_ISREG(mode))
		return ("not a regular file");

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	if ((mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0) { /* program? */
		e_save = gfarm_file_section_info_get_all_by_file(
		    gfarm_file, &nsections, &sections);
	} else {
		e_save = gfarm_file_section_info_get_sorted_all_serial_by_file(
		    gfarm_file, &nsections, &sections);
	}
	if (e_save != NULL)
		goto free_gfarm_file;

	for (i = 0; i < nsections; i++) {
		e = display_section_copies(gfarm_file, sections[i].section);
		if (e != NULL) {
			if (e_save == NULL)
				e_save = e;
			fprintf(stderr, "%s: %s\n", sections[i].section, e);
		}
	}
	gfarm_file_section_info_free_all(nsections, sections);
free_gfarm_file:
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
	fprintf(stderr, "\t-s\t\t"
		"display file size of each file fragment\n");
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

	while ((ch = getopt(argc, argv, "I:s?")) != -1) {
		switch (ch) {
		case 'I':
			section = optarg;
			break;
		case 's':
			opt_size = 1;
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
		if (section != NULL)
			e = display_replica_catalog_section(p, section);
		else
			e = display_replica_catalog(p);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n",
				section == NULL ? p : section, e);
			error = 1;
		}
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

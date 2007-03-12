#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfs_misc.h"
#include "gfarm_foreach.h"

char *program_name = "gfwhere";

static int opt_size;

static char *
display_name(char *name, struct gfs_stat *st, void *arg)
{
	static int print_ln;

	if (print_ln)
		printf("\n");
	else
		print_ln = 1;

	printf("%s:\n", name);
	return (NULL);
}

static char *
display_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	printf(" %s", info->hostname);
	return (NULL);
}

static char *
display_section(char *gfarm_file, char *section)
{
	struct gfarm_file_section_info sinfo;
	file_offset_t size;
	char *e = NULL;

	e = gfarm_file_section_info_get(gfarm_file, section, &sinfo);
	if (e != NULL)
		return (e);
	size = sinfo.filesize;
	gfarm_file_section_info_free(&sinfo);

	printf("%s", section);
	if (opt_size)
		printf(" [%" PR_FILE_OFFSET " bytes]", size);
	printf(":");

	e = gfarm_foreach_copy(display_copy, gfarm_file, section, NULL, NULL);

	printf("\n");
	return (e);
}

static char *
display_replica_catalog(char *gfarm_url, struct gfs_stat *st, void *arg)
{
	char *gfarm_file, *e = NULL, *e_save = NULL;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	gfarm_mode_t mode;
	char *section = arg;

	display_name(gfarm_url, st, arg);

	mode = st->st_mode;
	if (GFARM_S_ISDIR(mode))
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!GFARM_S_ISREG(mode))
		e = "invalid file";
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		return (e);
	}

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		return (e);
	}

	/* display a speccified section */
	if (section != NULL) {
		e = display_section(gfarm_file, section);
		if (e != NULL)
			fprintf(stderr, "%s: %s\n", section, e);
		goto free_gfarm_file;
	}

	/* display all sections */
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
		e = display_section(gfarm_file, sections[i].section);
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
	int i, n, ch, opt_recursive = 0;
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

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *p = gfarm_stringlist_elem(&paths, i);
		struct gfs_stat st;

		if ((e = gfs_stat(p, &st)) != NULL) {
			fprintf(stderr, "%s: %s\n", p, e);
		} else {
			if (GFARM_S_ISREG(st.st_mode)) 
				display_replica_catalog(p, &st, section);
			else if (opt_recursive)
				(void)gfarm_foreach_directory_hierarchy(
					display_replica_catalog, display_name,
					NULL, p, section);
			else
				fprintf(stderr, "%s: not a file\n", p);
			gfs_stat_free(&st);
		}
	}

	gfarm_stringlist_free_deeply(&paths);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (e == NULL ? 0 : 1);
}

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

char *
where_is_section_copy(char *gfarm_url)
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
	fprintf(stderr, "\tcurrently no option is supported.\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e;
	int i, ch, error = 0;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "rf")) != -1) {
		switch (ch) {
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

	for (i = 0; i < argc; i++) {
		if (argc > 1)
			printf("%s:\n", argv[i]);
		if (where_is_section_copy(argv[i]) != NULL)
			error = 1;
		if (argc > 1 && i < argc - 1)
			printf("\n");
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (error);
}

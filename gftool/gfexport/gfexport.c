/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfexport";

char *
gfprint(GFS_File gf, FILE *ofp)
{
	int c;

	while ((c = gfs_pio_getc(gf)) != EOF)
		putc(c, ofp);
	return (gfs_pio_error(gf));
}

char *
gfexport(char *gfarm_url, char *section, char *host, FILE *ofp, int explicit)
{
	char *e, *e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != NULL)
		return (e);

	if (section)
		/* section view */
		e = gfs_pio_set_view_section(
			gf, section, host, GFARM_FILE_SEQUENTIAL);
	else if (explicit)
		/* global mode is default, but call explicitly */
		e = gfs_pio_set_view_global(gf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL)
		goto gfs_pio_close;

	e = gfprint(gf, ofp);
 gfs_pio_close:
	e2 = gfs_pio_close(gf);
	return (e != NULL ? e : e2);
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-I <fragment>\n");
	fprintf(stderr, "\t-h <hostname>\n");
	exit(1);
}

static void
error_check(char *file, char* section, char *e)
{
	if (e == NULL)
		return;

	fprintf(stderr, "%s", program_name);
	if (file != NULL) {
		fprintf(stderr, ": %s", file);
		if (section != NULL)
			fprintf(stderr, " (%s)", section);
	}
	fprintf(stderr, ": %s\n", e);
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *section = NULL, *hostname = NULL, *url;
	int ch, global_view = 0;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "a:gh:I:?")) != -1) {
		switch (ch) {
		case 'a':
		case 'I':
			section = optarg;
			break;
		case 'g':
			global_view = 1;
			break;
		case 'h':
			hostname = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		error_check(NULL, NULL,
			    "error: only one input file name expected");

	e = gfarm_initialize(&argc_save, &argv_save);
	error_check(NULL, NULL, e);

	e = gfs_realpath(argv[0], &url);
	error_check(argv[0], NULL, e);

	e = gfexport(url, section, hostname, stdout, global_view);
	error_check(url, section, e);
	free(url);

	e = gfarm_terminate();
	error_check(NULL, NULL, e);

	return (0);
}

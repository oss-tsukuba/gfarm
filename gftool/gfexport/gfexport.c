#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

char *program_name = "gfexport";

gfarm_error_t
gfprint(GFS_File gf, FILE *ofp)
{
	int c;

	while ((c = gfs_pio_getc(gf)) != EOF)
		putc(c, ofp);
	return (gfs_pio_error(gf));
}

#if 0 /* not yet in gfarm v2 */
gfarm_error_t
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
#else
gfarm_error_t
gfexport(char *gfarm_url, FILE *ofp)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfprint(gf, ofp);
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}
#endif

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
#if 0 /* not yet in gfarm v2 */
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-I <fragment>\n");
	fprintf(stderr, "\t-h <hostname>\n");
#endif
	exit(1);
}

#if 0 /* not yet in gfarm v2 */
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
#endif

int
main(argc, argv)
	int argc;
	char **argv;
{
	gfarm_error_t e;
	char *url;
#if 0 /* not yet in gfarm v2 */
	char *section = NULL, *hostname = NULL;
	int global_view = 0;
#endif
	int ch;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize(): %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

#if 0 /* not yet in gfarm v2 */
	while ((ch = getopt(argc, argv, "a:gh:I:?")) != -1)
#else
	while ((ch = getopt(argc, argv, "?")) != -1)
#endif
	{
		switch (ch) {
#if 0
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
#endif
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr, "%s: %s\n", program_name,
		    "error: only one input file name expected");
		usage();
	}

	url = argv[0];

#if 0 /* not yet in gfarm v2 */
	e = gfexport(url, section, hostname, stdout, global_view);
#else
	e = gfexport(url, stdout);
#endif
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s: %s\n", program_name, url,
		    gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate(): %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	return (0);
}

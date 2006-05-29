/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <gfarm/gfarm.h>

char *program_name = "gfreg";

gfarm_error_t
gfimport(FILE *ifp, GFS_File ogf)
{
	int c;

	while ((c = getc(ifp)) != EOF)
		gfs_pio_putc(ogf, c);
	return (gfs_pio_error(ogf));
}

gfarm_error_t
gfimport_to(FILE *ifp, char *gfarm_url, int mode)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY, mode, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", gfarm_url, gfarm_error_string(e));
		return (e);
	}
	e = gfimport(ifp, gf);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "writing to %s: %s\n", gfarm_url,
		    gfarm_error_string(e));
	e2 = gfs_pio_close(gf);
	if (e2 != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "closing %s: %s\n", gfarm_url,
		    gfarm_error_string(e2));
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfimport_from_to(const char *ifile, char *gfarm_url)
{
	gfarm_error_t e;
	FILE *ifp = fopen(ifile, "r");
	struct stat st;
	int rv;

	if (ifp == NULL) {
		perror(ifile);
		return (GFARM_ERR_CANT_OPEN);
	}
	rv = stat(ifile, &st);
	if (rv == -1) {
		perror("stat");
		return (gfarm_errno_to_error(errno));
	}
	e = gfimport_to(ifp, gfarm_url, st.st_mode & 0777);
	fclose(ifp);
	return (e);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s <src_file> <dst_gfarm_file>...\n",
	    program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	extern int optind;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != EOF) {
		switch (c) {
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage();

	e = gfimport_from_to(argv[0], argv[1]);
	if (e != GFARM_ERR_NO_ERROR)
		status = 1;

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

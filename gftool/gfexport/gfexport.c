#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include "gfarm.h"

char *program_name = "gfexport";

char *
gfexport(char *gfarm_url, FILE *ofp, int nhosts, gfarm_stringlist hostlist)
{
	char *e;
	int i, nfrags, c;
	struct gfs_file *gf;

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);

	if (hostlist != NULL && nhosts != nfrags) {
		fprintf(stderr, "%s: specified host ignored, "
			"because host number %d does't match "
			"fragment number %d\n",
			program_name, nhosts, nfrags);
		hostlist = NULL;
	}

	for (i = 0; i < nfrags; i++) {
		e = gfs_pio_open(gfarm_url, i,
				 hostlist != NULL ? hostlist[i] : NULL,
				 GFS_FILE_RDONLY, &gf);
		if (e != NULL)
			return (e);
		while ((c = gfs_pio_getc(gf)) != EOF)
			putc(c, ofp);
		gfs_pio_close(gf);
	}
	return (NULL);
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *hostfile = NULL;
	int ch, error_line, n = 0;
	gfarm_stringlist hostlist = NULL;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr,
			"%s: error: only one input file name expected\n",
			program_name);
		exit(1);
	}
	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	if (hostfile != NULL) {
		e = gfarm_hostlist_read(hostfile, &n, &hostlist, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: %s: line %d: %s\n",
					program_name, hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s: %s\n",
					program_name, hostfile, e);
			exit(1);
		}
	}
	e = gfexport(argv[0], stdout, n, hostlist);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

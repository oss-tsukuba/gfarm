#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfexport";

void
gfprint(GFS_File gf, FILE *ofp)
{
	int c;

	while ((c = gfs_pio_getc(gf)) != EOF)
		putc(c, ofp);
	if (gfs_pio_error(gf) != NULL)
		fprintf(stderr, "%s: error: %s\n",
			program_name, gfs_pio_error(gf));
}

char *
gfexport_section(char *gfarm_url, char *section, FILE *ofp)
{
	char *e;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != NULL)
		return (e);
	e = gfs_pio_set_view_section(gf, section, NULL, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		gfs_pio_close(gf);
		return (e);
	}
	gfprint(gf, ofp);
	gfs_pio_close(gf);
	return (NULL);
}

char *
gfexport(char *gfarm_url, FILE *ofp, int nhosts, char **hostlist)
{
	char *e;
	GFS_File gf;
	int i, nfrags;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != NULL)
		return (e);
	e = gfs_pio_get_nfragment(gf, &nfrags);
	if (e != NULL) {
		gfs_pio_close(gf);
		return (e);
	}

	if (hostlist != NULL && nhosts != nfrags) {
		fprintf(stderr, "%s: specified hosts are ignored, because "
			"host number %d does't match fragment number %d\n",
			program_name, nhosts, nfrags);
		hostlist = NULL;
	}

	for (i = 0; i < nfrags; i++) {
		e = gfs_pio_set_view_index(gf, nfrags, i,
		    hostlist != NULL ? hostlist[i] : NULL,
		    GFARM_FILE_SEQUENTIAL);
		if (e != NULL)
			break;
		gfprint(gf, ofp);
	}
	gfs_pio_close(gf);
	return (e);
}

/* just a test routine for global view (and default view) */
char *
gfexport_test(char *gfarm_url, FILE *ofp, int explicit)
{
	char *e;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != NULL)
		return (e);
	if (explicit) {
		/*
		 * global mode is default,
		 * so, we don't have to specify this explicitly.
		 */
		e = gfs_pio_set_view_global(gf, GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			gfs_pio_close(gf);
			return (e);
		}
	}
	gfprint(gf, ofp);
	gfs_pio_close(gf);
	return (NULL);
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\n");
	fprintf(stderr, "\t-s <section>\n");
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
	char *e, *hostfile = NULL, *section = NULL;
	int ch, error_line, n = 0, default_view = 0, global_view = 0;
	char **hostlist = NULL;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:dgs:")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			break;
		case 'd':
			default_view = 1;
			break;
		case 'g':
			global_view = 1;
			break;
		case 's':
			section = optarg;
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
	if (hostfile != NULL && section != NULL) {
		fprintf(stderr,
			"%s: error: option -H and option -s are exclusive\n",
			program_name);
		exit(1);
	}
	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	if (section != NULL) {
		e = gfexport_section(argv[0], section, stdout);
	} else if (global_view) {
		e = gfexport_test(argv[0], stdout, 1);
	} else if (default_view) {
		e = gfexport_test(argv[0], stdout, 0);
	} else {
		if (hostfile != NULL) {
			e = gfarm_hostlist_read(hostfile,
			    &n, &hostlist, &error_line);
			if (e != NULL) {
				if (error_line != -1)
					fprintf(stderr,
						"%s: %s: line %d: %s\n",
						program_name,
						hostfile, error_line, e);
				else
					fprintf(stderr, "%s: %s: %s\n",
						program_name, hostfile, e);
				exit(1);
			}
		}
		e = gfexport(argv[0], stdout, n, hostlist);
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, argv[0], e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

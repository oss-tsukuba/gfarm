#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

char *program_name = "gfexport";

gfarm_error_t
gfexport(GFS_File gf, FILE *ofp)
{
	int c;

	while ((c = gfs_pio_getc(gf)) != EOF)
		putc(c, ofp);
	return (gfs_pio_error(gf));
}

#if 0 /* XXX v2 not yet */
gfarm_error_t
gfexport_by_hosts(char *gfarm_url, int nhosts, char **hostlist, FILE *ofp)
{
	gfarm_error_t e, e2;
	GFS_File gf;
	int i, nfrags;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_pio_get_nfragment(gf, &nfrags);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_pio_close(gf);
		return (e);
	}

	if (hostlist != NULL && nhosts != nfrags) {
		fprintf(stderr, "%s: specified hostlist are ignored, because "
			"the number of hosts %d doesn't match "
			"the number of fragments %d.\n",
			program_name, nhosts, nfrags);
		hostlist = NULL;
	}

	for (i = 0; i < nfrags; i++) {
		e = gfs_pio_set_view_index(gf, nfrags, i, hostlist[i % nhosts],
		    GFARM_FILE_SEQUENTIAL);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		e = gfexport(gf, ofp);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfexport_section(char *gfarm_url, char *section, FILE *ofp)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_pio_set_view_section(gf, section, NULL, GFARM_FILE_SEQUENTIAL);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_pio_close(gf);
		return (e);
	}
	e = gfexport(gf, ofp);
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

/* just a test routine for global view */
gfarm_error_t
gfexport_global(char *gfarm_url, FILE *ofp)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/*
	 * global mode is default,
	 * so, we don't have to call pio_set_view_global() explicitly.
	 */
	e = gfs_pio_set_view_global(gf, GFARM_FILE_SEQUENTIAL);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfexport(gf, ofp);

	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}
#endif /* XXX v2 not yet */

/* just a test routine for default view */
gfarm_error_t
gfexport_default(char *gfarm_url, FILE *ofp)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfexport(gf, ofp);
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
#if 0 /* XXX v2 not yet */
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\n");
	fprintf(stderr, "\t-I <fragment>\n");
#endif
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	gfarm_error_t e;
	int ch;
	char *gfarm_url;
#if 0 /* XXX v2 not yet */
	char *hostfile = NULL, *section = NULL;
	int global_view = 0;
#endif

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}


#if 0 /* XXX v2 not yet */
	while ((ch = getopt(argc, argv, "H:I:g?")) != -1)
#else
	while ((ch = getopt(argc, argv, "?")) != -1)
#endif
	{
		switch (ch) {
#if 0 /* XXX v2 not yet */
		case 'H':
			hostfile = optarg;
			break;
		case 'I':
			section = optarg;
			break;
		case 'g':
			global_view = 1;
			break;
#endif
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	gfarm_url = argv[0];

#if 1 /* XXX v2 not yet */
	e = gfexport_default(gfarm_url, stdout);
#else
	if (hostfile != NULL && section != NULL) {
		fprintf(stderr,
			"%s: error: option -H and option -I are exclusive\n",
			program_name);
		exit(1);
	}

	if (hostfile != NULL) {
		int error_line, n;
		char **hostlist;

		e = gfarm_hostlist_read(hostfile, &n, &hostlist, &error_line);
		if (e != GFARM_ERR_NO_ERROR) {
			if (error_line != -1)
				fprintf(stderr, "%s: %s: line %d: %s\n",
				    program_name, hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, hostfile, e);
			exit(1);
		}
		e = gfexport_by_hosts(gfarm_url, n, hostlist, stdout);
	} else if (section != NULL) {
		e = gfexport_section(gfarm_url, section, stdout);
	} else if (global_view) {
		e = gfexport_global(gfarm_url, stdout);
	} else {
		e = gfexport_default(gfarm_url, stdout);
	}
#endif
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s: %s\n", program_name, gfarm_url,
		    gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	return (0);
}

/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <gfarm/gfarm.h>

/*
 *  Create a hostfile.
 *
 *  gfsched <gfarm_url> [<hostfile>]
 *  gfsched -f <gfarm_file> [<hostfile>]
 */

char *program_name = "gfsched";

void
usage()
{
    fprintf(stderr, "usage: %s [-f] gfarm_file [hostfile]\n",
	    program_name);
    fflush(stderr);
}

int
main(int argc, char * argv[])
{
    char * gfarm_url = (char *)NULL;
    char * section = (char *)NULL;
    char * e = (char *)NULL;
    FILE * outp = stdout;
    int errflg = 0;
    extern int optind;
    int nhosts;
    char **hosts;
    char * gfarm_file;
    char * host;
    int c, i;

    if (argc >= 1)
	program_name = basename(argv[0]);
    e = gfarm_initialize(&argc, &argv);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    while ((c = getopt(argc, argv, "I:?")) != EOF) {
	switch (c) {
	case 'I': /* This is not for casual use, but for debugging purpose. */
	    section = optarg;
	    break;
	case '?':
	default:
	    ++errflg;
	}
    }
    if (errflg) {
	usage();
	exit(2);
    }

    if (optind < argc) {
	gfarm_url = argv[optind];
    }
    else {
	usage();
	exit(2);
    }
    ++optind;
    if (optind < argc) {
	outp = fopen(argv[optind], "w");
	if (outp == (FILE *)NULL) {
	    perror(argv[optind]);
	    exit(1);
	}
    }

    if (section == NULL) {
	e = gfarm_url_hosts_schedule(gfarm_url, (char *)NULL,
				     &nhosts, &hosts);
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, e);
	    exit(1);
	}

	for (i = 0; i < nhosts; ++i)
	    fprintf(outp, "%s\n", hosts[i]);
    }
    else {
	/* This is not for casual use, but for debugging purpose. */

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, e);
	    exit(1);
	}
	e = gfarm_file_section_host_schedule(gfarm_file, section, &host);
	if (e != NULL) {
	    fprintf(stderr, "%s: %s:%s: %s\n", program_name,
		    gfarm_url, section, e);
	    exit(1);
	}

	fprintf(outp, "%s\n", host);
	free(host);
	free(gfarm_file);
    }

    fclose(outp);

    e = gfarm_terminate();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    exit(0);
}

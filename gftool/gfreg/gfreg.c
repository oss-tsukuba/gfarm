#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "gfarm.h"

/*
 *  Register a local file to the Gfarm Meta DB.
 *
 *  gfreg <local_filename> <gfarm_url>
 */

char *program_name = "gfreg";

void
usage()
{
    fprintf(stderr, "Usage: %s <local_filename> <gfarm_url>\n",
	    program_name);
    exit(1);
}

int
main(int argc, char * argv[])
{
    char * filename, * gfarm_url;
    int node_index = -1, total_nodes = -1;
    char * e = (char *)NULL;
    extern char * optarg;
    extern int optind;
    int c;

    /*  Command options  */

    if (argc >= 1)
	program_name = argv[0];

    while ((c = getopt(argc, argv, "I:N:")) != -1) {
	switch (c) {
	case 'I':
	    node_index = strtol(optarg, NULL, 0);
	    break;
	case 'N':
	    total_nodes = strtol(optarg, NULL, 0);
	    break;
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
	fprintf(stderr,
		"%s: missing a local filename\n",
		program_name);
	usage();
	exit(1);
    }
    filename = argv[0];
    argc--;
    argv++;

    if (argc == 0) {
	fprintf(stderr,
		"%s: missing a Gfarm URL\n",
		program_name);
	usage();
	exit(1);
    }
    gfarm_url = argv[0];
    argc--;
    argv++;

    if (node_index < 0) {
	fprintf(stderr, "%s: missing a Gfarm index\n",
		program_name);
	usage();
	exit(1);
    }

    /* */

    e = gfarm_initialize();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    e = gfs_pio_set_fragment_info_local(filename, gfarm_url, node_index);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    e = gfarm_terminate();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    exit(0);
}

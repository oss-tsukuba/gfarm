#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gfarm/gfarm.h>

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
    int argc_save = argc;
    char **argv_save = argv;
    char * filename, * gfarm_url;
    int node_index = -1, total_nodes = -1;
    char * e = NULL, * architecture = NULL;
    struct stat s;
    extern char * optarg;
    extern int optind;
    int c;

    /*  Command options  */

    if (argc >= 1)
	program_name = argv[0];

    while ((c = getopt(argc, argv, "I:N:a:")) != -1) {
	switch (c) {
	case 'I':
	    node_index = strtol(optarg, NULL, 0);
	    break;
	case 'N':
	    total_nodes = strtol(optarg, NULL, 0);
	    break;
	case 'a':
	    architecture = optarg;
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

    /* */

    e = gfarm_initialize(&argc_save, &argv_save);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    if (stat(filename, &s) != -1 && S_ISREG(s.st_mode) &&
	(s.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0 && node_index < 0) {
	if (architecture == NULL) {
	    char *self_hostname;

	    if (gfarm_host_get_canonical_self_name(&self_hostname) == NULL)
		architecture =
		    gfarm_host_info_get_architecture_by_host(self_hostname);
	}
	if (architecture == NULL) {
	    fprintf(stderr, "%s: missing -a <architecture> for %s on %s\n",
		    program_name, filename, gfarm_host_get_self_name());
	    usage();
	    exit(1);
	}
	if (total_nodes <= 0)
	    total_nodes = 1;
	e = gfarm_url_program_register(gfarm_url, architecture,
				       filename, total_nodes);
    } else {
	if (node_index < 0) {
	    fprintf(stderr, "%s: missing -I <Gfarm index>\n",
		    program_name);
	    usage();
	    exit(1);
	}
	e = gfs_pio_set_fragment_info_local(filename, gfarm_url, node_index);
    }
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

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
 *  gfsched <gfarm_url> [<output_hostfile>]
 *  gfsched -f <gfarm_file> [<output_hostfile>]
 */

char *program_name = "gfsched";

void
usage(void)
{
    fprintf(stderr, "usage:\t%s [options] gfarm_url [output_hostfile]\n",
	    program_name);
    fprintf(stderr,
	    "\t%s -N <number> [-p <gfarm_url>] [<host>...]\n",
	    program_name);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t-I <fragment>\tschedules a node"
	    " using the specified fragment.\n");
    fprintf(stderr, "\t-p <gfarm_url>\tprogram URL"
	    " to process the file.\n");
    fflush(stderr);
}

int
main(int argc, char * argv[])
{
    char *gfarm_url = NULL;
    char *section = NULL;
    char *program = NULL;
    char *e = NULL;
    FILE *outp = stdout;
    int errflg = 0;
    extern int optind;
    int nhosts = -1;
    char **hosts;
    char *gfarm_file;
    char *host;
    int c, i;

    if (argc >= 1)
	program_name = basename(argv[0]);

    e = gfarm_initialize(&argc, &argv);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    while ((c = getopt(argc, argv, "I:N:p:?")) != EOF) {
	switch (c) {
	case 'I':
	    section = optarg;
	    break;
	case 'N':
	    nhosts = atoi(optarg);
	    break;
	case 'p':
	    program = optarg;
	    if (!gfarm_is_url(program)) {
		fprintf(stderr, "%s: only gfarm-URL can be specified"
			" as a parameter of -p option\n", program_name);
		exit(2);
	    }
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
    if (section != NULL && nhosts != -1) {
	fprintf(stderr, "%s: -I and -N option cannot be specified at once.\n",
		program_name);
	exit(2);
    }

    if (nhosts == -1) {
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
	    if (outp == NULL) {
		perror(argv[optind]);
		exit(1);
	    }
	}
    }

    if (nhosts != -1) {
	hosts = malloc(sizeof(*hosts) * nhosts);
	if (hosts == NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
	    exit(2);
	}
	if (optind < argc) {
	    for (i = optind; i < argc; i++) {
		e = gfarm_host_get_canonical_name(argv[i], &argv[i]);
		if (e != NULL) {
		    fprintf(stderr, "%s: %s: %s\n", program_name, argv[i], e);
		    exit(2);
		}
	    }
	    if (program != NULL) {
		e = "currently, hosts cannot be specified, "
		    "if both -N and -p option are used.";
	    }
	    else {
		e = gfarm_schedule_search_idle_hosts(
		    argc - optind, &argv[optind],
		    nhosts, hosts);
	    }
	}
	else {
	    if (program != NULL) {
		e = gfarm_schedule_search_idle_by_program(program,
							  nhosts, hosts);
	    }
	    else {
		e = gfarm_schedule_search_idle_by_all(nhosts, hosts);
	    }
	}
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, e);
	    exit(2);
	}
	for (i = 0; i < nhosts; ++i)
	    printf("%s\n", hosts[i]);
    }
    else if (section == NULL) {
	/* file-affinity scheduling */
	if (program != NULL) {
	    e = gfarm_url_hosts_schedule_by_program(gfarm_url, program, NULL,
						    &nhosts, &hosts);
	}
	else {
	    e = gfarm_url_hosts_schedule(gfarm_url, NULL,
					 &nhosts, &hosts);
	}
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, e);
	    exit(1);
	}

	for (i = 0; i < nhosts; ++i)
	    fprintf(outp, "%s\n", hosts[i]);
    }
    else {
	/* schedule a node using the specific file fragment. */
	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL) {
	    fprintf(stderr, "%s: %s\n", program_name, e);
	    exit(1);
	}
	if (program != NULL) {
	    e = gfarm_file_section_host_schedule_by_program(gfarm_file,
							    section,
							    program,
							    &host);
	}
	else {
	    e = gfarm_file_section_host_schedule(gfarm_file, section, &host);
	}
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

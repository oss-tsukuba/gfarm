#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfrm";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\t"
	    "remove replicated fragments on nodes listed in hostfile\n");
	fprintf(stderr, "\t-I <fragment>\t"
	    "remove the specified fragment replica with -H option\n");
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
	int i, ch, nhosts, error_line;
	char **hosttab;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:I:fr")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			break;
		case 'I':
			section = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (hostfile == NULL) {
		if (section != NULL) {
			fprintf(stderr,
			    "%s: the -I option requires the -H <hostfile> option\n",
			    program_name);
			exit(1);
		}
	} else {
		if (section == NULL) {
			fprintf(stderr,
			    "%s: the -H option requires the -I <fragment> option\n",
			    program_name);
			exit(1);
		}
		e = gfarm_hostlist_read(hostfile,
		    &nhosts, &hosttab, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: line %d: %s\n",
					hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s\n",
					program_name, e);
			exit(1);
		}
	}

	for (i = 0; i < argc; i++) {
		if (hostfile != NULL && section != NULL)
			e = gfs_unlink_section_replica(argv[i], section,
			    nhosts, hosttab);
		else
			e = gfs_unlink(argv[i]);
		if (e != NULL)
			fprintf(stderr, "%s: %s\n", argv[i], e);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfreplicate";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>\n", program_name);
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
	int ch, nhosts, error_line;
	char **hosttab;

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
	if (argc != 1)
		usage();

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (hostfile == NULL) {
		fprintf(stderr, "%s: -H <hostfile> option is required\n",
			program_name);
		exit(1);
	}
	e = gfarm_hostlist_read(hostfile, &nhosts, &hosttab, &error_line);
	if (e != NULL) {
		if (error_line != -1)
			fprintf(stderr, "%s: line %d: %s\n",
				hostfile, error_line, e);
		else
			fprintf(stderr, "%s: %s\n",
				program_name, e);
		exit(1);
	}
	e = gfarm_url_fragments_replicate(argv[0], nhosts, hosttab);
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

/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfrep";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <gfarm_url>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-D <domainname>\t\treplicate a whole file\n");
	fprintf(stderr, "\t-I fragment-index\treplicate a fragment"
		" with -d option\n");
	fprintf(stderr, "\t-s src-node\n");
	fprintf(stderr, "\t-d dest-node\n");
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
	char *index = NULL, *src = NULL, *dest = NULL, *domainname = NULL;
	char *gfarm_url;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:I:s:d:D:")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			break;
		case 'I':
			index = optarg;
			break;
		case 's':
			src = optarg;
			break;
		case 'd':
			dest = optarg;
			break;
		case 'D':
			domainname = optarg;
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
	gfarm_url = argv[0];

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (index != NULL) {
		/* replicate a section */
		if (hostfile != NULL)
			fprintf(stderr, "%s: warning: -H option is ignored\n",
				program_name);
		if (dest == NULL) {
			fprintf(stderr, "%s: -d dest-node option is required\n",
				program_name);
			exit(1);
		}
		if (src == NULL)
			e = gfarm_url_section_replicate_to(gfarm_url,
				index, dest);
		else
			e = gfarm_url_section_replicate_from_to(gfarm_url,
				index, src, dest);
	} else {
		/* replicate a whole file */
		if (hostfile != NULL) {
			if (domainname != NULL)
				fprintf(stderr, "%s: warning: -D is ignored\n",
					program_name);
			e = gfarm_hostlist_read(hostfile, &nhosts,
				&hosttab, &error_line);
			if (e != NULL) {
				if (error_line != -1)
					fprintf(stderr, "%s: line %d: %s\n",
						hostfile, error_line, e);
				else
					fprintf(stderr, "%s: %s\n",
						program_name, e);
				exit(1);
			}
			e = gfarm_url_fragments_replicate(gfarm_url,
				nhosts, hosttab);
		} else if (domainname != NULL) {
			e = gfarm_url_fragments_replicate_to_domainname(
				gfarm_url, domainname);
		} else
			usage();
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s: %s\n", program_name, gfarm_url, e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

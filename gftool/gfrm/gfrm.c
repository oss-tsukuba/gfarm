/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfrm";

void
usage()
{
	fprintf(stderr, "Usage: %s [ -h <host> ] <gfarm_url>...\n",
		program_name);
	fprintf(stderr, "       %s -I <fragment> -h <host> <gfarm_url>...\n",
		program_name);
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
	char *e, *section = NULL;
	int ch, nhosts = 0;
	char **hosttab;
	gfarm_stringlist host_list;

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_stringlist_init(&host_list);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "h:I:fr")) != -1) {
		switch (ch) {
		case 'h':
			e = gfarm_stringlist_add(&host_list, optarg);
			if (e != NULL) {
				fprintf(stderr, "%s: %s\n",
					program_name, e);
				exit(1);
			}
			++nhosts;
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
	if (argc == 0) {
		fprintf(stderr, "%s: too few arguments\n",
			program_name);
		exit(1);
	}

	if (section == NULL) {
		if (nhosts == 0) {
			int i;
			/* remove a whole file */
			for (i = 0; i < argc; i++) {
				e = gfs_unlink(argv[i]);
				if (e != NULL)
					fprintf(stderr, "%s: %s\n",
						argv[i], e);
			}
		}
		else {
			/*
			 * remove file replicas of a whole file
			 * on a specified node.
			 */
			int i, j;
			hosttab = gfarm_strings_alloc_from_stringlist(
				&host_list);
			gfarm_stringlist_free(&host_list);
			for (j = 0; j < nhosts; j++) {
				for (i = 0; i < argc; i++) {
					e = gfs_unlink_replicas_on_host(
						argv[i], hosttab[j]);
					if (e != NULL)
						fprintf(stderr, "%s: %s\n",
							argv[i], e);
				}
			}
		}
	} else {
		int i;
		/* remove a file fragment */
		if (nhosts == 0) {
			fprintf(stderr, "%s: -h option should be specified\n", 
				program_name);
			exit(1);
		}
		/* assert(nhosts == gfarm_stringlist_length(&host_list)); */
		hosttab = gfarm_strings_alloc_from_stringlist(&host_list);
		gfarm_stringlist_free(&host_list);

		for (i = 0; i < argc; i++) {
			e = gfs_unlink_section_replica(argv[i], section,
				nhosts, hosttab);
			if (e != NULL)
				fprintf(stderr, "%s: %s\n", argv[i], e);
		}
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

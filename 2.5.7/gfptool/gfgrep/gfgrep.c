/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <regex.h>
#include <gfarm/gfarm.h>

char *program_name = "gfarm_grep";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <regexp> <input_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-o <output_gfarm_file>\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	int c, total_nodes = -1, node_index = -1;
	char *regexp = NULL;
	char *e, *input, *output = NULL;
	regex_t re;
	struct gfs_file *igf;
	struct gfs_file *ogf;
	int eof;
	char line[1024];

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (argc >= 1)
		program_name = argv[0];

	while ((c = getopt(argc, argv, "e:o:")) != -1) {
		switch (c) {
		case 'e':
			regexp = optarg;
			break;
		case 'o':
			output = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfs_pio_get_node_rank(&node_index);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfs_pio_get_node_size(&total_nodes);
	if (total_nodes <= 0) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (regexp == NULL) {
		if (argc == 0) {
			if (node_index == 0) {
				fprintf(stderr,
					"%s: missing regular expression\n",
					program_name);
				usage();
			}
			exit(1);
		}
		regexp = argv[0];
		argc--;
		argv++;
	}
	if (output == NULL) {
		if (node_index == 0) {
			fprintf(stderr,
				"%s: \"-o <output gfarm_file>\" is needed\n",
				program_name);
		}
		exit(1);
	}
	if (argc == 0) {
		if (node_index == 0) {
			fprintf(stderr, "%s: input file name is mandatory\n",
				program_name);
		}
		exit(1);
	}
	input = argv[0];
	argc--;
	argv++;
	if (argc != 0) {
		if (node_index == 0) {
			fprintf(stderr,
				"%s: currently, "
				"only one input file is supported\n",
				program_name);
		}
		exit(1);
	}
	if (regcomp(&re, regexp, REG_NOSUB) != 0) {
		if (node_index == 0) {
			fprintf(stderr, "%s: invalid regular expression\n",
				program_name);
		}
		exit(1);
	}

	e = gfs_pio_create(output, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	    &ogf);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, output, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(ogf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, set_view_local(%s): %s\n",
			program_name, node_index, output, e);
		exit(1);
	}
	e = gfs_pio_open(input, GFARM_FILE_RDONLY, &igf);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, input, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(igf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr,
			"%s: node %d, set_view_local(%s): %s\n",
			program_name, node_index, input, e);
		exit(1);
	}
	for (;;) {
		e = gfs_pio_getline(igf,
		    line, sizeof line, &eof);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", input, e);
			break;
		}
		if (eof)
			break;
		if (regexec(&re, line, 0, NULL, 0) == 0)
			gfs_pio_putline(ogf, line);
	}
	gfs_pio_close(igf);
	gfs_pio_close(ogf);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	regfree(&re);

	return (0);
}

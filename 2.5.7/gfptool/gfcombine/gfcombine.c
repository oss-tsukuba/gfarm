/*
 * $Id$
 *
 * Reduce the number of fragments by nth.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gfarm/gfarm.h>

char *program_name = "gfarm_combine";
#define default_output "gfarm:combine.out"

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-n <combined_frag_number>\tdefault 2\n");
	fprintf(stderr, "\t-o <output_gfarm_file>\t\tdefault %s\n",
		default_output);
	exit(1);
}

#define BUFSIZE	65536

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int c, total_nodes = -1, node_index = -1;
	int output_nodes, output_index;
	char *e, *input, *output = default_output;
	GFS_File igf, ogf;
	int n = 2, i;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (argc >= 1)
		program_name = argv[0];

	while ((c = getopt(argc, argv, "o:n:")) != -1) {
		switch (c) {
		case 'n':
			n = atoi(optarg);
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
	if (argc == 0) {
		if (node_index == 0) {
			fprintf(stderr, "%s: missing input file name\n",
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

	output_nodes = (total_nodes + n - 1) / n;
	output_index = node_index / n;

	if (output_index * n != node_index) goto terminate;

	/* Only combined nodes run this part. */

	e = gfs_pio_create(output, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	    &ogf);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, output, e);
		exit(1);
	}
	e = gfs_pio_set_view_index(ogf, output_nodes,
		output_index, NULL, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, set_view_index(%s): %s\n",
			program_name, node_index, output, e);
		exit(1);
	}

	e = gfs_pio_open(input, GFARM_FILE_RDONLY, &igf);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, input, e);
		exit(1);
	}
	for (i = 0; i < n; ++i) {
		if (node_index + i < total_nodes) {
			e = gfs_pio_set_view_index(igf, GFARM_FILE_DONTCARE,
				node_index + i, NULL, GFARM_FILE_SEQUENTIAL);
			if (e != NULL) {
				fprintf(stderr,
					"%s: node %d, set_view_index(%s): %s\n",
					program_name, node_index, input, e);
				exit(1);
			}
			/* copy this fragment */
			for (;;) {
				int rv, wv;
				char buffer[BUFSIZE];
				e = gfs_pio_read(igf, buffer, sizeof(buffer),
					&rv);
				if (e != NULL || rv == 0)
					break;
				e = gfs_pio_write(ogf, buffer, rv, &wv);
				if (e != NULL)
					break;
			}
		}
	}

	e = gfs_pio_close(igf);
	if (e != NULL)
	    fprintf(stderr, "%s: close failed: %s\n", input, e);
	e = gfs_pio_close(ogf);
	if (e != NULL)
	    fprintf(stderr, "%s: close failed: %s\n", output, e);

 terminate:
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

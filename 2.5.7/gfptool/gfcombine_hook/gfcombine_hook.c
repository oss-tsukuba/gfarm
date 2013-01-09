/*
 * $Id$
 *
 * Reduce the number of fragments by nth using gfs_hook.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#include <gfarm/gfs_hook.h>

char *program_name = "gfarm_combine_hook";
#define default_output "gfarm:combine_hook.out"

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
	int ifd, ofd;
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

	gfs_hook_set_default_view_index(output_index, output_nodes);
	ofd = creat(output, 0666);
	if (ofd == -1) {
		perror(output);
		exit(1);
	}

	for (i = 0; i < n; ++i) {
		if (node_index + i < total_nodes) {
			gfs_hook_set_default_view_index(node_index + i,
				GFARM_FILE_DONTCARE);
			ifd = open(input, O_RDONLY);
			if (ifd == -1) {
				perror(input);
				exit(1);
			}
			/* copy this fragment */
			for (;;) {
				int rv, wv;
				char buffer[BUFSIZE];
				rv = read(ifd, buffer, sizeof(buffer));
				if (rv <= 0)
					break;
				wv = write(ofd, buffer, rv);
				if (wv <= 0)
					break;
			}
			if (close(ifd))
				perror(input);
		}
	}
	if (close(ofd))
		perror(output);

 terminate:
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

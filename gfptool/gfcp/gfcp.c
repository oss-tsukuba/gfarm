/*
 * $Id$
 *
 * Copy a file
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>

char *program_name = "gfcp";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file> <output_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-p\tpreserve file status\n");
	exit(1);
}

#define BUFSIZE	65536

int
main(int argc, char *argv[])
{
	int total_nodes, node_index;
	int flag_preserve = 0;
	char *e, c, *input, *output;
	GFS_File igf, ogf;
	struct gfs_stat gstat;
	gfarm_mode_t mode;
	struct gfarm_timespec gtspec[2];
	extern char *optarg;
	extern int optind;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (argc >= 1)
		program_name = argv[0];

	while ((c = getopt(argc, argv, "ph")) != -1) {
		switch (c) {
		case 'p':
			flag_preserve = 1;
			break;
		case 'h':
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
	if (e != NULL) {
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

	if (argc == 0) {
		if (node_index == 0) {
			fprintf(stderr, "%s: missing output file name\n",
				program_name);
		}
		exit(1);
	}
	output = argv[0];
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
	if (strcmp(input, output) == 0) {
		fprintf(stderr, "%s: %s and %s are the same file\n",
			program_name, input, output);
		exit(1);
	}

	e = gfs_stat(input, &gstat);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, input, e);
		exit(1);
	}
	if (flag_preserve) {
		mode = gstat.st_mode;
		gtspec[0] = gstat.st_atimespec;
		gtspec[1] = gstat.st_mtimespec;
	}
	else
		mode = 0666;
	gfs_stat_free(&gstat);

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

	/* check whether the destination is a directory or not */
	e = gfs_stat(output, &gstat);
	if (e == NULL && GFARM_S_ISDIR(gstat.st_mode)) {
		if (node_index == 0) {
			fprintf(stderr, "%s: is a directory\n", output);
		}
		exit(1);
	}

	e = gfs_pio_create(output, GFARM_FILE_WRONLY, mode, &ogf);
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

	/* copy this fragment */
	for (;;) {
		int rv, wv;
		char buffer[BUFSIZE];
		e = gfs_pio_read(igf, buffer, sizeof(buffer), &rv);
		if (e != NULL || rv == 0)
			break;
		e = gfs_pio_write(ogf, buffer, rv, &wv);
		if (e != NULL)
			break;
	}
	if (e != NULL)
		fprintf(stderr, "%s\n", e);

	e = gfs_pio_close(igf);
	if (e != NULL)
	    fprintf(stderr, "%s: close failed: %s\n", input, e);
	e = gfs_pio_close(ogf);
	if (e != NULL)
	    fprintf(stderr, "%s: close failed: %s\n", output, e);

	if (flag_preserve) {
		e = gfs_utimes(output, gtspec);
		if (e != NULL)
			fprintf(stderr, "%s: utimes failed: %s\n", output, e);
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <gfarm/gfarm.h>
#include <mpi.h>

char *program_name = "gfwc";

#if 0
# define FILE_TYPE	FILE *
# define GETC(file)	getc(file)
#else
# define FILE_TYPE	GFS_File
# define GETC(file)	gfs_pio_getc(file)
#endif

void
wordcount(FILE_TYPE file, long *result)
{
	int c;
	long linec, wordc, charc;
	int inword = 0;

	linec = wordc = charc = 0;

	while ((c = GETC(file)) != EOF) {
		charc++;
		if (c == '\n')
			linec++;
		if (isspace(c)) {
			if (inword)
				inword = 0;
		} else {
			if (!inword) {
				inword = 1;
				wordc++;
			}
		}
	}
	result[0] = linec;
	result[1] = wordc;
	result[2] = charc;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int argc_save = argc;
	char **argv_save = argv;
	int total_nodes, node_index;
	char *e, *input;
	struct gfs_file *igf;
	long local_result[3], total_result[3];

	if (argc >= 1)
		program_name = argv[0];

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &node_index);
	MPI_Comm_size(MPI_COMM_WORLD, &total_nodes);
#if 0
	printf("running on %d/%d\n", node_index, total_nodes);
#endif

	if (argc <= 1) {
		if (node_index == 0) {
			fprintf(stderr, "%s: input file name is mandatory\n",
				program_name);
		}
		MPI_Finalize();
		exit(1);
	}
	argc--;
	argv++;
	input = argv[0];
	if (argc != 1) {
		if (node_index == 0) {
			fprintf(stderr,
				"%s: currently, "
				"only one input file is supported\n",
				program_name);
		}
		MPI_Finalize();
		exit(1);
	}

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	gfs_pio_set_local(node_index, total_nodes);

	e = gfs_pio_open(input, GFARM_FILE_RDONLY, &igf);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, cannot open %s: %s\n",
			program_name, node_index, input, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(igf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "%s: node %d, set_view_local(%s): %s\n",
			program_name, node_index, input, e);
		gfs_pio_close(igf);
		exit(1);
	}
	wordcount(igf, local_result);
	gfs_pio_close(igf);

	MPI_Reduce(local_result, total_result, 3, MPI_INT,
	    MPI_SUM, 0, MPI_COMM_WORLD);

	if (node_index == 0)
		printf("\t%ld\t%ld\t%ld\n",
		       total_result[0], total_result[1], total_result[2]);

	MPI_Finalize();

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

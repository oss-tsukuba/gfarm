/*
 * $Id$
 */

#include <unistd.h>
#include <mpi.h>
#include <gfarm.h>

char *
gfs_hook_initialize(void)
{
	int rank, size;
	char *e;

	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL)
		return e;

	gfs_pio_set_local(rank, size);

	return NULL;
}

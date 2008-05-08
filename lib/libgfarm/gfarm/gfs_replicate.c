/*
 * $Id$
 */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfs_client.h"
#include "schedule.h"

static gfarm_error_t
gfs_replicate_from_to_internal(GFS_File gf, char *srchost, int srcport,
	char *dsthost, int dstport)
{
	struct gfs_connection *server;
	gfarm_error_t e;

	e = gfs_client_connection_acquire_by_host(dsthost, dstport, &server);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (gfs_client_pid(server) == 0)
		e = gfarm_client_process_set(server);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_client_replica_add_from(
			server, srchost, srcport, gfs_pio_fileno(gf));
		gfs_client_connection_free(server);
	}
	return (e);
}

gfarm_error_t
gfs_replicate_to(char *file, char *dsthost, int dstport)
{
	char *srchost;
	int srcport;
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(file, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfarm_schedule_file(gf, &srchost, &srcport);
	if (e != GFARM_ERR_NO_ERROR)
		goto close;
	e = gfs_replicate_from_to_internal(gf, srchost, srcport,
		dsthost, dstport);
	free(srchost);
 close:
	e2 = gfs_pio_close(gf);

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfs_replicate_from_to(char *file, char *srchost, int srcport,
	char *dsthost, int dstport)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	if (srchost == NULL)
		return (gfs_replicate_to(file, dsthost, dstport));

	e = gfs_pio_open(file, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfs_replicate_from_to_internal(gf, srchost, srcport,
		dsthost, dstport);
	e2 = gfs_pio_close(gf);

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

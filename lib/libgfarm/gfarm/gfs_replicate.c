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
	int retry = 0;

	e = gfs_client_connection_acquire_by_host(gfarm_metadb_server,
	    dsthost, dstport, &server);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (;;) {
		if (gfs_client_pid(server) == 0)
			e = gfarm_client_process_set(server,
			    gfarm_metadb_server);
		if (e == GFARM_ERR_NO_ERROR) {
			e = gfs_client_replica_add_from(
				server, srchost, srcport, gfs_pio_fileno(gf));
			gfs_client_connection_free(server);
			if (gfs_client_is_connection_error(e) && ++retry<=1 &&
			    gfs_client_connection_acquire_by_host(
			    gfarm_metadb_server, dsthost, dstport,
			    &server) == GFARM_ERR_NO_ERROR)
				continue;
		}

		break;
	}
	return (e);
}

static gfarm_error_t
gfs_replicate_to_internal(char *file, char *dsthost, int dstport, int migrate)
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
	if (e == GFARM_ERR_NO_ERROR && migrate)
		e = gfs_replica_remove_by_file(file, srchost);
	free(srchost);
 close:
	e2 = gfs_pio_close(gf);

	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfs_replicate_to_local(GFS_File gf, char *srchost, int srcport)
{
	gfarm_error_t e;
	char *self;
	int port;

	e = gfarm_host_get_canonical_self_name(gfarm_metadb_server,
	    &self, &port);
	if (e != GFARM_ERR_NO_ERROR)
		goto error;
	e = gfs_replicate_from_to_internal(gf, srchost, srcport, self, port);
 error:
	return (e);
}

gfarm_error_t
gfs_replicate_to(char *file, char *dsthost, int dstport)
{
	return (gfs_replicate_to_internal(file, dsthost, dstport, 0));
}

gfarm_error_t
gfs_migrate_to(char *file, char *dsthost, int dstport)
{
	return (gfs_replicate_to_internal(file, dsthost, dstport, 1));
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

gfarm_error_t
gfs_migrate_from_to(char *file, char *srchost, int srcport,
	char *dsthost, int dstport)
{
	gfarm_error_t e;

	e = gfs_replicate_from_to(file, srchost, srcport, dsthost, dstport);
	return (e != GFARM_ERR_NO_ERROR ? e :
		gfs_replica_remove_by_file(file, srchost));
}

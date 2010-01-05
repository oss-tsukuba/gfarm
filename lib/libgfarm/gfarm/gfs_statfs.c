/*
 * $Id$
 */

#include <unistd.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfm_client.h"
#include "config.h"

/* XXX FIXME: implement gfs_statvfs() which takes a path as an argument */

gfarm_error_t
gfs_statfs(gfarm_off_t *used, gfarm_off_t *avail, gfarm_off_t *files)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;

	for (;;) {
		if ((e = gfm_client_connection_and_process_acquire(
		    gfarm_metadb_server_name, gfarm_metadb_server_port,
		    &gfm_server)) != GFARM_ERR_NO_ERROR)
			return (e);

		e = gfm_client_statfs(gfm_server, used, avail, files);
		if (gfm_client_is_connection_error(e) && ++retry <= 1){
			gfm_client_connection_free(gfm_server);
			continue;
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	return (e);

}

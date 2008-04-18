/*
 * $Id$
 */

#include <unistd.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfm_client.h"
#include "config.h"

gfarm_error_t
gfs_statfs(gfarm_off_t *used, gfarm_off_t *avail, gfarm_off_t *files)
{
	return (gfm_client_statfs(gfarm_metadb_server, used, avail, files));
}

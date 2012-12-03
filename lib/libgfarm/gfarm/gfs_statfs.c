/*
 * $Id$
 */

#include <unistd.h>
#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfm_client.h"
#include "lookup.h"
#include "config.h"

gfarm_error_t
gfs_statfs(gfarm_off_t *used, gfarm_off_t *avail, gfarm_off_t *files)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	const char *path = GFARM_PATH_ROOT;

	for (;;) {
		if ((e = gfarm_url_parse_metadb(&path, &gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002667,
			    "gfarm_url_parse_metadb failed: %s",
			    gfarm_error_string(e));
			return (e);
		}

		e = gfm_client_statfs(gfm_server, used, avail, files);
		if (gfm_client_is_connection_error(e) && ++retry <= 1) {
			gfm_client_connection_free(gfm_server);
			continue;
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	return (e);

}

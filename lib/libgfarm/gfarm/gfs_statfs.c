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
#include "gfs_failover.h"

struct statfs_info {
	const char *path;
	gfarm_off_t *used, *avail, *files;
};

static gfarm_error_t
statfs_rpc(struct gfm_connection **gfm_serverp, void *closure)
{
	gfarm_error_t e;
	struct statfs_info *si = closure;

	e = gfm_client_connection_and_process_acquire_by_path(
	    si->path, gfm_serverp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003977,
		    "gfm_client_connection_and_process_acquire_by_path: %s",
		    gfarm_error_string(e));
		return (e);
	}
	gfm_client_connection_lock(*gfm_serverp);
	e = gfm_client_statfs(*gfm_serverp, si->used, si->avail, si->files);
	gfm_client_connection_unlock(*gfm_serverp);
	return (e);
}

static gfarm_error_t
statfs_post_failover(struct gfm_connection *gfm_server, void *closure)
{
	if (gfm_server)
		gfm_client_connection_free(gfm_server);
	return (GFARM_ERR_NO_ERROR);
}

static void
statfs_exit(struct gfm_connection *gfm_server, gfarm_error_t e,
	void *closure)
{
	(void)statfs_post_failover(gfm_server, closure);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003978,
		    "gfs_statfs: %s",
		    gfarm_error_string(e));
}

gfarm_error_t
gfs_statfs_by_path(const char *path,
	gfarm_off_t *used, gfarm_off_t *avail, gfarm_off_t *files)
{
	struct statfs_info si = {
		path, used, avail, files
	};

	return (gfm_client_rpc_with_failover(statfs_rpc, statfs_post_failover,
	    statfs_exit, NULL, &si));
}

gfarm_error_t
gfs_statfs(gfarm_off_t *used, gfarm_off_t *avail, gfarm_off_t *files)
{
	return (gfs_statfs_by_path(GFARM_PATH_ROOT, used, avail, files));
}

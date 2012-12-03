/*
 * $Id$
 */

#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfm_client.h"
#include "config.h"
#include "lookup.h"

struct gfm_replica_list_by_name_closure {
	gfarm_int32_t n;
	char **hosts;
};

static gfarm_error_t
gfm_replica_list_by_name_request(struct gfm_connection *gfm_server,
	void *closure)
{
	gfarm_error_t e = gfm_client_replica_list_by_name_request(gfm_server);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000151,
		    "replica_list_by_name request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_replica_list_by_name_result(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replica_list_by_name_closure *c = closure;
	gfarm_error_t e = gfm_client_replica_list_by_name_result(
		gfm_server, &c->n, &c->hosts);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000152,
		    "replica_list_by_name result: %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

static void
gfm_replica_list_by_name_cleanup(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replica_list_by_name_closure *c = closure;
	int i;

	for (i = 0; i < c->n; i++)
		free(c->hosts[i]);
	free(c->hosts);
}

gfarm_error_t
gfs_replica_list_by_name(const char *path, int *np, char ***hostsp)
{
	gfarm_error_t e;
	struct gfm_replica_list_by_name_closure closure;

	e = gfm_inode_op_readonly(path, GFARM_FILE_LOOKUP,
	    gfm_replica_list_by_name_request,
	    gfm_replica_list_by_name_result,
	    gfm_inode_success_op_connection_free,
	    gfm_replica_list_by_name_cleanup,
	    &closure);
	if (e == GFARM_ERR_NO_ERROR) {
		*np = closure.n;
		*hostsp = closure.hosts;
	} else {
		gflog_debug(GFARM_MSG_1001383,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}
	return (e);
}



struct gfm_replica_remove_by_file_closure {
	const char *host;
};

static gfarm_error_t
gfm_replica_remove_by_file_request(struct gfm_connection *gfm_server,
	void *closure)
{
	struct gfm_replica_remove_by_file_closure *c = closure;
	gfarm_error_t e = gfm_client_replica_remove_by_file_request(gfm_server,
	    c->host);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000153,
		    "replica_remove_by_file request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_replica_remove_by_file_result(struct gfm_connection *gfm_server,
	void *closure)
{
	gfarm_error_t e = gfm_client_replica_remove_by_file_result(gfm_server);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000154,
		    "replica_remove_by_file result: %s",
		    gfarm_error_string(e));
#endif
	return (e);
}

gfarm_error_t
gfs_replica_remove_by_file(const char *path, const char *host)
{
	struct gfm_replica_remove_by_file_closure closure;

	closure.host = host;
	return (gfm_inode_op_modifiable(path,
	    GFARM_FILE_LOOKUP|GFARM_FILE_REPLICA_SPEC,
	    gfm_replica_remove_by_file_request,
	    gfm_replica_remove_by_file_result,
	    gfm_inode_success_op_connection_free,
	    NULL, NULL,
	    &closure));
}

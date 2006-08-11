#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/user_info.h>

#include "gfm_client.h"

#include "metadb_server.h"

static struct gfm_connection *gfm_server;

/* this interface is only used by gfsd */
void
gfarm_metadb_set_server(struct gfm_connection *server)
{
	gfm_server = server;
}

gfarm_error_t
gfarm_host_info_get_by_name_alias(const char *hostname,
	struct gfarm_host_info *info)
{
	gfarm_error_t e, e2;

	e = gfm_client_host_info_get_by_namealiases(gfm_server,
	    1, &hostname, &e2, info);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

gfarm_error_t
gfarm_metadb_verify_username(const char *global_user)
{
	gfarm_error_t e, e2;
	struct gfarm_user_info ui;

	e = gfm_client_user_info_get_by_names(gfm_server,
	    1, &global_user, &e2, &ui);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (e2 != GFARM_ERR_NO_ERROR)
		return (e2);
	gfarm_user_info_free(&ui);
	return (GFARM_ERR_NO_ERROR);
}



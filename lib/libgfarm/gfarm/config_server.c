/*
 * $Id$
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "liberror.h"
#include "auth.h"
#include "gfpath.h"
#define GFARM_USE_STDIO
#include "config.h"
#include "metadb_server.h"

static void
gfarm_config_set_default_spool_on_server(void)
{
	if (gfarm_spool_root == NULL) {
		/* XXX - this case is not recommended. */
		gfarm_spool_root = GFARM_SPOOL_ROOT;
	}
}

static void
gfarm_config_set_default_metadb_server(void)
{
	gfarm_error_t e;
	int n;
	struct gfarm_metadb_server **msl, *m;
	struct gfarm_metadb_server *ms[1];

	/* gfarm_metadb_server_name is checked in
	 * gfarm_config_set_default_ports */
	assert(gfarm_metadb_server_name != NULL);

	if ((msl = gfarm_get_metadb_server_list(&n)) != NULL)
		return;
	if ((e = gfarm_metadb_server_new(&m))
	    != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
	ms[0] = m;
	gfarm_metadb_server_set_name(m, gfarm_metadb_server_name);
	gfarm_metadb_server_set_port(m, gfarm_metadb_server_port);
	gfarm_metadb_server_set_is_master(m, 1);
	if ((e = gfarm_set_metadb_server_list(ms, 1))
	    != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_config_read(void)
{
	gfarm_error_t e;
	int lineno;
	FILE *config;

	gfarm_init_config();
	if ((config = fopen(gfarm_config_file, "r")) == NULL) {
		gflog_debug(GFARM_MSG_1000976,
			"open operation on server config file (%s) failed",
			gfarm_config_file);
		return (GFARM_ERRMSG_CANNOT_OPEN_CONFIG);
	}
	e = gfarm_config_read_file(config, &lineno);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000014, "%s: %d: %s",
		    gfarm_config_file, lineno, gfarm_error_string(e));
		return (e);
	}

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();
	gfarm_config_set_default_metadb_server();

	return (GFARM_ERR_NO_ERROR);
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_initialize(void)
{
	gfarm_error_t e;

	gflog_initialize();

	e = gfarm_server_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000977,
			"gfarm_server_config_read() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	gfarm_config_set_default_spool_on_server();

	return (GFARM_ERR_NO_ERROR);
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_terminate(void)
{
	/* nothing to do (and also may never be called) */
	gflog_terminate();

	return (GFARM_ERR_NO_ERROR);
}

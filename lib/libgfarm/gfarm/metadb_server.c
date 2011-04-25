/*
 * $Id$
 */

#include <string.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "metadb_server.h"

struct gfarm_metadb_server {
	char *name;
	int port;
	int flags;
};

#define GFARM_METADB_SERVER_FLAG_IS_SELF	0x00000001
#define GFARM_METADB_SERVER_FLAG_IS_MASTER	0x00000002
#define GFARM_METADB_SERVER_FLAG_IS_SYNC_REP	0x00000004


gfarm_error_t
gfarm_metadb_server_new(struct gfarm_metadb_server **m)
{
	if ((*m = malloc(sizeof(struct gfarm_metadb_server))) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	(*m)->flags = 0;
	return (GFARM_ERR_NO_ERROR);
}

const char *
gfarm_metadb_server_get_name(struct gfarm_metadb_server *m)
{
	return (m->name);
}

void
gfarm_metadb_server_set_name(struct gfarm_metadb_server *m, char *name)
{
	m->name = name;
}

int
gfarm_metadb_server_get_port(struct gfarm_metadb_server *m)
{
	return (m->port);
}

void
gfarm_metadb_server_set_port(struct gfarm_metadb_server *m, int port)
{
	m->port = port;
}

static void
set_flag(struct gfarm_metadb_server *m, int flag, int enable)
{
	if (enable)
		m->flags |= flag;
	else
		m->flags &= ~flag;
}

int
gfarm_metadb_server_is_master(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_METADB_SERVER_FLAG_IS_MASTER) != 0);
}

void
gfarm_metadb_server_set_is_master(struct gfarm_metadb_server *m, int enable)
{
	set_flag(m, GFARM_METADB_SERVER_FLAG_IS_MASTER, enable);
}

int
gfarm_metadb_server_is_self(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_METADB_SERVER_FLAG_IS_SELF) != 0);
}

void
gfarm_metadb_server_set_is_self(struct gfarm_metadb_server *m, int enable)
{
	set_flag(m, GFARM_METADB_SERVER_FLAG_IS_SELF, enable);
}

int
gfarm_metadb_server_is_sync_replication(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_METADB_SERVER_FLAG_IS_SYNC_REP) != 0);
}

void
gfarm_metadb_server_set_is_sync_replication(struct gfarm_metadb_server *m,
	int enable)
{
	set_flag(m, GFARM_METADB_SERVER_FLAG_IS_SYNC_REP, enable);
}

void
gfarm_metadb_server_free(struct gfarm_metadb_server *m)
{
	free(m->name);
}

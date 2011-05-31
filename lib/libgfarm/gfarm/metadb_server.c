/*
 * $Id$
 */

#include <string.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfm_proto.h"

#include "metadb_server.h"

gfarm_error_t
gfarm_metadb_server_new(struct gfarm_metadb_server **m,
	const char *name, int port)
{
	if (GFARM_MALLOC(*m) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	(*m)->name = name;
	(*m)->clustername = NULL;
	(*m)->port = port;
	(*m)->flags = 0;
	(*m)->tflags = 0;

	return (GFARM_ERR_NO_ERROR);
}

const char *
gfarm_metadb_server_get_name(struct gfarm_metadb_server *m)
{
	return (m->name);
}

const char *
gfarm_metadb_server_get_clustername(struct gfarm_metadb_server *m)
{
	return (m->clustername);
}

void
gfarm_metadb_server_set_clustername(struct gfarm_metadb_server *m,
	char *clustername)
{
	m->clustername = clustername;
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

static void
set_tflag(struct gfarm_metadb_server *m, int tflag, int enable)
{
	if (enable)
		m->tflags |= tflag;
	else
		m->tflags &= ~tflag;
}

int
gfarm_metadb_server_get_flags(struct gfarm_metadb_server *m)
{
	return (m->flags);
}

void
gfarm_metadb_server_set_flags(struct gfarm_metadb_server *m, int flags)
{
	m->flags = flags;
}

int
gfarm_metadb_server_is_default_master(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_METADB_SERVER_FLAG_IS_DEFAULT_MASTER) != 0);
}

void
gfarm_metadb_server_set_is_default_master(struct gfarm_metadb_server *m,
	int enable)
{
	set_flag(m, GFARM_METADB_SERVER_FLAG_IS_DEFAULT_MASTER, enable);
}

int
gfarm_metadb_server_is_master_candidate(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_METADB_SERVER_FLAG_IS_MASTER_CANDIDATE) != 0);
}

void
gfarm_metadb_server_set_is_master_candidate(struct gfarm_metadb_server *m,
	int enable)
{
	set_flag(m, GFARM_METADB_SERVER_FLAG_IS_MASTER_CANDIDATE, enable);
}

int
gfarm_metadb_server_is_master(struct gfarm_metadb_server *m)
{
	return ((m->tflags & GFARM_METADB_SERVER_FLAG_IS_MASTER) != 0);
}

void
gfarm_metadb_server_set_is_master(struct gfarm_metadb_server *m, int enable)
{
	set_tflag(m, GFARM_METADB_SERVER_FLAG_IS_MASTER, enable);
}

int
gfarm_metadb_server_is_self(struct gfarm_metadb_server *m)
{
	return ((m->tflags & GFARM_METADB_SERVER_FLAG_IS_SELF) != 0);
}

void
gfarm_metadb_server_set_is_self(struct gfarm_metadb_server *m, int enable)
{
	set_tflag(m, GFARM_METADB_SERVER_FLAG_IS_SELF, enable);
}

int
gfarm_metadb_server_is_sync_replication(struct gfarm_metadb_server *m)
{
	return ((m->tflags & GFARM_METADB_SERVER_FLAG_IS_SYNCREP) != 0);
}

void
gfarm_metadb_server_set_is_sync_replication(struct gfarm_metadb_server *m,
	int enable)
{
	set_tflag(m, GFARM_METADB_SERVER_FLAG_IS_SYNCREP, enable);
}

int
gfarm_metadb_server_is_active(struct gfarm_metadb_server *m)
{
	return ((m->tflags & GFARM_METADB_SERVER_FLAG_IS_ACTIVE) != 0);
}

void
gfarm_metadb_server_set_is_active(struct gfarm_metadb_server *m, int enable)
{
	set_tflag(m, GFARM_METADB_SERVER_FLAG_IS_ACTIVE, enable);
}

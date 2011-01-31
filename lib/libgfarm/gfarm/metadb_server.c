/*
 * $Id$
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <gfarm/gfarm.h>

#include "metadb_server.h"

struct gfarm_metadb_server {
	char *name;
	int port;
	int flags;
};

#define GFARM_MDHOST_FLAGS_IS_SELF	0x00000001
#define GFARM_MDHOST_FLAGS_IS_MASTER	0x00000002


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
	return ((m->flags & GFARM_MDHOST_FLAGS_IS_MASTER) != 0);
}

void
gfarm_metadb_server_set_is_master(struct gfarm_metadb_server *m, int enable)
{
	set_flag(m, GFARM_MDHOST_FLAGS_IS_MASTER, enable);
}

int
gfarm_metadb_server_is_self(struct gfarm_metadb_server *m)
{
	return ((m->flags & GFARM_MDHOST_FLAGS_IS_SELF) != 0);
}

void
gfarm_metadb_server_set_is_self(struct gfarm_metadb_server *m, int enable)
{
	set_flag(m, GFARM_MDHOST_FLAGS_IS_SELF, enable);
}

void
gfarm_metadb_server_free(struct gfarm_metadb_server *m)
{
	free(m->name);
}

static struct gfarm_metadb_server **metadb_server_list = NULL;
static int num_metadb_server_list = 0;

struct gfarm_metadb_server**
gfarm_get_metadb_server_list(int *n)
{
	*n = num_metadb_server_list;
	return (metadb_server_list);
}

gfarm_error_t
gfarm_set_metadb_server_list(struct gfarm_metadb_server **metadb_servers,
	int n)
{
	metadb_server_list = malloc(sizeof(void *) * n);
	if (metadb_server_list == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(metadb_server_list, metadb_servers, sizeof(void *) * n);
	num_metadb_server_list = n;
	return (GFARM_ERR_NO_ERROR);
}


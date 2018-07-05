/*
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"
#include "queue.h"

#include "auth.h"
#include "gfp_xdr.h"
#include "config.h"

#include "gfm_client.h"

#include "subr.h"
#include "host.h"
#include "user.h"
#include "peer.h"
#include "abstract_host.h"
#include "mdhost.h"
#include "db_access.h"


struct mdhost_elem {
	struct mdhost *mh;
	GFARM_STAILQ_ENTRY(mdhost_elem) next;
};

GFARM_STAILQ_HEAD(mdhost_list, mdhost_elem);

/* in-core gfarm_metadb_cluster */
struct mdcluster {
	char *name;
	struct mdhost_list mh_list;
	int valid;
};

static struct gfarm_hash_table *mdcluster_hashtab;

#define MDCLUSTER_HASHTAB_SIZE	31

#define FOREACH_MDCLUSTER(it) \
	for (gfarm_hash_iterator_begin(mdcluster_hashtab, &(it)); \
	     !gfarm_hash_iterator_is_end(&(it)); \
	     gfarm_hash_iterator_next(&(it)))

const char *
mdcluster_get_name(struct mdcluster *c)
{
	return (c->name);
}

static struct mdcluster *
mdcluster_lookup_internal(const char *name)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(mdcluster_hashtab, &name,
		sizeof(name));
	if (entry == NULL)
		return (NULL);
	return (*(struct mdcluster **)gfarm_hash_entry_data(entry));
}

struct mdcluster *
mdcluster_lookup(const char *name)
{
	struct mdcluster *c = mdcluster_lookup_internal(name);

	if (c == NULL || c->valid == 0)
		return (NULL);
	return (c);
}

static struct mdcluster *
mdcluster_iterator_access(struct gfarm_hash_iterator *it)
{
	return (*(struct mdcluster **)gfarm_hash_entry_data(
	gfarm_hash_iterator_access(it)));
}

void
mdcluster_foreach(int (*op)(struct mdcluster *, void *), void *closure)
{
	struct gfarm_hash_iterator it;
	struct mdcluster *c;

	FOREACH_MDCLUSTER(it) {
		c = mdcluster_iterator_access(&it);
		if (c->valid && op(c, closure) == 0)
			break;
	}
}

void
mdcluster_foreach_mdhost(struct mdcluster *c,
	int (*op)(struct mdhost *, void *), void *closure)
{
	struct mdhost_elem *he;

	GFARM_STAILQ_FOREACH(he, &c->mh_list, next)
		if (op(he->mh, closure) == 0)
			break;
}

static struct mdcluster *
mdcluster_new(char *name)
{
	struct mdcluster *c;

	if ((c = malloc(sizeof(struct mdcluster))) == NULL)
		return (NULL);
	c->name = name;
	c->valid = 1;
	GFARM_STAILQ_INIT(&c->mh_list);
	return (c);
}

static gfarm_error_t
mdcluster_enter(const char *name, struct mdcluster **cpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct mdcluster *c;
	gfarm_error_t e;
	char *name2;

	c = mdcluster_lookup_internal(name);
	if (c) {
		if (c->valid)
			return (GFARM_ERR_ALREADY_EXISTS);
		c->valid = 1;
		*cpp = c;
		return (GFARM_ERR_NO_ERROR);
	}

	name2 = strdup(name);
	if (name2 != NULL)
		c = mdcluster_new(name2);
	if (name2 == NULL || c == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003012,
		    "%s", gfarm_error_string(e));
		free(name2);
		return (e);
	}

	entry = gfarm_hash_enter(mdcluster_hashtab,
	    &c->name, sizeof(c->name),
	    sizeof(struct mdcluster *), &created);
	if (entry == NULL) {
		free(c);
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003013,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		free(c);
		gflog_debug(GFARM_MSG_1003014,
		    "Entry %s already exists", c->name);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	*(struct mdcluster **)gfarm_hash_entry_data(entry) = c;

	if (cpp)
		*cpp = c;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
mdcluster_get_or_create_by_mdhost(struct mdhost *h)
{
	gfarm_error_t e;
	struct mdhost_elem *he;
	struct mdcluster *c;
	const char *cname = mdhost_get_cluster_name(h);

	c = mdcluster_lookup(cname);
	if (c == NULL && (e = mdcluster_enter(cname, &c))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003015,
		    "failed to create mdcluster for mdhost %s : %s",
		    mdhost_get_name(h), gfarm_error_string(e));
		return (e);
	}
	mdhost_set_cluster(h, c);

	GFARM_MALLOC(he);
	if (he == NULL) {
		gflog_error(GFARM_MSG_1003016,
		    "failed to create mdcluster for mdhost %s : %s",
		    mdhost_get_name(h),
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	he->mh = h;
	GFARM_STAILQ_INSERT_TAIL(&c->mh_list, he, next);

	return (GFARM_ERR_NO_ERROR);
}

void
mdcluster_remove_mdhost(struct mdhost *h)
{
	struct mdhost_elem *he, *tmp;
	struct mdcluster *c = mdhost_get_cluster(h);

	GFARM_STAILQ_FOREACH_SAFE(he, &c->mh_list, next, tmp) {
		if (he->mh == h) {
			GFARM_STAILQ_REMOVE(&c->mh_list, he,
				mdhost_elem, next);
			free(he);
			break;
		}
	}

	if (GFARM_STAILQ_EMPTY(&c->mh_list))
		c->valid = 0;
}

void
mdcluster_init(void)
{
	mdcluster_hashtab =
	    gfarm_hash_table_alloc(MDCLUSTER_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (mdcluster_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1003017,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
}

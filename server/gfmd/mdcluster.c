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
	pthread_mutex_t mutex;
};
static const char MDCLUSTER_MUTEX_DIAG[] = "mdcluster_mutex";

static struct gfarm_hash_table *mdcluster_hashtab;
pthread_mutex_t mdcluster_hash_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char MDCLUSTER_HASH_MUTEX_DIAG[] = "mdcluster_hash_mutex";

#define MDCLUSTER_HASHTAB_SIZE	31

#define FOREACH_MDCLUSTER(it) \
	for (gfarm_hash_iterator_begin(mdcluster_hashtab, &(it)); \
	     !gfarm_hash_iterator_is_end(&(it)); \
	     gfarm_hash_iterator_next(&(it)))

static void
mdcluster_mutex_lock(struct mdcluster *c, const char *diag)
{
	gfarm_mutex_lock(&c->mutex, diag, MDCLUSTER_MUTEX_DIAG);
}

static void
mdcluster_mutex_unlock(struct mdcluster *c, const char *diag)
{
	gfarm_mutex_unlock(&c->mutex, diag, MDCLUSTER_MUTEX_DIAG);
}

static void
mdcluster_hash_mutex_lock(const char *diag)
{
	gfarm_mutex_lock(&mdcluster_hash_mutex, diag,
		MDCLUSTER_HASH_MUTEX_DIAG);
}

static void
mdcluster_hash_mutex_unlock(const char *diag)
{
	gfarm_mutex_unlock(&mdcluster_hash_mutex, diag,
		   MDCLUSTER_HASH_MUTEX_DIAG);
}

const char *
mdcluster_get_name(struct mdcluster *c)
{
	char *name;
	static const char diag[] = "mdcluster_get_name";

	mdcluster_mutex_lock(c, diag);
	name = c->name;
	mdcluster_mutex_unlock(c, diag);
	return (name);
}

/* need to call mdcluster_mutex_unlock */
static struct mdcluster *
mdcluster_lookup_internal(const char *name, const char *diag)
{
	struct gfarm_hash_entry *entry;
	struct mdcluster *mdc;

	mdcluster_hash_mutex_lock(diag);
	entry = gfarm_hash_lookup(mdcluster_hashtab, &name,
		sizeof(name));
	mdcluster_hash_mutex_unlock(diag);
	if (entry == NULL)
		return (NULL);
	else {
		mdc = *(struct mdcluster **)gfarm_hash_entry_data(entry);
		mdcluster_mutex_lock(mdc, diag);
		return (mdc);
	}
}

/* need to call mdcluster_mutex_unlock */
static struct mdcluster *
mdcluster_lookup(const char *name, const char *diag)
{
	struct mdcluster *c = mdcluster_lookup_internal(name, diag);

	if (c == NULL)
		return (NULL);
	if (c->valid == 0) {
		mdcluster_mutex_unlock(c, diag);
		return (NULL);
	}
	return (c);
}

static struct mdcluster *
mdcluster_new(char *name)
{
	struct mdcluster *c;
	static const char diag[] = "mdcluster_new";

	if (GFARM_MALLOC(c) == NULL)
		return (NULL);
	c->name = name;
	c->valid = 1;
	GFARM_STAILQ_INIT(&c->mh_list);
	gfarm_mutex_init(&c->mutex, diag, MDCLUSTER_MUTEX_DIAG);
	return (c);
}

/* need to call mdcluster_mutex_unlock */
static gfarm_error_t
mdcluster_enter(const char *name, struct mdcluster **cpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct mdcluster *c;
	gfarm_error_t e;
	char *name2;
	static const char diag[] = "mdcluster_enter";

	c = mdcluster_lookup_internal(name, diag);
	if (c) {
		if (c->valid) {
			mdcluster_mutex_unlock(c, diag);
			return (GFARM_ERR_ALREADY_EXISTS);
		}
		c->valid = 1;
		if (cpp)
			*cpp = c;
		else
			mdcluster_mutex_unlock(c, diag);
		return (GFARM_ERR_NO_ERROR);
	}

	name2 = strdup_log(name, diag);
	if (name2 != NULL)
		c = mdcluster_new(name2);
	if (name2 == NULL || c == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003012,
		    "%s", gfarm_error_string(e));
		free(name2);
		return (e);
	}

	mdcluster_hash_mutex_lock(diag);
	entry = gfarm_hash_enter(mdcluster_hashtab,
	    &c->name, sizeof(c->name),
	    sizeof(struct mdcluster *), &created);
	if (entry == NULL) {
		mdcluster_hash_mutex_unlock(diag);
		free(c);
		free(name2);
		e = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1003013,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		mdcluster_hash_mutex_unlock(diag);
		free(c);
		free(name2);
		gflog_debug(GFARM_MSG_1003014,
		    "Entry %s already exists", name);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	*(struct mdcluster **)gfarm_hash_entry_data(entry) = c;
	mdcluster_hash_mutex_unlock(diag);

	if (cpp) {
		mdcluster_mutex_lock(c, diag);
		*cpp = c;
	}
	return (GFARM_ERR_NO_ERROR);
}

/* PREREQUISITE: mdhost_mutex_lock */
gfarm_error_t
mdcluster_get_or_create_by_mdhost(struct mdhost *h)
{
	gfarm_error_t e;
	struct mdhost_elem *he;
	struct mdcluster *c;
	const char *cname = mdhost_get_cluster_name_unlocked(h);
	static const char diag[] = "mdcluster_get_or_create_by_mdhost";

	c = mdcluster_lookup(cname, diag);
	if (c == NULL && (e = mdcluster_enter(cname, &c))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003015,
		    "failed to create mdcluster for mdhost %s : %s",
		    mdhost_get_name_unlocked(h), gfarm_error_string(e));
		return (e);
	}
	mdhost_set_cluster_unlocked(h, c);

	GFARM_MALLOC(he);
	if (he == NULL) {
		mdcluster_mutex_unlock(c, diag);
		gflog_error(GFARM_MSG_1003016,
		    "failed to create mdcluster for mdhost %s : %s",
		    mdhost_get_name_unlocked(h),
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	he->mh = h;
	GFARM_STAILQ_INSERT_TAIL(&c->mh_list, he, next);
	mdcluster_mutex_unlock(c, diag);

	return (GFARM_ERR_NO_ERROR);
}

/* PREREQUISITE: mdhost_mutex_lock */
void
mdcluster_remove_mdhost(struct mdhost *h)
{
	struct mdhost_elem *he, *tmp;
	struct mdcluster *c = mdhost_get_cluster_unlocked(h);
	static const char diag[] = "mdcluster_remove_mdhost";

	mdcluster_mutex_lock(c, diag);
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
	mdcluster_mutex_unlock(c, diag);
}

void
mdcluster_init(void)
{
	static const char diag[] = "mdcluster_init";

	mdcluster_hashtab =
	    gfarm_hash_table_alloc(MDCLUSTER_HASHTAB_SIZE,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (mdcluster_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1003017,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
	gfarm_mutex_init(&mdcluster_hash_mutex, diag,
		 MDCLUSTER_HASH_MUTEX_DIAG);
}

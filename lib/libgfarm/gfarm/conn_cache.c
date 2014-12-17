#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "lru_cache.h"
#include "thrsubr.h"

#include "conn_hash.h"
#include "conn_cache.h"

#ifndef __KERNEL__	/* GFSP_CONN_MUTEX :: conn_lock */
#define	GFSP_CONN_MUTEX
#define	GFSP_CONN_INIT(conn)
#define	GFSP_CONN_LOCK(conn)
#define	GFSP_CONN_UNLOCK(conn)
#else /* __KERNEL__ */
/*
 * In kernel mode, processes of the same user use the same connection.
 * Lock the connection from sending-request to receiving-reply,
 * a compound request as well.
 */
#define	GFSP_CONN_MUTEX		struct gfarm_rmutex conn_lock;
#define	GFSP_CONN_INIT(conn)	gfarm_rmutex_init(&(conn)->conn_lock, "conn");
#define	GFSP_CONN_LOCK(conn)	gfarm_rmutex_lock(&(conn)->conn_lock);
#define	GFSP_CONN_UNLOCK(conn)	gfarm_rmutex_unlock(&(conn)->conn_lock);
#endif /* __KERNEL__ */

#define CONNECTION_CACHE_LIMIT	0x7fff /* XXX must be configurable */

struct gfp_cached_connection {
	/*
	 * must be the first member of struct gfp_cached_connection.
	 * Because gfp_cached_connection_gc_entry() does DOWNCAST
	 * from "lru_entry" to "struct gfp_cached_connection".
	 */
	struct gfarm_lru_entry lru_entry;

	struct gfarm_hash_entry *hash_entry;

	struct gfp_conn_hash_id id;

	void *connection_data;

	void (*dispose_connection_data)(void *);
	GFSP_CONN_MUTEX
};

void
gfp_conn_cache_init(struct gfp_conn_cache *c,
	gfarm_error_t (*dispose)(void *), const char *type_name,
	int table_size, int *num_cachep)
{
	assert(c != NULL);

	gfarm_lru_cache_init(&c->lru_list);
	c->hashtab = NULL;
	c->dispose_connection = dispose;
	c->type_name = type_name;
	c->table_size = table_size;
	c->num_cachep = num_cachep;
	gfarm_mutex_init(&c->mutex, "gfp_conn_cache_init", "conn_cache");
}

void
gfp_conn_cache_term(struct gfp_conn_cache *c)
{
	gfp_cached_connection_terminate(c);
	gfarm_mutex_destroy(&c->mutex, "gfp_conn_cache_term", "conn_cache");
}

/*
 * return TRUE,  if created by gfs_client_connection_acquire() && still cached.
 * return FALSE, if created by gfs_client_connect() || purged from cache.
 */
#define GFP_IS_CACHED_CONNECTION(connection) ((connection)->hash_entry != NULL)

static const char diag_what[] =	"connection cache";

int
gfp_is_cached_connection(struct gfp_cached_connection *connection)
{
	return (GFP_IS_CACHED_CONNECTION(connection));
}

void *
gfp_cached_connection_get_data(struct gfp_cached_connection *connection)
{
	return (connection->connection_data);
}

void
gfp_cached_connection_set_data(struct gfp_cached_connection *connection,
	void *p)
{
	connection->connection_data = p;
}

void
gfp_cached_connection_set_dispose_data(struct gfp_cached_connection *connection,
	void (*dispose_connection_data)(void *))
{
	connection->dispose_connection_data = dispose_connection_data;
}

const char *
gfp_cached_connection_hostname(struct gfp_cached_connection *connection)
{
	return (connection->id.hostname);
}

const char *
gfp_cached_connection_username(struct gfp_cached_connection *connection)
{
	return (connection->id.username);
}

int
gfp_cached_connection_port(struct gfp_cached_connection *connection)
{
	return (connection->id.port);
}

gfarm_error_t
gfp_cached_connection_set_username(struct gfp_cached_connection *connection,
	const char *user)
{
	struct gfp_conn_hash_id *idp = &connection->id;
	char *olduser, *newuser;

	olduser = idp->username;
	if (strcmp(olduser, user) == 0)
		return (GFARM_ERR_NO_ERROR);
	newuser = strdup(user);
	if (newuser == NULL)  {
		gflog_debug(GFARM_MSG_1002564,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	idp->username = newuser;
	free(olduser);
	return (GFARM_ERR_NO_ERROR);
}
void
gfp_connection_lock(struct gfp_cached_connection *connection)
{
	GFSP_CONN_LOCK(connection);
}
void
gfp_connection_unlock(struct gfp_cached_connection *connection)
{
#ifdef __KERNEL__
	int err;
	err = GFSP_CONN_UNLOCK(connection);
	if (err != 0) {
		gflog_fatal(GFARM_MSG_1003871, "owner=%d count=%d",
			connection->conn_lock.r_owner,
			connection->conn_lock.r_locked);
	}
#endif /* __KERNEL__ */
}

gfarm_error_t
gfp_uncached_connection_new(const char *hostname, int port,
	const char *username, struct gfp_cached_connection **connectionp)
{
	struct gfp_cached_connection *connection;
	struct gfp_conn_hash_id *idp;

	GFARM_MALLOC(connection);
	if (connection == NULL) {
		gflog_debug(GFARM_MSG_1001087,
			"allocation of 'connection' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	connection->hash_entry = NULL; /* this is an uncached connection */
	idp = &connection->id;
	idp->hostname = strdup(hostname);
	idp->username = strdup(username);
	if (idp->hostname == NULL || idp->username == NULL) {
		gflog_debug(GFARM_MSG_1002565,
		    "gfp_cached_connection_acquire (%s)(%d)"
		    " failed: %s",
		    hostname, port,
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		free(idp->hostname);
		free(idp->username);
		free(connection);
		return (GFARM_ERR_NO_MEMORY);
	}
	idp->port = port;

	gfarm_lru_init_uncached_entry(&connection->lru_entry);

	connection->connection_data = NULL;
	connection->dispose_connection_data = NULL;
	GFSP_CONN_INIT(connection)
	*connectionp = connection;
	return (GFARM_ERR_NO_ERROR);
}

/* this must be called from (*cache->dispose_connection)() */
void
gfp_uncached_connection_dispose(struct gfp_cached_connection *connection)
{
	struct gfp_conn_hash_id *idp = &connection->id;

	free(idp->hostname);
	free(idp->username);
	if (connection->dispose_connection_data && connection->connection_data)
		connection->dispose_connection_data(
		    connection->connection_data);
	free(connection);
}

/* convert from cached connection to uncached */
void
gfp_cached_connection_purge_from_cache(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	static const char diag[] = "gfp_cached_connection_purge_from_cache";

	if (!GFP_IS_CACHED_CONNECTION(connection))
		return;

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	gfarm_lru_cache_purge_entry(&connection->lru_entry);

	gfp_conn_hash_purge(cache->hashtab, connection->hash_entry);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

	connection->hash_entry = NULL; /* this is an uncached connection now */
}

/* convert from uncached connection to cached */
static inline gfarm_error_t
gfp_uncached_connection_into_cache(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection,
	void  (*func)(struct gfarm_lru_cache *, struct gfarm_lru_entry *))

{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	int created;
	static const char diag[] = "gfp_uncached_connection_enter_cache";

	if (GFP_IS_CACHED_CONNECTION(connection)) {
		gflog_fatal(GFARM_MSG_1000057,
		    "gfp_uncached_connection_enter_cache(%s): "
		    "programming error", cache->type_name);
	}

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	e = gfp_conn_hash_id_enter_noalloc(&cache->hashtab, cache->table_size,
	    sizeof(connection), &connection->id, &entry, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

		gflog_debug(GFARM_MSG_1001088,
			"insertion to connection hash (%s)(%d) failed: %s",
			gfp_cached_connection_hostname(connection),
			gfp_cached_connection_port(connection),
			gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

		gflog_debug(GFARM_MSG_1001089,
			"insertion to connection hash (%s)(%d) failed: %s",
			gfp_cached_connection_hostname(connection),
			gfp_cached_connection_port(connection),
			gfarm_error_string(GFARM_ERR_ALREADY_EXISTS));
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	func(&cache->lru_list, &connection->lru_entry);

	*(struct gfp_cached_connection **)gfarm_hash_entry_data(entry)
	    = connection;
	connection->hash_entry = entry;

	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
	return (GFARM_ERR_NO_ERROR);
}
gfarm_error_t
gfp_uncached_connection_enter_cache(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	return (gfp_uncached_connection_into_cache(cache, connection,
				gfarm_lru_cache_link_entry));
}
gfarm_error_t
gfp_uncached_connection_enter_cache_tail(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	return (gfp_uncached_connection_into_cache(cache, connection,
				gfarm_lru_cache_link_entry_tail));
}

/* update the LRU list to mark this gfp_cached_connection recently used */
void
gfp_cached_connection_used(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	static const char diag[] = "gfp_cached_connection_used";

	if (!GFP_IS_CACHED_CONNECTION(connection))
		return;

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	gfarm_lru_cache_access_entry(&cache->lru_list, &connection->lru_entry);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
}

static void
gfp_cached_connection_gc_entry(struct gfarm_lru_entry *entry, void *closure)
{
	struct gfp_conn_cache *cache = closure;
	struct gfp_cached_connection *connection =
	    (struct gfp_cached_connection *)entry; /* DOWNCAST */
	static const char diag[] = "gfp_cached_connection_gc_entry";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	gfp_conn_hash_purge(cache->hashtab, connection->hash_entry);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

	/*
	 * `cache' itself will be disposed by gfp_uncached_connection_dispose()
	 * which must be called from (*cache->dispose_connection)()
	 */
	(*cache->dispose_connection)(connection->connection_data);
}

static void
gfp_cached_connection_gc_internal(struct gfp_conn_cache *cache,
	int free_target)
{
	static const char diag[] = "gfp_cached_connection_gc_internal";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	gfarm_lru_cache_gc(&cache->lru_list, free_target,
	    gfp_cached_connection_gc_entry, cache, cache->type_name,
	    &cache->mutex, diag_what);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
}

void
gfp_cached_connection_gc_all(struct gfp_conn_cache *cache)
{
	gfp_cached_connection_gc_internal(cache, 0);
}
int
gfp_connection_cache_change(struct gfp_conn_cache *cache, int cnt)
{
	static const char diag[] = "gfp_connection_cache_change";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	if (cnt > 0 && *cache->num_cachep > CONNECTION_CACHE_LIMIT)
		cnt = 0;
	else
		*cache->num_cachep += cnt;
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

	return (cnt);
}

gfarm_error_t
gfp_cached_connection_acquire(struct gfp_conn_cache *cache,
	const char *canonical_hostname, int port, const char *user,
	struct gfp_cached_connection **connectionp, int *createdp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct gfp_cached_connection *connection;
	struct gfp_conn_hash_id *idp, *kidp;
	static const char diag[] = "gfp_cached_connection_acquire";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	e = gfp_conn_hash_enter_noalloc(&cache->hashtab, cache->table_size,
	    sizeof(connection), canonical_hostname, port, user,
	    &entry, createdp);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
		gflog_debug(GFARM_MSG_1001090,
			"insertion to connection hash (%s)(%d) failed: %s",
			canonical_hostname, port,
			gfarm_error_string(e));
		return (e);
	}
	if (!*createdp) {
		connection = *(struct gfp_cached_connection **)
		    gfarm_hash_entry_data(entry);
		gfarm_lru_cache_addref_entry(&cache->lru_list,
		    &connection->lru_entry);
	} else {
		GFARM_MALLOC(connection);
		if (connection == NULL) {
			gfp_conn_hash_purge(cache->hashtab, entry);
			gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
			gflog_debug(GFARM_MSG_1001091,
				"allocation of 'connection' failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}

		idp = &connection->id;
		idp->hostname = strdup(canonical_hostname);
		idp->port = port;
		idp->username = strdup(user);
		if (idp->hostname == NULL || idp->username == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002566,
			    "gfp_cached_connection_acquire (%s)(%d)"
			    " failed: %s",
			    canonical_hostname, port,
			    gfarm_error_string(e));
			free(idp->hostname);
			free(idp->username);
			free(connection);
			gfp_conn_hash_purge(cache->hashtab, entry);
			gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
			return (e);
		}
		kidp = (struct gfp_conn_hash_id *)gfarm_hash_entry_key(entry);
		kidp->hostname = idp->hostname;
		kidp->username = idp->username;

		gfarm_lru_cache_add_entry(&cache->lru_list,
		    &connection->lru_entry);

		*(struct gfp_cached_connection **)gfarm_hash_entry_data(entry)
		    = connection;
		connection->hash_entry = entry;
		connection->connection_data = NULL;
		connection->dispose_connection_data = NULL;
		GFSP_CONN_INIT(connection)
	}
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
	*connectionp = connection;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_cached_or_uncached_connection_free(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	int removable;
	static const char diag[] = "gfp_cached_or_uncached_connection_free";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	removable = gfarm_lru_cache_delref_entry(&cache->lru_list,
	    &connection->lru_entry);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

	if (!removable)
		return; /* shouln't be disposed */

	if (!GFP_IS_CACHED_CONNECTION(connection)) /* already purged */
		(*cache->dispose_connection)(connection->connection_data);
	else
		gfp_cached_connection_gc_internal(cache, *cache->num_cachep);
}

void
gfp_cached_connection_addref(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	static const char diag[] = "gfp_cached_connection_addref";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	gfarm_lru_cache_addref_entry(&cache->lru_list,
	    &connection->lru_entry);
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
}

/*
 * this function frees all cached connections, including in-use ones.
 *
 * potential problems:
 * - connections which are currently in-use are freed too.
 * - connections which are uncached are NOT freed.
 *   i.e. the followings are all NOT freed:
 *	- connections which have never cached,
 *      - connections which had been once cached but currently uncached
 *	  (since their network connections were dead)
 */
void
gfp_cached_connection_terminate(struct gfp_conn_cache *cache)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct gfp_cached_connection *connection;
	static const char diag[] = "gfp_cached_connection_terminate";

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);
	if (cache->hashtab == NULL) {
		gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
		return;
	}
	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);

	/*
	 * clear all free connections.
	 * to makes cache->lru_list.free_cached_entries 0.
	 */
	gfp_cached_connection_gc_all(cache);

	gfarm_mutex_lock(&cache->mutex, diag, diag_what);

	/* clear all in-use connections too.  XXX really necessary?  */
	for (gfarm_hash_iterator_begin(cache->hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);) {
		entry = gfarm_hash_iterator_access(&it);
		connection = *(struct gfp_cached_connection **)
		    gfarm_hash_entry_data(entry);

		gfarm_lru_cache_purge_entry(&connection->lru_entry);

		gfp_conn_hash_iterator_purge(&it);

		gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
		(*cache->dispose_connection)(connection->connection_data);
		gfarm_mutex_lock(&cache->mutex, diag, diag_what);

		/* restart from the top, because maybe changed by others */
		gfarm_hash_iterator_begin(cache->hashtab, &it);
	}

	/* free hash table */
	gfarm_hash_table_free(cache->hashtab);
	cache->hashtab = NULL;

	gfarm_mutex_unlock(&cache->mutex, diag, diag_what);
}

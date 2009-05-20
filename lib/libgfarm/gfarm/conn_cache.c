#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "lru_cache.h"

#include "conn_hash.h"
#include "conn_cache.h"

struct gfp_cached_connection {
	/*
	 * must be the first member of struct gfp_cached_connection.
	 * Because gfp_cached_connection_gc_entry() does DOWNCAST
	 * from "lru_entry" to "struct gfp_cached_connection".
	 */
	struct gfarm_lru_entry lru_entry;

	struct gfarm_hash_entry *hash_entry;

	void *connection_data;
};

/*
 * return TRUE,  if created by gfs_client_connection_acquire() && still cached.
 * return FALSE, if created by gfs_client_connect() || purged from cache.
 */
#define GFP_IS_CACHED_CONNECTION(connection) ((connection)->hash_entry != NULL)

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

gfarm_error_t
gfp_uncached_connection_new(struct gfp_cached_connection **connectionp)
{
	struct gfp_cached_connection *connection;

	GFARM_MALLOC(connection);
	if (connection == NULL)
		return (GFARM_ERR_NO_MEMORY);

	connection->hash_entry = NULL; /* this is an uncached connection */

	gfarm_lru_init_uncached_entry(&connection->lru_entry);

	connection->connection_data = NULL;
	*connectionp = connection;
	return (GFARM_ERR_NO_ERROR);
}

/* this must be called from (*cache->dispose_connection)() */
void
gfp_uncached_connection_dispose(struct gfp_cached_connection *connection)
{
	free(connection);
}

/* convert from cached connection to uncached */
void
gfp_cached_connection_purge_from_cache(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	if (!GFP_IS_CACHED_CONNECTION(connection))
		return;

	gfarm_lru_cache_purge_entry(&connection->lru_entry);

	gfp_conn_hash_purge(cache->hashtab, connection->hash_entry);
	connection->hash_entry = NULL; /* this is an uncached connection now */
}

/* convert from uncached connection to cached */
gfarm_error_t
gfp_uncached_connection_enter_cache(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection,
	const char *hostname, int port)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	int created;

	if (GFP_IS_CACHED_CONNECTION(connection)) {
		gflog_error("gfp_uncached_connection_enter_cache(%s): "
		    "programming error", cache->type_name);
		abort();
	}
	e = gfp_conn_hash_enter(&cache->hashtab, cache->table_size,
	    sizeof(connection),
	    hostname, port, gfarm_get_global_username(),
	    &entry, &created);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!created)
		return (GFARM_ERR_ALREADY_EXISTS);

	gfarm_lru_cache_link_entry(&cache->lru_list, &connection->lru_entry);

	*(struct gfp_cached_connection **)gfarm_hash_entry_data(entry)
	    = connection;
	connection->hash_entry = entry;

	return (GFARM_ERR_NO_ERROR);
}

/* update the LRU list to mark this gfp_cached_connection recently used */
void
gfp_cached_connection_used(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	if (!GFP_IS_CACHED_CONNECTION(connection))
		return;

	gfarm_lru_cache_access_entry(&cache->lru_list, &connection->lru_entry);
}

static void
gfp_cached_connection_gc_entry(struct gfarm_lru_entry *entry, void *closure)
{
	struct gfp_conn_cache *cache = closure;
	struct gfp_cached_connection *connection =
	    (struct gfp_cached_connection *)entry; /* DOWNCAST */

	gfp_conn_hash_purge(cache->hashtab, connection->hash_entry);

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
	gfarm_lru_cache_gc(&cache->lru_list, free_target,
	    gfp_cached_connection_gc_entry, cache, cache->type_name);
}

void
gfp_cached_connection_gc_all(struct gfp_conn_cache *cache)
{
	gfp_cached_connection_gc_internal(cache, 0);
}

gfarm_error_t
gfp_cached_connection_acquire(struct gfp_conn_cache *cache,
	const char *canonical_hostname, int port,
	struct gfp_cached_connection **connectionp, int *createdp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct gfp_cached_connection *connection;

	e = gfp_conn_hash_enter(&cache->hashtab, cache->table_size,
	    sizeof(connection), canonical_hostname, port,
	    gfarm_get_global_username(),
	    &entry, createdp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (!*createdp) {
		connection = *(struct gfp_cached_connection **)
		    gfarm_hash_entry_data(entry);
		gfarm_lru_cache_addref_entry(&cache->lru_list,
		    &connection->lru_entry);
	} else {
		GFARM_MALLOC(connection);
		if (connection == NULL) {
			gfp_conn_hash_purge(cache->hashtab, entry);
			return (GFARM_ERR_NO_MEMORY);
		}

		gfarm_lru_cache_add_entry(&cache->lru_list,
		    &connection->lru_entry);

		*(struct gfp_cached_connection **)gfarm_hash_entry_data(entry)
		    = connection;
		connection->hash_entry = entry;

		connection->connection_data = NULL;
	}
	*connectionp = connection;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_cached_or_uncached_connection_free(struct gfp_conn_cache *cache,
	struct gfp_cached_connection *connection)
{
	if (!gfarm_lru_cache_delref_entry(&cache->lru_list,
	    &connection->lru_entry))
		return; /* shouln't be disposed */

	if (!GFP_IS_CACHED_CONNECTION(connection)) /* already purged */
		(*cache->dispose_connection)(connection->connection_data);
	else
		gfp_cached_connection_gc_internal(cache, *cache->num_cachep);
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

	if (cache->hashtab == NULL)
		return;

	/*
	 * clear all free connections.
	 * to makes cache->lru_list.free_cached_entries 0.
	 */
	gfp_cached_connection_gc_all(cache);

	/* clear all in-use connections too.  XXX really necessary?  */
	for (gfarm_hash_iterator_begin(cache->hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it); ) {
		entry = gfarm_hash_iterator_access(&it);
		connection = *(struct gfp_cached_connection **)
		    gfarm_hash_entry_data(entry);

		gfarm_lru_cache_purge_entry(&connection->lru_entry);

		gfp_conn_hash_iterator_purge(&it);
		(*cache->dispose_connection)(connection->connection_data);
	}

	/* free hash table */
	gfarm_hash_table_free(cache->hashtab);
	cache->hashtab = NULL;
}


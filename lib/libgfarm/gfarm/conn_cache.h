/*
 * The following #include is necessary to use this module:
 * #include "lru_cache.h"
 */

struct gfarm_hash_table;
struct gfp_cached_connection;
struct gfp_xdr;

struct gfp_conn_cache {
	struct gfarm_lru_cache lru_list;

	struct gfarm_hash_table *hashtab;

	gfarm_error_t (*dispose_connection)(void *);

	const char *type_name;
	int table_size;
	int *num_cachep;

	pthread_mutex_t mutex;
};

/* The `dispose' function below must call gfp_uncached_connection_dispose() */
#define GFP_CONN_CACHE_INITIALIZER(var, dispose, \
	   type_name, table_size, num_cachep) \
	{ \
		GFARM_LRU_CACHE_INITIALIZER(var.lru_list), \
		NULL, \
		dispose, \
		type_name, table_size, num_cachep, \
		PTHREAD_MUTEX_INITIALIZER \
	}

int gfp_is_cached_connection(struct gfp_cached_connection *);
void *gfp_cached_connection_get_data(struct gfp_cached_connection *);
void gfp_cached_connection_set_data(struct gfp_cached_connection *, void *);
void gfp_cached_connection_set_dispose_data(struct gfp_cached_connection *,
	void (*)(void *));
const char *gfp_cached_connection_hostname(struct gfp_cached_connection *);
const char *gfp_cached_connection_username(struct gfp_cached_connection *);
int gfp_cached_connection_port(struct gfp_cached_connection *);
gfarm_error_t gfp_cached_connection_set_username(
	struct gfp_cached_connection *, const char *user);


gfarm_error_t gfp_uncached_connection_new(const char *, int, const char *,
	struct gfp_cached_connection **);
void gfp_uncached_connection_dispose(struct gfp_cached_connection *);
void gfp_cached_connection_purge_from_cache(struct gfp_conn_cache *,
	struct gfp_cached_connection *);
gfarm_error_t gfp_uncached_connection_enter_cache(struct gfp_conn_cache *,
	struct gfp_cached_connection *);
void gfp_cached_connection_used(struct gfp_conn_cache *,
	struct gfp_cached_connection *);
void gfp_cached_connection_gc_all(struct gfp_conn_cache *);
int gfp_cached_connection_refcount(struct gfp_cached_connection *);
gfarm_error_t gfp_cached_connection_acquire(struct gfp_conn_cache *,
	const char *, int, const char *, struct gfp_cached_connection **,
	int *);
void gfp_cached_or_uncached_connection_free(struct gfp_conn_cache *,
	struct gfp_cached_connection *);
void gfp_cached_connection_terminate(struct gfp_conn_cache *);

/*
 * acquired > 0 && linked from gfarm_lru_cache:
 *	the entry is occupied.
 * acquired > 0 && not linked from gfarm_lru_cache:
 *	the entry is occupied, but shouldn't be used in future. (e.g. zombie)
 * acquired == 0 && linked from gfarm_lru_cache:
 *	the entry is free, but it's still cached.
 *	gfarm_lru_cache:free_cached_entries counts only this type of entries.
 * acquired == 0 && not linked from gfarm_lru_cache:
 *	the entry is free, and it's not cached.
 */
struct gfarm_lru_entry {
	struct gfarm_lru_entry *next, *prev; /* doubly linked circular list */

	int acquired; /* reference counter */
};

#define GFARM_LRU_CACHE_ENTRY_INITIALIZER(list_head_ptr) \
	{ \
		(list_head_ptr), \
		(list_head_ptr), \
		0 \
	}

struct gfarm_lru_cache {
	/* the head entry of doubly linked circular list */
	struct gfarm_lru_entry list_head;

	int free_cached_entries;
};

#define GFARM_LRU_CACHE_INITIALIZER(var) \
	{ \
		GFARM_LRU_CACHE_ENTRY_INITIALIZER(&(var).list_head), \
		0 \
	}

void gfarm_lru_entry_init(struct gfarm_lru_entry *);
void gfarm_lru_cache_init(struct gfarm_lru_cache *);

void gfarm_lru_cache_link_entry(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
void gfarm_lru_cache_link_entry_tail(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
void gfarm_lru_cache_access_entry(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
void gfarm_lru_cache_add_entry(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
void gfarm_lru_cache_purge_entry(struct gfarm_lru_entry *);
void gfarm_lru_init_uncached_entry(struct gfarm_lru_entry *);

void gfarm_lru_cache_addref_entry(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
int gfarm_lru_cache_delref_entry(struct gfarm_lru_cache *,
	struct gfarm_lru_entry *);
void gfarm_lru_cache_gc(struct gfarm_lru_cache *, int,
	void (*)(struct gfarm_lru_entry *, void *), void *, const char *,
	pthread_mutex_t *, const char *);

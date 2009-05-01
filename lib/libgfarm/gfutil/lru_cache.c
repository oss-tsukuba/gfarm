#include <assert.h>
#include <stdlib.h>

#include "gfutil.h"
#include "lru_cache.h"

/* link the entry to the head of the LRU cache list */
void
gfarm_lru_cache_link_entry(struct gfarm_lru_cache *cache,
	struct gfarm_lru_entry *entry)
{
	entry->next = cache->list_head.next;
	entry->prev = &cache->list_head;
	cache->list_head.next->prev = entry;
	cache->list_head.next = entry;
}

/* unlink the entry from the LRU cache list */
static void
gfarm_lru_cache_unlink_entry(struct gfarm_lru_entry *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

/* move the entry to the head of the LRU cache list */
void
gfarm_lru_cache_access_entry(struct gfarm_lru_cache *cache,
	struct gfarm_lru_entry *entry)
{
	gfarm_lru_cache_unlink_entry(entry);
	gfarm_lru_cache_link_entry(cache, entry);	
}

/* initialize the entry as the acquired state, and add it to the LRU cache */
void
gfarm_lru_cache_add_entry(struct gfarm_lru_cache *cache,
	struct gfarm_lru_entry *entry)
{
	entry->acquired = 1;
	gfarm_lru_cache_link_entry(cache, entry);
}

/* purge the entry from the LRU cache */
void
gfarm_lru_cache_purge_entry(struct gfarm_lru_entry *entry)
{
	gfarm_lru_cache_unlink_entry(entry);
	
	entry->prev = entry->next = NULL; /* mark the entry purged */

	/* note that this entry may be still acquired */
}

void
gfarm_lru_init_uncached_entry(struct gfarm_lru_entry *entry)
{
	entry->acquired = 1;
	entry->prev = entry->next = NULL; /* mark the entry purged */
}

/* acquire the entry */
void
gfarm_lru_cache_addref_entry(struct gfarm_lru_cache *cache,
	struct gfarm_lru_entry *entry)
{
	if (entry->acquired == 0) { /* must be a cached entry */
		assert(entry->prev != NULL && entry->next != NULL);

		--cache->free_cached_entries; /* now, this isn't free */
	}
	++entry->acquired;
	gfarm_lru_cache_access_entry(cache, entry);
}

/* free the entry */
int
gfarm_lru_cache_delref_entry(struct gfarm_lru_cache *cache,
	struct gfarm_lru_entry *entry)
{
	if (--entry->acquired <= 0) {
		if (entry->acquired < 0) {
			gflog_error("gfarm_lru_cache_delref_entry: %d\n",
			    entry->acquired);
			abort();
		}
		if (entry->prev != NULL) /* i.e. if cached entry */
			++cache->free_cached_entries; /* now, this is free */
		return (1); /* not occupied by anyone */
	}
	return (0); /* still occupied by someone else */
}

void
gfarm_lru_cache_gc(struct gfarm_lru_cache *cache, int free_target,
	void (*dispose_entry)(struct gfarm_lru_entry *, void *),
	void *closure, const char *entry_name)
{
	struct gfarm_lru_entry *entry, *prev;

	/* search least recently used connection */
	for (entry = cache->list_head.prev;
	    cache->free_cached_entries > free_target; entry = prev) {
		prev = entry->prev;

		if (entry == &cache->list_head) {
			gflog_error("free %s/target = %d/%d", entry_name,
			    cache->free_cached_entries, free_target);
			gflog_error("But no free %s is found.", entry_name);
			gflog_error("This shouldn't happen");
			abort();
		}

		if (entry->acquired <= 0) {
			gfarm_lru_cache_purge_entry(entry);
			--cache->free_cached_entries;

			/* abandon this free entry */
			(*dispose_entry)(entry, closure);
		}
	}
}

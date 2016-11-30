#include <stddef.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "tree.h"

#include "uint64_map.h"

/*
 * the reason why we use red-black-tree map instead of hash map
 * is because number of hardlinks is hard to estimate.
 */

struct uint64_to_uint64_map_entry {
	RB_ENTRY(uint64_to_uint64_map_entry) node;
	gfarm_uint64_t key;
	gfarm_uint64_t value;
};

struct uint64_to_uint64_map {
	RB_HEAD(uint64_to_uint64_head, uint64_to_uint64_map_entry) head;
	gfarm_uint64_t size;
};

static int
uint64_to_uint64_map_entry_compare(
	struct uint64_to_uint64_map_entry *a,
	struct uint64_to_uint64_map_entry *b)
{
	if (a->key < b->key)
		return (-1);
	else if (a->key > b->key)
		return (1);
	else
		return (0);
}

RB_PROTOTYPE(uint64_to_uint64_head,  uint64_to_uint64_map_entry,
	node, uint64_to_uint64_map_entry_compare)
RB_GENERATE(uint64_to_uint64_head,  uint64_to_uint64_map_entry,
	node, uint64_to_uint64_map_entry_compare)

struct uint64_to_uint64_map *
uint64_to_uint64_map_new(void)
{
	struct uint64_to_uint64_map *map;

	GFARM_MALLOC(map);
	if (map == NULL)
		return (NULL);

	RB_INIT(&map->head);
	map->size = 0;
	return (map);
}

static void
uint64_to_uint64_map_entry_delete(struct uint64_to_uint64_map *map,
	struct uint64_to_uint64_map_entry *entry)
{
	struct uint64_to_uint64_map_entry *deleted =
		RB_REMOVE(uint64_to_uint64_head, &map->head, entry);

	if (deleted) {
		map->size--;
		free(deleted);
	}
}

void
uint64_to_uint64_map_free(struct uint64_to_uint64_map *map)
{
	struct uint64_to_uint64_map_entry *entry;

	while ((entry = RB_MIN(uint64_to_uint64_head, &map->head)) != NULL)
		uint64_to_uint64_map_entry_delete(map, entry);
	free(map);
}

static struct uint64_to_uint64_map_entry *
uint64_to_uint64_map_enter(struct uint64_to_uint64_map *map,
	gfarm_uint64_t key, int *createdp)
{
	struct uint64_to_uint64_map_entry *entry, *found;

	GFARM_MALLOC(entry);
	if (entry == NULL)
		return (NULL);
	entry->key = key;

	found = RB_INSERT(uint64_to_uint64_head, &map->head, entry);
	if (found != NULL) {
		free(entry);
		if (createdp != NULL)
			*createdp = 0;
		return (found);
	} else {
		map->size++;
		if (createdp != NULL)
			*createdp = 1;
		return (entry);
	}
}

static struct uint64_to_uint64_map_entry *
uint64_to_uint64_map_lookup(struct uint64_to_uint64_map *map,
	gfarm_uint64_t key)
{
	struct uint64_to_uint64_map_entry entry;

	entry.key = key;
	return (RB_FIND(uint64_to_uint64_head, &map->head, &entry));
}

gfarm_uint64_t
uint64_to_uint64_map_size(struct uint64_to_uint64_map *map)
{
	return (map->size);
}

gfarm_uint64_t
uint64_to_uint64_map_entry_key(struct uint64_to_uint64_map_entry *entry)
{
	return (entry->key);
}

gfarm_uint64_t
uint64_to_uint64_map_entry_value(struct uint64_to_uint64_map_entry *entry)
{
	return (entry->value);
}

int
uint64_to_uint64_map_inc_value(struct uint64_to_uint64_map *map,
	gfarm_uint64_t key, gfarm_uint64_t *valuep)
{
	struct uint64_to_uint64_map_entry *entry;
	int created;

	entry = uint64_to_uint64_map_lookup(map, key);
	if (entry == NULL) {
		entry = uint64_to_uint64_map_enter(map, key, &created);
		if (entry == NULL)
			return (0); /* failed due to memory shortage */
		if (created)
			entry->value = 0;
	}
	entry->value++;
	if (valuep != NULL)
		*valuep = entry->value;
	return (1); /* success */
}

int
uint64_to_uint64_map_foreach(struct uint64_to_uint64_map *map, void *closure,
	int (*callback)(void *, struct uint64_to_uint64_map_entry *))
{
	struct uint64_to_uint64_map_entry *entry;

	RB_FOREACH(entry, uint64_to_uint64_head, &map->head) {
		if ((*callback)(closure, entry))
			return (1); /* interrupted */
	}
	return (0); /* completed */
}

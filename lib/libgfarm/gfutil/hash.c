#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/gflog.h>

#include "gfutil.h"
#include "hash.h"

#define ALIGNMENT 16
#define HASH_ALIGN(p) (((unsigned long)(p) + ALIGNMENT - 1) & ~(ALIGNMENT - 1))

/* string hash function by Peter Weinberger */
int
gfarm_hash_default(const void *key, int keylen)
{
	int i;
	unsigned int hash = 0, g;

	for (i = 0; i < keylen; i++) {
		hash = (hash << 4) + ((unsigned char *)key)[i];
		/* this assumes size of `int' is 32 bit */
		if ((g = hash & 0xf0000000) != 0) {
			hash ^= g >> 24;
			hash ^= g;
		}
	}
	return (hash);
}

int
gfarm_hash_casefold(const void *key, int keylen)
{
	int i;
	unsigned int hash = 0, g;

	for (i = 0; i < keylen; i++) {
		hash = (hash << 4) + tolower(((unsigned char *)key)[i]);
		/* this assumes size of `int' is 32 bit */
		if ((g = hash & 0xf0000000) != 0) {
			hash ^= g >> 24;
			hash ^= g;
		}
	}
	return (hash);
}

int
gfarm_hash_key_equal_default(const void *key1, int key1len,
			     const void *key2, int key2len)
{
	return (key1len == key2len && memcmp(key1, key2, key1len) == 0);
}

int
gfarm_hash_key_equal_casefold(const void *key1, int key1len,
			      const void *key2, int key2len)
{
	int i;

	if (key1len != key2len)
		return (0);
	for (i = 0; i < key1len; i++) {
		if (tolower(((unsigned char *)key1)[i]) !=
		    tolower(((unsigned char *)key2)[i]))
			return (0);
	}
	return (1);
}

struct gfarm_hash_entry {
	struct gfarm_hash_entry *next;
	int key_length;
	int data_length;
	double key_stub;
};

#define HASH_KEY(entry)	\
	(((char *)(entry)) + \
	 HASH_ALIGN(offsetof(struct gfarm_hash_entry, key_stub)))
#define HASH_DATA(entry) \
	(HASH_KEY(entry) + HASH_ALIGN((entry)->key_length))

struct gfarm_hash_table {
	int table_size;

	int (*hash)(const void *, int);
	int (*equal)(const void *, int, const void *, int);

	struct gfarm_hash_entry *buckets[1];
};

struct gfarm_hash_table *
gfarm_hash_table_alloc(int size,
		       int (*hash)(const void *, int),
		       int (*equal)(const void *, int, const void *, int))
{
	struct gfarm_hash_table *hashtab;
	size_t alloc_size;
	int overflow = 0;

	alloc_size = gfarm_size_add(&overflow,
			sizeof(struct gfarm_hash_table),
			gfarm_size_mul(&overflow,
				sizeof(struct gfarm_hash_entry *), size - 1));
	if (overflow) {
		gflog_debug(GFARM_MSG_1000783,
			"Overflow when allocating hash table, size=(%d)",
			size);
		return (NULL);
	}
	hashtab = malloc(alloc_size); /* size is already checked */
	if (hashtab == NULL) {
		gflog_debug(GFARM_MSG_1000784,
			"allocation of 'gfarm_hash_table' (%zd) failed",
			alloc_size);
		return (NULL);
	}
	hashtab->table_size = size;
	hashtab->hash = hash;
	hashtab->equal = equal;
	memset(hashtab->buckets, 0, sizeof(struct gfarm_hash_entry *) * size);
	return (hashtab);
}

void
gfarm_hash_table_free(struct gfarm_hash_table *hashtab)
{
	int i;
	struct gfarm_hash_entry *p, *np;

	for (i = 0; i < hashtab->table_size; i++) {
		for (p = hashtab->buckets[i]; p != NULL; p = np) {
			np = p->next;
			free(p);
		}
	}
	free(hashtab);
}

static struct gfarm_hash_entry **
gfarm_hash_lookup_internal_search(struct gfarm_hash_table *hashtab,
	struct gfarm_hash_entry **pp,
	const void *key, int keylen)
{
	struct gfarm_hash_entry *p;
	int (*equal)(const void *, int, const void *, int) = hashtab->equal;

	for (p = *pp; p != NULL; pp = &p->next, p = *pp) {
		if ((*equal)(HASH_KEY(p), p->key_length, key, keylen))
			break;
	}
	return (pp);
}

#define HASH_BUCKET(hashtab, key, keylen) \
	((*hashtab->hash)(key, keylen) % hashtab->table_size)

#define GFARM_HASH_LOOKUP_INTERNAL(hashtab, key, keylen) \
	gfarm_hash_lookup_internal_search(hashtab, \
	    &hashtab->buckets[HASH_BUCKET(hashtab, key, keylen)], \
	    key, keylen)

struct gfarm_hash_entry *
gfarm_hash_lookup(struct gfarm_hash_table *hashtab,
		  const void *key, int keylen)
{
	struct gfarm_hash_entry **pp =
		GFARM_HASH_LOOKUP_INTERNAL(hashtab, key, keylen);

	return (*pp);
}

struct gfarm_hash_entry *
gfarm_hash_enter(struct gfarm_hash_table *hashtab, const void *key, int keylen,
		  int datalen, int *createdp)
{
	struct gfarm_hash_entry *p, **pp =
		GFARM_HASH_LOOKUP_INTERNAL(hashtab, key, keylen);
	size_t hash_entry_size;
	int overflow = 0;

	if (createdp != NULL)
		*createdp = 0;

	if (*pp != NULL)
		return (*pp);

	/*
	 * create if not found
	 */
	hash_entry_size =
		gfarm_size_add(&overflow,
		    gfarm_size_add(&overflow,
			HASH_ALIGN(offsetof(struct gfarm_hash_entry, key_stub)),
			HASH_ALIGN(keylen)),
		    datalen);
	if (overflow) {
		gflog_debug(GFARM_MSG_1000785,
			"Overflow when entering hash entry");
		return (NULL);
	}
	p = malloc(hash_entry_size); /* size is already checked */
	if (p == NULL) {
		gflog_debug(GFARM_MSG_1000786,
			"allocation of 'gfarm_hash_entry' failed (%zd)",
			hash_entry_size);
		return (NULL);
	}
	*pp = p;

	p->next = NULL;
	p->key_length = keylen;
	p->data_length = datalen;
	memcpy(HASH_KEY(p), key, keylen);

	if (createdp != NULL)
		*createdp = 1;
	return (p);
}

int
gfarm_hash_purge(struct gfarm_hash_table *hashtab, const void *key, int keylen)
{
	struct gfarm_hash_entry *p, **pp =
		GFARM_HASH_LOOKUP_INTERNAL(hashtab, key, keylen);

	p = *pp;
	if (p == NULL)
		return (0); /* key is not found */
	*pp = p->next;
	free(p);
	return (1); /* purged */
}

void *
gfarm_hash_entry_key(struct gfarm_hash_entry *entry)
{
	return (HASH_KEY(entry));
}

int
gfarm_hash_entry_key_length(struct gfarm_hash_entry *entry)
{
	return (entry->key_length);
}

void *
gfarm_hash_entry_data(struct gfarm_hash_entry *entry)
{
	return (HASH_DATA(entry));
}

int
gfarm_hash_entry_data_length(struct gfarm_hash_entry *entry)
{
	return (entry->data_length);
}

/*
 * hash iterator
 */
int
gfarm_hash_iterator_valid_entry(struct gfarm_hash_iterator *iterator)
{
	struct gfarm_hash_table *hashtab = iterator->table;

	if (iterator->bucket_index >= hashtab->table_size)
		return (0);
	while (*iterator->pp == NULL) {
		if (++iterator->bucket_index >= hashtab->table_size)
			return (0);
		iterator->pp = &hashtab->buckets[iterator->bucket_index];
	}
	return (1);
}

void
gfarm_hash_iterator_begin(struct gfarm_hash_table *hashtab,
	struct gfarm_hash_iterator *iterator)
{
	iterator->table = hashtab;
	iterator->bucket_index = 0;
	iterator->pp = &hashtab->buckets[0 /* == iterator->bucket_index */];
}

void
gfarm_hash_iterator_next(struct gfarm_hash_iterator *iterator)
{
	if (*iterator->pp == NULL)
		iterator->bucket_index++;
	else
		iterator->pp = &(*iterator->pp)->next;
}

int
gfarm_hash_iterator_is_end(struct gfarm_hash_iterator *iterator)
{
	return (!gfarm_hash_iterator_valid_entry(iterator));
}

struct gfarm_hash_entry *
gfarm_hash_iterator_access(struct gfarm_hash_iterator *iterator)
{
	if (gfarm_hash_iterator_valid_entry(iterator))
		return (*iterator->pp);
	else
		return (NULL);
}

int
gfarm_hash_iterator_lookup(struct gfarm_hash_table *hashtab,
	const void *key, int keylen,
	struct gfarm_hash_iterator *iterator)
{
	iterator->table = hashtab;
	iterator->bucket_index = HASH_BUCKET(hashtab, key, keylen);
	iterator->pp = gfarm_hash_lookup_internal_search(hashtab,
	    &hashtab->buckets[HASH_BUCKET(hashtab, key, keylen)],
	    key, keylen);
	return (*iterator->pp != NULL);
}

int
gfarm_hash_iterator_purge(struct gfarm_hash_iterator *iterator)
{
	struct gfarm_hash_entry *p;

	if (!gfarm_hash_iterator_valid_entry(iterator))
		return (0); /* not purged */
	p = *iterator->pp;
	*iterator->pp = p->next;
	free(p);
	return (1); /* purged */
}

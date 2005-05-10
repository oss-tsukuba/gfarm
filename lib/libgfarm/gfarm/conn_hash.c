#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "hash.h"
#include "conn_hash.h"

struct gfp_conn_hash_id {
	char *hostname;
	int port;
	char *username;
};

static int
gfp_conn_hash_index(const void *key, int keylen)
{
	const struct gfp_conn_hash_id *id = key;

	return (gfarm_hash_casefold(id->hostname, strlen(id->hostname)) +
	    id->port * 3 +
	    gfarm_hash_default(id->username, strlen(id->username)) * 5);
}

static int
gfp_conn_hash_equal(const void *key1, int key1len,
			     const void *key2, int key2len)
{
	const struct gfp_conn_hash_id *id1 = key1, *id2 = key2;

	return (strcasecmp(id1->hostname, id2->hostname) == 0 &&
	    id1->port == id2->port &&
	    strcmp(id1->username, id2->username) == 0);
}

char *
gfp_conn_hash_hostname(struct gfarm_hash_entry *entry)
{
	struct gfp_conn_hash_id *id = gfarm_hash_entry_key(entry);

	return (id->hostname);
}

char *
gfp_conn_hash_username(struct gfarm_hash_entry *entry)
{
	struct gfp_conn_hash_id *id = gfarm_hash_entry_key(entry);

	return (id->username);
}

gfarm_error_t
gfp_conn_hash_enter(struct gfarm_hash_table **hashtabp, int hashtabsize,
	size_t entrysize,
	const char *hostname, int port, const char *username,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	struct gfarm_hash_table *hashtab = *hashtabp;
	struct gfp_conn_hash_id id;
	struct gfarm_hash_entry *entry;
	int created;

	if (hashtab == NULL) {
		hashtab = gfarm_hash_table_alloc(hashtabsize,
		    gfp_conn_hash_index, gfp_conn_hash_equal);
		if (hashtab == NULL)
			return (GFARM_ERR_NO_MEMORY);
		*hashtabp = hashtab;
	}
	id.hostname = strdup(hostname);
	id.port = port;
	id.username = strdup(username);
	if (id.hostname == NULL || id.username == NULL) {
		if (id.hostname != NULL)
			free(id.hostname);
		if (id.username != NULL)
			free(id.username);
		return (GFARM_ERR_NO_MEMORY);
	}
	entry = gfarm_hash_enter(hashtab, &id, sizeof(id), entrysize,
	    &created);
	if (entry == NULL) {
		free(id.hostname);
		free(id.username);
		return (GFARM_ERR_NO_MEMORY);
	}
	*entry_ret = entry;
	*created_ret = created;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_conn_hash_purge(struct gfarm_hash_table *hashtab,
	struct gfarm_hash_entry *entry)
{
	void *key = gfarm_hash_entry_key(entry);
	int keylen = gfarm_hash_entry_key_length(entry);
	struct gfp_conn_hash_id *idp = key;
	struct gfp_conn_hash_id id = *idp;

	gfarm_hash_purge(hashtab, key, keylen);
	free(id.hostname);
	free(id.username);
}

void
gfp_conn_hash_iterator_purge(struct gfarm_hash_iterator *iterator)
{
	void *key = gfarm_hash_entry_key(gfarm_hash_iterator_access(iterator));
	struct gfp_conn_hash_id *idp = key;
	struct gfp_conn_hash_id id = *idp;

	gfarm_hash_iterator_purge(iterator);
	free(id.hostname);
	free(id.username);
}

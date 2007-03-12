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

int
gfp_conn_hash_port(struct gfarm_hash_entry *entry)
{
	struct gfp_conn_hash_id *id = gfarm_hash_entry_key(entry);

	return (id->port);
}

static gfarm_error_t
gfp_conn_hash_table_init(
	struct gfarm_hash_table **hashtabp, int hashtabsize)
{
	struct gfarm_hash_table *hashtab;

	hashtab = gfarm_hash_table_alloc(hashtabsize,
	    gfp_conn_hash_index, gfp_conn_hash_equal);
	if (hashtab == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*hashtabp = hashtab;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_conn_hash_enter(struct gfarm_hash_table **hashtabp, int hashtabsize,
	size_t entrysize,
	const char *hostname, int port, const char *username,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	gfarm_error_t e;
	struct gfp_conn_hash_id id, *idp;
	struct gfarm_hash_entry *entry;
	int created;

	if (*hashtabp == NULL &&
	    (e = gfp_conn_hash_table_init(hashtabp, hashtabsize))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	id.username = (char *)username; /* UNCONST */
	entry = gfarm_hash_enter(*hashtabp, &id, sizeof(id), entrysize,
	    &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (created) {
		idp = gfarm_hash_entry_key(entry);
		idp->hostname = strdup(hostname);
		idp->username = strdup(username);
		if (idp->hostname == NULL || idp->username == NULL) {
			if (idp->hostname != NULL)
				free(idp->hostname);
			if (idp->username != NULL)
				free(idp->username);
			idp->hostname = (char *)hostname; /* UNCONST */
			idp->username = (char *)username; /* UNCONST */
			gfarm_hash_purge(*hashtabp, &id, sizeof(id));
			return (GFARM_ERR_NO_MEMORY);
		}
	}

	*entry_ret = entry;
	*created_ret = created;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_conn_hash_lookup(struct gfarm_hash_table **hashtabp, int hashtabsize,
	const char *hostname, int port, const char *username,
	struct gfarm_hash_entry **entry_ret)
{
	gfarm_error_t e;
	struct gfp_conn_hash_id id;
	struct gfarm_hash_entry *entry;

	if (*hashtabp == NULL &&
	    (e = gfp_conn_hash_table_init(hashtabp, hashtabsize)) !=
	    GFARM_ERR_NO_ERROR)
		return (e);

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	id.username = (char *)username; /* UNCONST */
	entry = gfarm_hash_lookup(*hashtabp, &id, sizeof(id));
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	*entry_ret = entry;
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

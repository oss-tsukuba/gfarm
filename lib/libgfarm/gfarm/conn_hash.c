#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "hash.h"
#include "conn_hash.h"

static int
gfp_conn_hash_index(const void *key, int keylen)
{
	const struct gfp_conn_hash_id *id = key;

	/*
	 * XXX FIXME: username is currently removed from keys,
	 * to make GSI authentication work.
	 * (Should we change GSI authentication protocol?)
	 */
	return (gfarm_hash_casefold(id->hostname, strlen(id->hostname)) +
#ifdef __KERNEL__	/* id->username :: multi user */
		gfarm_hash_default(id->username, strlen(id->username)) +
#endif /* __KERNEL__ */
		id->port * 3);
}

static int
gfp_conn_hash_equal(const void *key1, int key1len,
			     const void *key2, int key2len)
{
	const struct gfp_conn_hash_id *id1 = key1, *id2 = key2;

	/* XXX FIXME: username is currently removed from keys */
	return (strcasecmp(id1->hostname, id2->hostname) == 0 &&
#ifdef __KERNEL__	/* id->username :: multi user */
		strcmp(id1->username, id2->username) == 0 &&
#endif /* __KERNEL__ */
		id1->port == id2->port);
}

const char *
gfp_conn_hash_hostname(struct gfarm_hash_entry *entry)
{
	struct gfp_conn_hash_id *id = gfarm_hash_entry_key(entry);

	return (id->hostname);
}

const char *
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

gfarm_error_t
gfp_conn_hash_table_init(
	struct gfarm_hash_table **hashtabp, int hashtabsize)
{
	struct gfarm_hash_table *hashtab;

	hashtab = gfarm_hash_table_alloc(hashtabsize,
	    gfp_conn_hash_index, gfp_conn_hash_equal);
	if (hashtab == NULL) {
		gflog_debug(GFARM_MSG_1001081,
			"allocation of hashtable(%d) failed: %s",
			hashtabsize,
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*hashtabp = hashtab;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_conn_hash_table_dispose(struct gfarm_hash_table *hashtab)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct gfp_conn_hash_id *idp;
	char *hostname, *username;

	gfarm_hash_iterator_begin(hashtab, &it);
	for (;;) {
		if (gfarm_hash_iterator_is_end(&it))
			break;
		entry = gfarm_hash_iterator_access(&it);
		idp = gfarm_hash_entry_key(entry);
		hostname = idp->hostname;
		username = idp->username;
		gfarm_hash_iterator_purge(&it);
		free(hostname);
		free(username);
	}

	gfarm_hash_table_free(hashtab);
}

gfarm_error_t
gfp_conn_hash_id_enter_noalloc(struct gfarm_hash_table **hashtabp,
	int hashtabsize,
	size_t entrysize, struct gfp_conn_hash_id *idp,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	int created;

	if (*hashtabp == NULL &&
	    (e = gfp_conn_hash_table_init(hashtabp, hashtabsize))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001082,
		    "initialization of connection hashtable (%d) failed: %s",
		    hashtabsize, gfarm_error_string(e));
		return (e);
	}

	assert(idp);
	assert(idp->hostname);
	assert(idp->username);
	assert(idp->port > 0);
	entry = gfarm_hash_enter(*hashtabp, idp, sizeof(*idp), entrysize,
	    &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001083,
			"insertion to hashtable (%zd) failed: %s",
			entrysize,
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	*entry_ret = entry;
	*created_ret = created;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_conn_hash_id_enter(struct gfarm_hash_table **hashtabp, int hashtabsize,
	size_t entrysize, struct gfp_conn_hash_id *idp,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	gfarm_error_t e;
	char *h, *u;

	if ((e = gfp_conn_hash_id_enter_noalloc(
	    hashtabp, hashtabsize, entrysize, idp, entry_ret, created_ret))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002562,
		    "(%s/%d/%s): %s",
		    idp->hostname, idp->port, idp->username,
		    gfarm_error_string(e));
		return (e);
	}
	if (*created_ret) {
		h = strdup(idp->hostname);
		u = strdup(idp->username);
		if (h == NULL || u == NULL) {
			gflog_debug(GFARM_MSG_1002563,
			    "(%s/%d/%s): no memory",
			    idp->hostname, idp->port, idp->username);
			free(h);
			free(u);
			gfp_conn_hash_purge(*hashtabp, *entry_ret);
			return (GFARM_ERR_NO_MEMORY);
		}
		idp = gfarm_hash_entry_key(*entry_ret);
		idp->hostname = h;
		idp->username = u;
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfp_conn_hash_enter_noalloc(struct gfarm_hash_table **hashtabp, int hashtabsize,
	size_t entrysize, const char *hostname, int port, const char *username,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	struct gfp_conn_hash_id id;

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	id.username = (char *)username; /* UNCONST */
	return (gfp_conn_hash_id_enter_noalloc(hashtabp, hashtabsize, entrysize,
	    &id, entry_ret, created_ret));
}

gfarm_error_t
gfp_conn_hash_enter(struct gfarm_hash_table **hashtabp, int hashtabsize,
	size_t entrysize, const char *hostname, int port, const char *username,
	struct gfarm_hash_entry **entry_ret, int *created_ret)
{
	struct gfp_conn_hash_id id;

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	id.username = (char *)username; /* UNCONST */
	return (gfp_conn_hash_id_enter(hashtabp, hashtabsize, entrysize,
	    &id, entry_ret, created_ret));
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
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001085,
		    "initialization of connection hashtable (%d) failed: %s",
		    hashtabsize, gfarm_error_string(e));
		return (e);
	}

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	id.username = (char *)username; /* UNCONST */
	entry = gfarm_hash_lookup(*hashtabp, &id, sizeof(id));
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001086,
			"lookup in hashtable (%s)(%d)(%s) failed",
			hostname, port, username);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	*entry_ret = entry;
	return (GFARM_ERR_NO_ERROR);
}

void
gfp_conn_hash_purge(struct gfarm_hash_table *hashtab,
	struct gfarm_hash_entry *entry)
{
	void *key = gfarm_hash_entry_key(entry);
	int keylen = gfarm_hash_entry_key_length(entry);
	gfarm_hash_purge(hashtab, key, keylen);
}

void
gfp_conn_hash_iterator_purge(struct gfarm_hash_iterator *iterator)
{
	gfarm_hash_iterator_purge(iterator);
}

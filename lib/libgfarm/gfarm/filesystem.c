#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "hash.h"

#include "filesystem.h"
#include "metadb_server.h"

struct gfarm_metadb_server;

struct gfarm_filesystem {
	struct gfarm_filesystem *next;
	struct gfarm_metadb_server **servers;
	int nservers, flags;
};

struct gfarm_filesystem_hash_id {
	const char *hostname;
	int port;
};

static struct gfarm_filesystem filesystems = { &filesystems };
static struct gfarm_hash_table *ms2fs_hashtab;

#define GFARM_FILESYSTEM_FLAG_IS_DEFAULT	0x00000001

static int
gfarm_filesystem_hash_index(const void *key, int keylen)
{
	const struct gfarm_filesystem_hash_id *id = key;
	const char *hostname = id->hostname;

	return (gfarm_hash_casefold(hostname, strlen(hostname)) + id->port * 3);
}

static int
gfarm_filesystem_hash_equal(const void *key1, int key1len,
	const void *key2, int key2len)
{
	const struct gfarm_filesystem_hash_id *id1 = key1;
	const struct gfarm_filesystem_hash_id *id2 = key2;

	return (strcasecmp(id1->hostname, id2->hostname) == 0 &&
	    id1->port == id2->port);
}

static gfarm_error_t
gfarm_filesystem_add(struct gfarm_filesystem **fsp)
{
	gfarm_error_t e;
	struct gfarm_filesystem *fs;

	GFARM_MALLOC(fs);
	if (fs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	fs->next = filesystems.next;
	fs->servers = NULL;
	fs->nservers = 0;
	fs->flags = 0;
	filesystems.next = fs;
	*fsp = fs;
	return (GFARM_ERR_NO_ERROR);
}

int
gfarm_filesystem_is_initialized(void)
{
	return (filesystems.next != &filesystems);
}

gfarm_error_t
gfarm_filesystem_init(void)
{
#define MS2FS_HASHTAB_SIZE 5
	gfarm_error_t e;
	struct gfarm_filesystem *fs;

	if (gfarm_filesystem_is_initialized())
		return (GFARM_ERR_NO_ERROR);
	if ((e = gfarm_filesystem_add(&fs)) != GFARM_ERR_NO_ERROR)
		return (e);
	fs->flags |= GFARM_FILESYSTEM_FLAG_IS_DEFAULT;

	ms2fs_hashtab = gfarm_hash_table_alloc(MS2FS_HASHTAB_SIZE,
	    gfarm_filesystem_hash_index, gfarm_filesystem_hash_equal);
	if (ms2fs_hashtab == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_filesystem_hash_enter(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server *ms)
{
	int created;
	struct gfarm_hash_entry *entry;
	struct gfarm_filesystem **fsp;
	struct gfarm_filesystem_hash_id id;

	id.hostname = gfarm_metadb_server_get_name(ms);
	id.port = gfarm_metadb_server_get_port(ms);
	entry = gfarm_hash_enter(ms2fs_hashtab, &id, sizeof(id), sizeof(fs),
	    &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	assert(created);
	/* memory owner of hostname is gfarm_metadb_server */
	fsp = gfarm_hash_entry_data(entry);
	*fsp = fs;
	return (GFARM_ERR_NO_ERROR);
}

static int
gfarm_filesystem_is_default(struct gfarm_filesystem *fs)
{
	return ((fs->flags & GFARM_FILESYSTEM_FLAG_IS_DEFAULT) != 0);
}

struct gfarm_filesystem*
gfarm_filesystem_get_default(void)
{
	struct gfarm_filesystem *fs = filesystems.next;

	assert(gfarm_filesystem_is_default(fs));
	return (fs);
}

struct gfarm_filesystem*
gfarm_filesystem_get(const char *hostname, int port)
{
	struct gfarm_filesystem_hash_id id;
	struct gfarm_hash_entry *entry;

	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	entry = gfarm_hash_lookup(ms2fs_hashtab, &id, sizeof(id));
	return (entry ?
	    *(struct gfarm_filesystem **)gfarm_hash_entry_data(entry) : NULL);
}

gfarm_error_t
gfarm_filesystem_set_metadb_server_list(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server **metadb_servers, int n)
{
	gfarm_error_t e;
	int i;
	struct gfarm_metadb_server **servers;

	servers = malloc(sizeof(void *) * n);
	if (servers == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	memcpy(servers, metadb_servers, sizeof(void *) * n);
	fs->servers = servers;
	fs->nservers = n;

	for (i = 0; i < n; ++i) {
		if ((e = gfarm_filesystem_hash_enter(fs, servers[i])) !=
		    GFARM_ERR_NO_ERROR)
			return (e);
	}

	return (GFARM_ERR_NO_ERROR);
}

struct gfarm_metadb_server**
gfarm_filesystem_get_metadb_server_list(struct gfarm_filesystem *fs, int *np)
{
	*np = fs->nservers;
	return (fs->servers);
}

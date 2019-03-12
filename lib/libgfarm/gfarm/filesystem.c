#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "hash.h"

#include "context.h"
#include "filesystem.h"
#include "metadb_server.h"
#include "gfm_client.h"
#include "gfs_file_list.h"

struct gfarm_metadb_server;

struct gfarm_filesystem {
	struct gfarm_filesystem *next;
	struct gfarm_metadb_server **servers;
	int nservers, flags;

	/*
	 * opened file list
	 *
	 * all gfm_connection related to GFS_File in file_list MUST be
	 * the same instance to execute failover process against all opened
	 * files at the same time in gfs_pio_failover().
	 */
	struct gfs_file_list *file_list;

	/*
	 * detected failover but not yet recovered.
	 *
	 * gfm_connection MUST NOT be acquired without calling
	 * gfs_pio_failover() when failover_detected is true.
	 */
	int failover_detected;

	int failover_count;

	/* if gfm_connection of this filesystem is in failover process or not */
	int in_failover_process;
};

struct gfarm_filesystem_hash_id {
	const char *hostname;
	int port;
};

#define staticp	(gfarm_ctxp->filesystem_static)

struct gfarm_filesystem_static {
	struct gfarm_filesystem filesystems;
	struct gfarm_hash_table *ms2fs_hashtab;
};

#define MS2FS_HASHTAB_SIZE 17

static void gfarm_filesystem_free(struct gfarm_filesystem *);

gfarm_error_t
gfarm_filesystem_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_filesystem_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	memset(&s->filesystems, 0, sizeof(s->filesystems));
	s->filesystems.next = &s->filesystems;
	s->ms2fs_hashtab = NULL;

	ctxp->filesystem_static = s;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_filesystem_m2fs_hashtab_free(void)
{
	if (staticp->ms2fs_hashtab != NULL) {
		gfarm_hash_table_free(staticp->ms2fs_hashtab);
		staticp->ms2fs_hashtab = NULL;
	}
}

void
gfarm_filesystem_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_filesystem_static *s = ctxp->filesystem_static;
	struct gfarm_filesystem *fs;

	if (s == NULL)
		return;

	gfarm_filesystem_m2fs_hashtab_free();
	for (;;) {
		fs = staticp->filesystems.next;
		if (fs == &staticp->filesystems)
			break;
		gfs_pio_file_list_free(fs->file_list);
		gfarm_filesystem_free(fs);
		free(fs);
	}
	free(s);
}

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

gfarm_error_t
gfarm_filesystem_new(struct gfarm_filesystem **fsp)
{
	gfarm_error_t e;
	struct gfarm_filesystem *fs;
	struct gfs_file_list *gfl;

	GFARM_MALLOC(fs);
	if (fs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002567,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	gfl = gfs_pio_file_list_alloc();
	if (gfl == NULL) {
		free(fs);
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003872,
		    "alloc gfs_file_list: %s",
		    gfarm_error_string(e));
		return (e);
	}
	fs->next = staticp->filesystems.next;
	fs->servers = NULL;
	fs->nservers = 0;
	fs->flags = 0;
	fs->file_list = gfl;
	fs->failover_detected = 0;
	fs->failover_count = 0;
	fs->in_failover_process = 0;
	staticp->filesystems.next = fs;
	*fsp = fs;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_filesystem_free(struct gfarm_filesystem *fs)
{
	int i;
	struct gfarm_metadb_server *ms;
	struct gfarm_filesystem *p;

	for (i = 0; i < fs->nservers; ++i) {
		ms = fs->servers[i];
		gfarm_metadb_server_free(ms);
		free(ms);
	}
	free(fs->servers);

	for (p = &staticp->filesystems; p->next != &staticp->filesystems;
	     p = p->next) {
		if (p->next == fs) {
			p->next = fs->next;
			break;
		}
	}
}

static gfarm_error_t
gfarm_filesystem_ms2fs_hash_alloc(void)
{
	if (staticp->ms2fs_hashtab != NULL)
		return (GFARM_ERR_NO_ERROR);

	staticp->ms2fs_hashtab = gfarm_hash_table_alloc(MS2FS_HASHTAB_SIZE,
	    gfarm_filesystem_hash_index, gfarm_filesystem_hash_equal);
	if (staticp->ms2fs_hashtab == NULL) {
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
	static const char diag[] = "gfarm_filesystem_hash_enter";

	id.hostname = gfarm_metadb_server_get_name(ms);
	id.port = gfarm_metadb_server_get_port(ms);
	entry = gfarm_hash_enter(staticp->ms2fs_hashtab, &id, sizeof(id),
	    sizeof(fs), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1002569,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_1004729,
		    "%s: duplicate host: %s", diag, id.hostname);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	/* memory owner of hostname is gfarm_metadb_server */
	fsp = gfarm_hash_entry_data(entry);
	*fsp = fs;
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_filesystem_hash_purge(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server *ms)
{
	struct gfarm_filesystem_hash_id id;
	int r;

	id.hostname = gfarm_metadb_server_get_name(ms);
	id.port = gfarm_metadb_server_get_port(ms);
	r = gfarm_hash_purge(staticp->ms2fs_hashtab, &id, sizeof(id));
	assert(r);
	(void)r;
}

void
gfarm_filesystem_set_default(struct gfarm_filesystem *fs)
{
	fs->flags |= GFARM_FILESYSTEM_FLAG_IS_DEFAULT;
}

static int
gfarm_filesystem_is_default(struct gfarm_filesystem *fs)
{
	return ((fs->flags & GFARM_FILESYSTEM_FLAG_IS_DEFAULT) != 0);
}

struct gfarm_filesystem *
gfarm_filesystem_get_default(void)
{
	struct gfarm_filesystem *fs = staticp->filesystems.next;

	for (; fs != &staticp->filesystems; fs = fs->next) {
		if (gfarm_filesystem_is_default(fs))
			return (fs);
	}
	return (NULL);
}

struct gfarm_filesystem*
gfarm_filesystem_get(const char *hostname, int port)
{
	struct gfarm_filesystem_hash_id id;
	struct gfarm_hash_entry *entry;

	if (staticp->ms2fs_hashtab == NULL)
		return (NULL);
	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	entry = gfarm_hash_lookup(staticp->ms2fs_hashtab, &id, sizeof(id));
	return (entry ?
	    *(struct gfarm_filesystem **)gfarm_hash_entry_data(entry) : NULL);
}

struct gfarm_filesystem*
gfarm_filesystem_get_by_connection(struct gfm_connection *gfm_server)
{
	return (gfarm_filesystem_get(gfm_client_hostname(gfm_server),
	    gfm_client_port(gfm_server)));
}

static gfarm_error_t
gfarm_filesystem_update_metadb_server_list(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server **metadb_servers, int n,
	int do_purge)
{
	gfarm_error_t e;
	int i;
	struct gfarm_metadb_server *ms, **servers;

	GFARM_MALLOC_ARRAY(servers, sizeof(void *) * n);
	if (servers == NULL) {
		gflog_debug(GFARM_MSG_1002570,
		    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfarm_filesystem_ms2fs_hash_alloc()) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002568, "%s", gfarm_error_string(e));
		free(servers);
		return (e);
	}

	for (i = 0; i < fs->nservers; ++i) {
		gfarm_metadb_server_set_is_removed(fs->servers[i], 1);
		if (do_purge)
			gfarm_filesystem_hash_purge(fs, fs->servers[i]);
	}
	for (i = 0; i < n; ++i)
		gfarm_metadb_server_set_is_removed(metadb_servers[i], 0);
	for (i = 0; i < fs->nservers; ++i) {
		ms = fs->servers[i];
		if (gfarm_metadb_server_is_removed(ms)) {
			if (gfarm_metadb_server_is_memory_owned_by_fs(ms)) {
				gfarm_metadb_server_free(ms);
				free(ms);
			}
		}
	}
	free(fs->servers);
	fs->servers = NULL;
	fs->nservers = 0;

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

gfarm_error_t
gfarm_filesystem_set_metadb_server_list(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server **metadb_servers, int n)
{
	return (gfarm_filesystem_update_metadb_server_list(
	    fs, metadb_servers, n, 1));
}

gfarm_error_t
gfarm_filesystem_replace_metadb_server_list(struct gfarm_filesystem *fs,
	struct gfarm_metadb_server **metadb_servers, int n)
{
	/*
	 * we cannot call gfarm_filesystem_hash_purge() by each fs->servers[i],
	 * because gfarm_metadb_server_get_port(fs->servers[i]) may be
	 * already broken by "gfmdhost -m -p <port>",
	 * when this is called from mdhost_updated() in gfmd.
	 */
	gfarm_filesystem_m2fs_hashtab_free();

	/* do_purge == false due to the reason above */
	return (gfarm_filesystem_update_metadb_server_list(
	    fs, metadb_servers, n, 0));
}

gfarm_error_t
gfarm_filesystem_add(const char *hostname, int port,
	struct gfarm_filesystem **fsp)
{
	gfarm_error_t e;
	char *host = NULL;
	struct gfarm_metadb_server *ms = NULL, *mss[1];
	struct gfarm_filesystem *fs = gfarm_filesystem_get(hostname, port);

	if (fs != NULL)
		return (GFARM_ERR_NO_ERROR);

	host = strdup(hostname);
	if (host == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003873,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_metadb_server_new(&ms, host, port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003874,
		    "%s", gfarm_error_string(e));
		goto error;
	}
	if ((e = gfarm_filesystem_new(&fs)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003875,
		    "%s", gfarm_error_string(e));
		goto error;
	}

	mss[0] = ms;
	if ((e = gfarm_filesystem_set_metadb_server_list(fs, mss, 1))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003876,
		    "%s", gfarm_error_string(e));
		goto error;
	}

	*fsp = fs;
	return (GFARM_ERR_NO_ERROR);

error:
	if (fs != NULL) {
		gfarm_filesystem_free(fs);
		free(fs);
	} else if (ms != NULL) {
		gfarm_metadb_server_free(ms);
		free(ms);
	} else if (host != NULL)
		free(host);
	return (e);
}

struct gfarm_metadb_server**
gfarm_filesystem_get_metadb_server_list(struct gfarm_filesystem *fs, int *np)
{
	*np = fs->nservers;
	return (fs->servers);
}

struct gfarm_metadb_server*
gfarm_filesystem_get_metadb_server_first(struct gfarm_filesystem *fs)
{
	assert(fs->nservers > 0);
	return (fs->servers[0]);
}

struct gfs_file_list *
gfarm_filesystem_opened_file_list(struct gfarm_filesystem *fs)
{
	return (fs->file_list);
}

int
gfarm_filesystem_failover_detected(struct gfarm_filesystem *fs)
{
	return (fs != NULL ? fs->failover_detected : 0);
}

void
gfarm_filesystem_set_failover_detected(struct gfarm_filesystem *fs,
	int detected)
{
	fs->failover_detected = detected;
}

int
gfarm_filesystem_failover_count(struct gfarm_filesystem *fs)
{
	return (fs != NULL ? fs->failover_count : 0);
}

void
gfarm_filesystem_set_failover_count(struct gfarm_filesystem *fs, int count)
{
	fs->failover_count = count;
}

int
gfarm_filesystem_in_failover_process(struct gfarm_filesystem *fs)
{
	return (fs != NULL ? fs->in_failover_process : 0);
}

void
gfarm_filesystem_set_in_failover_process(struct gfarm_filesystem *fs, int b)
{
	fs->in_failover_process = b;
}

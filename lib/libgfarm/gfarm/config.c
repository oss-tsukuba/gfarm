/*
 * $Id$
 */

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <assert.h>

#include <sys/socket.h>
#include <netinet/in.h> /* ntohs */
#include <netdb.h>

#include <time.h>
#include <pwd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "lru_cache.h"
#include "msgdigest.h"

#include "context.h"
#include "liberror.h"
#include "patmatch.h"
#include "hostspec.h"
#if 0 /* not yet in gfarm v2 */
#include "param.h"
#endif
#include "sockopt.h"
#include "host.h" /* XXX address_use is disabled for now */
#include "auth.h"
#include "gfpath.h"
#define GFARM_USE_STDIO
#include "config.h"
#include "gfm_proto.h" /* GFMD_DEFAULT_PORT */
#include "gfs_proto.h" /* GFSD_DEFAULT_PORT */
#include "gfs_profile.h"
#include "gfm_client.h"
#include "lookup.h"
#include "metadb_server.h"
#include "filesystem.h"
#include "conn_hash.h"
#include "conn_cache.h"
#include "humanize_number.h"
#include "gfs_rdma.h"	/* GFARM_RDMA_REG_MR_DEFAULT */

#ifdef SOMAXCONN
#define LISTEN_BACKLOG_DEFAULT	SOMAXCONN
#else
#define LISTEN_BACKLOG_DEFAULT	5
#endif

#define staticp	(gfarm_ctxp->config_static)

#define MAX_CONFIG_LINE_LENGTH	1023

struct gfarm_config_static {
	char *config_file;

	/* security */
	char *shared_key_file;

	/* xattr cache handling */
	gfarm_stringlist xattr_cache_list;

	/* Gfarm username handling */
	struct gfarm_hash_table *local_ug_maps_tab;

	/* client side variables */
	char *local_username;
	char *local_homedir;

	/* static configuration variables */
	int log_message_verbose;
	gfarm_int64_t minimum_free_disk_space;
	char **debug_command_argv;
	char *argv0;
};

gfarm_error_t
gfarm_config_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_config_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->config_file = GFARM_CONFIG;
	s->shared_key_file = NULL;
	gfarm_stringlist_init(&s->xattr_cache_list);
	s->local_ug_maps_tab = NULL;
	s->local_username = NULL;
	s->local_homedir = NULL;
	s->log_message_verbose = GFARM_CONFIG_MISC_DEFAULT;
	s->minimum_free_disk_space = GFARM_CONFIG_MISC_DEFAULT;
	s->debug_command_argv = NULL;
	s->argv0 = NULL;

	ctxp->config_static = s;
	return (GFARM_ERR_NO_ERROR);
}

static void local_ug_maps_tab_free(void);
static void debug_command_argv_free(void);

void
gfarm_config_static_term(struct gfarm_context *ctxp)
{
	struct gfarm_config_static *s = ctxp->config_static;

	if (s == NULL)
		return;

	free(s->shared_key_file);
	gfarm_stringlist_free_deeply(&s->xattr_cache_list);
	local_ug_maps_tab_free();
	free(s->local_username);
	free(s->local_homedir);
	debug_command_argv_free();
	free(s->argv0);
	free(s);
}

void
gfarm_config_set_filename(char *filename)
{
	staticp->config_file = filename;
}

char *
gfarm_config_get_filename(void)
{
	return (staticp->config_file);
}

const char *
gfarm_version(void)
{
	const static char ver[] = PACKAGE_VERSION;

	return (ver);
}

static int
version_to_int(const char *c)
{
	int ver = 0;

	for (; isdigit(*(unsigned char *)c); ++c)
		ver = ver * 10 + (*c - '0');
	return (ver);
}

static const char *
skip_version(const char *c)
{
	while (*c && *c != '.')
		++c;
	if (*c == '.')
		++c;
	return (c);
}

int
gfarm_version_major(void)
{
	const char *v = gfarm_version();
	static int major = 0;

	if (major == 0)
		major = version_to_int(v);
	return (major);
}

int
gfarm_version_minor(void)
{
	const char *v = gfarm_version();
	static int minor = 0;

	if (minor == 0) {
		v = skip_version(v);
		minor = version_to_int(v);
	}
	return (minor);
}

int
gfarm_version_teeny(void)
{
	const char *v = gfarm_version();
	static int teeny = 0;

	if (teeny == 0) {
		v = skip_version(v);
		v = skip_version(v);
		teeny = version_to_int(v);
	}
	return (teeny);
}

/* XXX move actual function definition here */
static gfarm_error_t gfarm_strtoken(char **, char **);

/*
 * NOTE:
 * client host should call gfs_stat_cache_clear() after
 * calling this gfarm_xattr_caching_pattern_add() function,
 * otherwise unexpected GFARM_ERR_NO_SUCH_OBJECT may happen.
 *
 * The reason we don't call gfs_stat_cache_clear() automatically is
 * because it's not appropriate for gfmd.
 */
gfarm_error_t
gfarm_xattr_caching_pattern_add(const char *attr_pattern)
{
	gfarm_error_t e;
	char *pat = strdup(attr_pattern);

	if (pat == NULL) {
		gflog_debug(GFARM_MSG_1002446,
		    "failed to allocate an attr_pattern \"%s\": no memory",
		    attr_pattern);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_stringlist_add(&staticp->xattr_cache_list, pat);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002447,
		    "failed to allocate record an attr_pattern \"%s\": "
		    "no memory", attr_pattern);
		free(pat);
	}
	return (e);
}

int
gfarm_xattr_caching(const char *attrname)
{
	gfarm_stringlist cache_list = staticp->xattr_cache_list;
	int i, n = gfarm_stringlist_length(&cache_list);
	const char *pattern;

	for (i = 0; i < n; i++) {
		pattern = gfarm_stringlist_elem(&cache_list, i);
		if (gfarm_pattern_match(pattern, attrname, 0))
			return (1);
	}
	return (0);
}

int
gfarm_xattr_caching_patterns_number(void)
{
	return (gfarm_stringlist_length(&staticp->xattr_cache_list));
}

char**
gfarm_xattr_caching_patterns(void)
{
	return (GFARM_STRINGLIST_STRARRAY(staticp->xattr_cache_list));
}

/*
 * GFarm username handling
 */

struct gfarm_local_ug_maps_id {
	char *hostname;
	int port;
};

struct gfarm_local_ug_maps {
	gfarm_stringlist local_user_map_file_list;
	gfarm_stringlist local_group_map_file_list;
};

#define LOCAL_UG_MAP_FILE_HASHTAB_SIZE 31

static int
local_ug_maps_hash_index(const void *key, int keylen)
{
	const struct gfarm_local_ug_maps_id *id = key;

	return (gfarm_hash_casefold(id->hostname, strlen(id->hostname)) +
	    id->port * 3);
}

static int
local_ug_maps_hash_equal(const void *key1, int key1len,
	const void *key2, int key2len)
{
	const struct gfarm_local_ug_maps_id *id1 = key1, *id2 = key2;

	return (strcasecmp(id1->hostname, id2->hostname) == 0 &&
	    id1->port == id2->port);
}

#define DEFAULT_HOSTNAME_KEY	"."
#define DEFAULT_PORT_KEY	(-1)

void (*gfarm_ug_maps_notify)(const char *, int , int , const char *);

static gfarm_error_t
local_ug_maps_enter(const char *hostname, int port, int is_user,
	const char *map_file)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct gfarm_local_ug_maps *ugm;
	struct gfarm_local_ug_maps_id id, *idp = NULL;
	char *s = NULL;
	int created;

	if (staticp->local_ug_maps_tab == NULL) {
		staticp->local_ug_maps_tab = gfarm_hash_table_alloc(
		    LOCAL_UG_MAP_FILE_HASHTAB_SIZE,
		    local_ug_maps_hash_index, local_ug_maps_hash_equal);
		if (staticp->local_ug_maps_tab == NULL) {
			gflog_debug(GFARM_MSG_1002524,
			    "allocation of hashtable failed: %s",
			    gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
	}

	if (hostname == NULL) {
		hostname = DEFAULT_HOSTNAME_KEY;
		port = DEFAULT_PORT_KEY;
	}
	id.hostname = (char *)hostname; /* UNCONST */
	id.port = port;
	entry = gfarm_hash_enter(staticp->local_ug_maps_tab, &id, sizeof(id),
	    sizeof(*ugm), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1002525,
		    "insertion to hashtable failed: %s",
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	if (created) {
		idp = gfarm_hash_entry_key(entry);
		idp->hostname = strdup(hostname);
		if (idp->hostname == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002526,
			    "strdup failed: %s",
			    gfarm_error_string(e));
			goto error;
		}
		ugm = gfarm_hash_entry_data(entry);
		gfarm_stringlist_init(&ugm->local_user_map_file_list);
		gfarm_stringlist_init(&ugm->local_group_map_file_list);
	} else {
		ugm = gfarm_hash_entry_data(entry);
	}
	s = strdup(map_file);
	if (s == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002527,
		    "strdup failed: %s",
		    gfarm_error_string(e));
		goto error;
	}
	if (is_user) {
		if ((e = gfarm_stringlist_add(&ugm->local_user_map_file_list,
		    s)) != GFARM_ERR_NO_ERROR) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002528,
			    "gfarm_stringlist_add failed: %s",
			    gfarm_error_string(e));
			goto error;
		}
	} else {
		if ((e = gfarm_stringlist_add(&ugm->local_group_map_file_list,
		    s)) != GFARM_ERR_NO_ERROR) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002529,
			    "gfarm_stringlist_add failed: %s",
			    gfarm_error_string(e));
			goto error;
		}
	}
	/* for gfskd, linux helper daemon */
	if (gfarm_ug_maps_notify) {
		gfarm_ug_maps_notify(hostname, port, is_user, s);
	}

	return (GFARM_ERR_NO_ERROR);
error:
	if (created) {
		if (idp)
			free(idp->hostname);
		gfarm_hash_purge(staticp->local_ug_maps_tab, &id, sizeof(id));
	}
	free(s);
	return (e);
}

static void
local_ug_maps_tab_free(void)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct gfarm_local_ug_maps_id *idp;
	struct gfarm_local_ug_maps *ugm;

	if (staticp->local_ug_maps_tab == NULL)
		return;

	for (gfarm_hash_iterator_begin(staticp->local_ug_maps_tab, &it);
	     !gfarm_hash_iterator_is_end(&it);) {
		entry = gfarm_hash_iterator_access(&it);
		idp = gfarm_hash_entry_key(entry);
		free(idp->hostname);
		ugm = gfarm_hash_entry_data(entry);
		gfarm_stringlist_free_deeply(&ugm->local_user_map_file_list);
		gfarm_stringlist_free_deeply(&ugm->local_group_map_file_list);
		gfarm_hash_iterator_purge(&it);
	}
	gfarm_hash_table_free(staticp->local_ug_maps_tab);
}

static struct gfarm_local_ug_maps *
local_ug_maps_lookup(const char *hostname, int port)
{
	struct gfarm_hash_entry *entry = NULL;
	struct gfarm_local_ug_maps_id id;
	struct gfarm_hash_table *map = staticp->local_ug_maps_tab;

	if (map == NULL)
		return (NULL);
	if (hostname && port >= 0) {
		id.hostname = (char *)hostname; /* UNCONST */
		id.port = port;
		entry = gfarm_hash_lookup(map, &id, sizeof(id));
	}
	if (entry == NULL) {
		id.hostname = DEFAULT_HOSTNAME_KEY;
		id.port = DEFAULT_PORT_KEY;
		entry = gfarm_hash_lookup(map, &id, sizeof(id));
		if (entry == NULL)
			return (NULL);
	}
	return ((struct gfarm_local_ug_maps *)gfarm_hash_entry_data(entry));
}

#define LOCAL_USER_MAP_FILE_LIST(ugm, hostname, port) \
	((ugm = local_ug_maps_lookup((hostname), (port))) ? \
	&ugm->local_user_map_file_list : NULL)

#define LOCAL_GROUP_MAP_FILE_LIST(ugm, hostname, port) \
	((ugm = local_ug_maps_lookup((hostname), (port))) ? \
	&ugm->local_group_map_file_list : NULL)

/* the return value of the following function should be free(3)ed */
static gfarm_error_t
map_user(gfarm_stringlist *map_file_list, const char *from, char **to_p,
	const char *(*mapping)(const char *, const char *, const char *),
	gfarm_error_t error_redefined)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	FILE *map = NULL;
	char *mapfile = NULL;
	int i, list_len, mapfile_mapped_index;
	char buffer[MAX_CONFIG_LINE_LENGTH + 1], *g_user, *l_user, *tmp;
	const char *mapped;
	int lineno = 0;

	*to_p = NULL;
	if (map_file_list == NULL)
		goto search_end;
	list_len = gfarm_stringlist_length(map_file_list);
	mapfile_mapped_index = list_len;
	for (i = 0; i < list_len; i++) {
		mapfile = gfarm_stringlist_elem(map_file_list, i);
		if ((map = fopen(mapfile, "r")) == NULL) {
			gflog_error(GFARM_MSG_1000009,
			    "%s: cannot open: %s", mapfile, strerror(errno));
			return (GFARM_ERR_CANT_OPEN);
		}
		lineno = 0;
		while (fgets(buffer, sizeof buffer, map) != NULL) {
			char *bp = buffer;

			lineno++;
			e = gfarm_strtoken(&bp, &g_user);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (g_user == NULL) /* blank or comment line */
				continue;
			e = gfarm_strtoken(&bp, &l_user);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (l_user == NULL) {
				e = GFARM_ERRMSG_MISSING_LOCAL_USER;
				goto finish;
			}
			mapped = (*mapping)(from, g_user, l_user);
			if (mapped != NULL) {
				if (*to_p != NULL &&
				    strcmp(mapped, *to_p) != 0 &&
				    i == mapfile_mapped_index) {
					e = error_redefined;
					goto finish;
				}
				if (*to_p == NULL) {
					*to_p = strdup(mapped);
					if (*to_p == NULL) {
						e = GFARM_ERR_NO_MEMORY;
						goto finish;
					}
				}
				mapfile_mapped_index = i;
			}
			e = gfarm_strtoken(&bp, &tmp);
			if (e != GFARM_ERR_NO_ERROR)
				goto finish;
			if (tmp != NULL) {
				e = GFARM_ERRMSG_TOO_MANY_ARGUMENTS;
				goto finish;
			}
		}
		fclose(map);
		map = NULL;
	}
search_end:
	if (*to_p == NULL) { /* not found */
		*to_p = strdup(from);
		if (*to_p == NULL)
			e = GFARM_ERR_NO_MEMORY;
	}
finish:
	if (map != NULL)
		fclose(map);
	if (e != GFARM_ERR_NO_ERROR) {
		free(*to_p);
		gflog_error(GFARM_MSG_1000010,
		    "%s line %d: %s", mapfile, lineno,
		    gfarm_error_string(e));
	}
	return (e);
}

static const char *
map_global_to_local(const char *from, const char *global_user,
	const char *local_user)
{
	if (strcmp(from, global_user) == 0)
		return (local_user);
	return (NULL);
}

/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_global_to_local_username_by_host(const char *hostname, int port,
	const char *global_user, char **local_user_p)
{
	struct gfarm_local_ug_maps *ugm;
	return (map_user(LOCAL_USER_MAP_FILE_LIST(ugm, hostname, port),
	    global_user, local_user_p, map_global_to_local,
	    GFARM_ERRMSG_GLOBAL_USER_REDEFIEND));
}

gfarm_error_t
gfarm_global_to_local_username_by_url(const char *url,
	const char *global_user, char **local_user_p)
{
	gfarm_error_t e;
	char *hostname;
	int port;

	if ((e = gfarm_get_hostname_by_url(url, &hostname, &port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002530,
		    "gfarm_get_hostname_by_url(%s) failed: %s",
		    url, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_global_to_local_username_by_host(hostname, port,
	    global_user, local_user_p)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002531,
		    "gfarm_global_to_local_username_by_host(%s,%d) (%s)"
		    " failed: %s",
		    hostname, port, url, gfarm_error_string(e));
	}
	free(hostname);
	return (e);
}

static const char *
map_local_to_global(const char *from, const char *global_user,
	const char *local_user)
{
	if (strcmp(from, local_user) == 0)
		return (global_user);
	return (NULL);
}

/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_local_to_global_username_by_host(const char *hostname, int port,
	const char *local_user, char **global_user_p)
{
	struct gfarm_local_ug_maps *ugm;
	return (map_user(LOCAL_USER_MAP_FILE_LIST(ugm, hostname, port),
	    local_user, global_user_p, map_local_to_global,
	    GFARM_ERRMSG_LOCAL_USER_REDEFIEND));
}

gfarm_error_t
gfarm_local_to_global_username_by_url(const char *url,
	const char *local_user, char **global_user_p)
{
	gfarm_error_t e;
	char *hostname;
	int port;

	if ((e = gfarm_get_hostname_by_url(url, &hostname, &port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002532,
		    "gfarm_get_hostname_by_url(%s) failed: %s",
		    url, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_local_to_global_username_by_host(hostname, port,
	    local_user, global_user_p)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002533,
		    "gfarm_local_to_global_username_by_host(%s,%d) (%s)"
		    " failed: %s",
		    hostname, port, url, gfarm_error_string(e));
	}
	free(hostname);
	return (e);
}

/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_global_to_local_groupname_by_host(const char *hostname, int port,
	const char *global_group, char **local_group_p)
{
	struct gfarm_local_ug_maps *ugm;
	return (map_user(LOCAL_GROUP_MAP_FILE_LIST(ugm, hostname, port),
	    global_group, local_group_p, map_global_to_local,
	    GFARM_ERRMSG_GLOBAL_GROUP_REDEFIEND));
}

gfarm_error_t
gfarm_global_to_local_groupname_by_url(const char *url,
	const char *global_group, char **local_group_p)
{
	gfarm_error_t e;
	char *hostname;
	int port;

	if ((e = gfarm_get_hostname_by_url(url, &hostname, &port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002534,
		    "gfarm_get_hostname_by_url(%s) failed: %s",
		    url, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_global_to_local_groupname_by_host(hostname, port,
	    global_group, local_group_p)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002535,
		    "gfarm_global_to_local_groupname_by_host(%s,%d) (%s)"
		    " failed: %s",
		    hostname, port, url, gfarm_error_string(e));
	}
	free(hostname);
	return (e);
}

/* the return value of the following function should be free(3)ed */
gfarm_error_t
gfarm_local_to_global_groupname_by_host(const char *hostname, int port,
	const char *local_group, char **global_group_p)
{
	struct gfarm_local_ug_maps *ugm;
	return (map_user(LOCAL_GROUP_MAP_FILE_LIST(ugm, hostname, port),
	    local_group, global_group_p, map_local_to_global,
	    GFARM_ERRMSG_LOCAL_GROUP_REDEFIEND));
}

gfarm_error_t
gfarm_local_to_global_groupname_by_url(const char *url,
	const char *local_group, char **global_group_p)
{
	gfarm_error_t e;
	char *hostname;
	int port;

	if ((e = gfarm_get_hostname_by_url(url, &hostname, &port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002536,
		    "gfarm_get_hostname_by_url(%s) failed: %s",
		    url, gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_local_to_global_groupname_by_host(hostname, port,
	    local_group, global_group_p)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002537,
		    "gfarm_local_to_global_groupname_by_host(%s,%d) (%s)"
		    " failed: %s",
		    hostname, port, url, gfarm_error_string(e));
	}
	free(hostname);
	return (e);
}

static gfarm_error_t
set_string(char **var, char *value)
{
	if (*var != NULL)
		free(*var);
	*var = strdup(value);
	if (*var == NULL) {
		gflog_debug(GFARM_MSG_1000918,
			"allocation of memory failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_get_global_username_by_url(const char *url, char **userp)
{
	gfarm_error_t e;
	char *hostname;
	int port;

	if ((e = gfarm_get_hostname_by_url(url, &hostname, &port))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfarm_get_global_username_by_host(hostname, port, userp);
	free(hostname);
	return (e);
}

gfarm_error_t
gfarm_get_global_username_by_host_for_connection_cache(
	const char *hostname, int port, char **userp)
{
	char *local_user = gfarm_get_local_username();
	char *global_user;
	gfarm_error_t e;

	if (userp == NULL)
		return (GFARM_ERR_NO_ERROR);
	e = gfarm_local_to_global_username_by_host(hostname, port, local_user,
	    &global_user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003440,
		    "local-to-global user-mapping failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	*userp = global_user;
	return (e);
}

gfarm_error_t
gfarm_get_global_username_by_host(const char *hostname, int port, char **userp)
{
	char *global_user;
	gfarm_error_t e;
#ifdef HAVE_GSI
	struct gfm_connection *gfm_server;
	const char *user;
#endif

	if (userp == NULL)
		return (GFARM_ERR_NO_ERROR);
	e = gfarm_get_global_username_by_host_for_connection_cache(
		hostname, port, &global_user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003441,
		    "gfarm_get_global_username_by_host_for_connection_cache() "
		    "failed: %s", gfarm_error_string(e));
		return (e);
	}
#ifdef HAVE_GSI
	/* global username can be specified by GSI DN in gfmd user database */
	e = gfm_client_connection_and_process_acquire(hostname, port,
	    global_user, &gfm_server);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003442, "cannot acquire connection: %s",
		    gfarm_error_string(e));
		free(global_user);
		return (e);
	}
	if (GFARM_IS_AUTH_GSI(
	    gfm_client_connection_auth_method(gfm_server))) {
		user = gfm_client_username(gfm_server);
		if (user == NULL) {
			gflog_error(GFARM_MSG_1003443,
			    "global username is not set");
			e = GFARM_ERR_NO_SUCH_USER;
		} else {
			free(global_user);
			global_user = strdup(user);
			if (global_user == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				gflog_error(GFARM_MSG_1003444,
				    "%s", gfarm_error_string(e));
			}
		}
	}
	gfm_client_connection_free(gfm_server);
#endif
	if (e == GFARM_ERR_NO_ERROR)
		*userp = global_user;
	else
		free(global_user);
	return (e);
}

gfarm_error_t
gfarm_set_local_username(char *local_username)
{
	return (set_string(&staticp->local_username, local_username));
}

#ifndef __KERNEL__	/* gfarm_get_local_username :: multi user */
char *
gfarm_get_local_username(void)
{
	return (staticp->local_username);
}
#endif /* __KERNEL__ */

gfarm_error_t
gfarm_set_local_homedir(char *local_homedir)
{
	return (set_string(&staticp->local_homedir, local_homedir));
}

char *
gfarm_get_local_homedir(void)
{
	return (staticp->local_homedir);
}

/*
 * We should not trust gfarm_get_*() values as a result of this function
 * (because it may be forged).
 */
gfarm_error_t
gfarm_set_local_user_for_this_local_account(void)
{
	return (gfarm_set_local_user_for_this_uid(geteuid()));
}

gfarm_error_t
gfarm_set_local_user_for_this_uid(uid_t uid)
{
	gfarm_error_t error;
	struct passwd pwbuf, *pwd;
	char *buf;

	GFARM_MALLOC_ARRAY(buf, gfarm_ctxp->getpw_r_bufsz);
	if (buf == NULL) {
		error = GFARM_ERR_NO_MEMORY;
		gflog_error(GFARM_MSG_1000011, "gfarm_set_local_user: %s",
			gfarm_error_string(error));
		return (error);
	}
	if (getpwuid_r(uid, &pwbuf, buf, gfarm_ctxp->getpw_r_bufsz,
	    &pwd) != 0 || pwd == NULL) {
		gflog_error(GFARM_MSG_1000012, "local account doesn't exist");
		error = GFARM_ERR_NO_SUCH_OBJECT;
		goto error;
	}
	error = gfarm_set_local_username(pwd->pw_name);
	if (error == GFARM_ERR_NO_ERROR)
		error = gfarm_set_local_homedir(pwd->pw_dir);
 error:
	free(buf);
	return (error);
}

/*
 * GFarm Configurations.
 *
 * Initial string values should be NULL, otherwise the value incorrectly
 * free(3)ed in the gfarm_config_clear() function below.
 * If you would like to provide default value other than NULL, set the
 * value at gfarm_config_set_default*().
 */
/* GFS dependent */
#define GFARM_SPOOL_CHECK_PARALLEL_DEFAULT	\
	GFARM_SPOOL_CHECK_PARALLEL_AUTOMATIC
#define GFARM_SPOOL_CHECK_PARALLEL_MAX_DEFAULT	64
#define GFARM_SPOOL_CHECK_PARALLEL_STEP_DEFAULT	1
#define GFARM_SPOOL_CHECK_PARALLEL_PER_CAPACITY_DEFAULT \
	(64LL*1024*1024*1024*1024) /* one process per 64TB */
#define GFARM_SPOOL_BASE_LOAD_DEFAULT	0.0F
#define GFARM_SPOOL_DIGEST_ERROR_CHECK_DEFAULT	1 /* enable */
#define GFARM_SPOOL_SERVER_READ_ONLY_RETRY_INTERVAL_DEFAULT 60 /* second */
#define GFARM_WRITE_VERIFY_DEFAULT 0 /* disable */
#define GFARM_WRITE_VERIFY_INTERVAL_DEFAULT 21600 /* seconds (6 hours) */
#define GFARM_WRITE_VERIFY_RETRY_INTERVAL_DEFAULT 600 /* 600 seconds (10min) */
#define GFARM_WRITE_VERIFY_LOG_INTERVAL_DEFAULT 3600 /* 3600 seconds (1hour) */

int gfarm_spool_server_listen_backlog = GFARM_CONFIG_MISC_DEFAULT;
char *gfarm_spool_server_listen_address = NULL;
int gfarm_spool_server_back_channel_rcvbuf_limit = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_spool_server_read_only_retry_interval = GFARM_CONFIG_MISC_DEFAULT;
char *gfarm_spool_root[GFARM_SPOOL_ROOT_NUM];
static struct {
	enum gfarm_spool_check_level level;
	const char *name;
} gfarm_spool_check_levels[] = {
	{ GFARM_SPOOL_CHECK_LEVEL_DISABLE, "disable" },
	{ GFARM_SPOOL_CHECK_LEVEL_DISPLAY, "display" },
	{ GFARM_SPOOL_CHECK_LEVEL_DELETE, "delete" },
	{ GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND, "lost_found" }
};
static enum gfarm_spool_check_level gfarm_spool_check_level =
	GFARM_SPOOL_CHECK_LEVEL_DEFAULT;
static const char *gfarm_spool_check_level_name = NULL;
int gfarm_spool_check_parallel = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_spool_check_parallel_max = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_spool_check_parallel_step = GFARM_CONFIG_MISC_DEFAULT;
gfarm_off_t gfarm_spool_check_parallel_per_capacity =
    GFARM_CONFIG_MISC_DEFAULT;
float gfarm_spool_base_load = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_spool_digest_error_check = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_write_verify = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_write_verify_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_write_verify_retry_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_write_verify_log_interval = GFARM_CONFIG_MISC_DEFAULT;

/* GFM dependent */

enum gfarm_backend_db_type gfarm_backend_db_type = GFARM_BACKEND_DB_TYPE_NONE;
enum gfarm_db_access_type gfarm_db_access_type = GFARM_DB_ACCESS_TYPE_DBQ;
int gfarm_metadb_server_listen_backlog = GFARM_CONFIG_MISC_DEFAULT;

static struct {
	enum gfarm_atime_type type;
	const char *name;
} gfarm_atime_types[] = {
	{ GFARM_ATIME_DISABLE, "disable" },
	{ GFARM_ATIME_RELATIVE, "relative" },
	{ GFARM_ATIME_STRICT, "strict" }
};
static enum gfarm_atime_type gfarm_atime_type = GFARM_ATIME_DEFAULT;
static const char *gfarm_atime_type_name = NULL;
#define GFARM_MAX_OPEN_FILES_DEFAULT	1024
int gfarm_max_open_files = GFARM_CONFIG_MISC_DEFAULT;

/* LDAP dependent */
char *gfarm_ldap_server_name = NULL;
char *gfarm_ldap_server_port = NULL;
char *gfarm_ldap_base_dn = NULL;
char *gfarm_ldap_bind_dn = NULL;
char *gfarm_ldap_bind_password = NULL;
char *gfarm_ldap_tls = NULL;
char *gfarm_ldap_tls_cipher_suite = NULL;
char *gfarm_ldap_tls_certificate_key_file = NULL;
char *gfarm_ldap_tls_certificate_file = NULL;

/* PostgreSQL dependent */
char *gfarm_postgresql_server_name = NULL;
char *gfarm_postgresql_server_port = NULL;
char *gfarm_postgresql_dbname = NULL;
char *gfarm_postgresql_user = NULL;
char *gfarm_postgresql_password = NULL;
char *gfarm_postgresql_conninfo = NULL;

/* LocalFS dependent */
char *gfarm_localfs_datadir = NULL;

/* IO statistics */
char *gfarm_iostat_gfmd_path;
char *gfarm_iostat_gfsd_path;
int gfarm_iostat_max_client = GFARM_CONFIG_MISC_DEFAULT;
#define GFARM_IOSTAT_MAX_CLIENT 1024

/* miscellaneous */
#define GFARM_CONFIG_INCLUDE_NESTING_LIMIT_DEFAULT	20
#define GFARM_LOG_MESSAGE_VERBOSE_DEFAULT	0
#define GFARM_NO_FILE_SYSTEM_NODE_TIMEOUT_DEFAULT 30 /* 30 seconds */

/* 35 == 10*3 (failure of primary, secondary, tertiary nameserver) + 5 (RTT) */
#define GFARM_GFMD_AUTHENTICATION_TIMEOUT_DEFAULT 35 /* 35 seconds */

#define GFARM_GFMD_RECONNECTION_TIMEOUT_DEFAULT 30 /* 30 seconds */
#define GFARM_GFSD_CONNECTION_TIMEOUT_DEFAULT 30 /* 30 seconds */
#define GFARM_ATTR_CACHE_LIMIT_DEFAULT		40000 /* 40,000 entries */
#define GFARM_ATTR_CACHE_TIMEOUT_DEFAULT	1000 /* 1,000 milli second */
#define GFARM_PAGE_CACHE_TIMEOUT_DEFAULT	1000 /* 1,000 milli second */

/* same with GFARM_GFMD_AUTHENTICATION_TIMEOUT_DEFAULT */
#define GFARM_SCHEDULE_RPC_TIMEOUT_DEFAULT 35 /* 35 seconds */

#define GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT 600 /* 10 minutes */
#define GFARM_SCHEDULE_CONCURRENCY_DEFAULT	10
#define GFARM_SCHEDULE_CONCURRENCY_PER_NET_DEFAULT	3
#define GFARM_SCHEDULE_IDLE_LOAD_DEFAULT	100  /* 0.1 * F2LL_SCALE */
#define GFARM_SCHEDULE_BUSY_LOAD_DEFAULT	500  /* 0.5 * F2LL_SCALE */
#define GFARM_SCHEDULE_VIRTUAL_LOAD_DEFAULT	300  /* 0.3 * F2LL_SCALE */
#define GFARM_SCHEDULE_CANDIDATES_RATIO_DEFAULT	4000 /* 4.0 * F2LL_SCALE */
#define GFARM_SCHEDULE_RTT_THRESH_RATIO_DEFAULT	4000 /* 4.0 * F2LL_SCALE */
#define GFARM_SCHEDULE_RTT_THRESH_DIFF_DEFAULT	1000 /* 1000 micro second */
#define GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT 1 /* enable */
#define GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT	(512 * 1024 * 1024) /* 512MB */
#define GFARM_DIRECT_LOCAL_ACCESS_DEFAULT	1 /* enable */
#define GFARM_REPLICATION_AT_WRITE_OPEN_DEFAULT	1 /* enable */
#define GFARM_READ_ONLY_DEFAULT 0 /* disable */
#define GFARM_SIMULTANEOUS_REPLICATION_RECEIVERS_DEFAULT	20
#define GFARM_REPLICATION_BUSY_HOST_DEFAULT	1
#define GFARM_GFSD_CONNECTION_CACHE_DEFAULT	256 /* 256 free connections */
#define GFARM_GFMD_CONNECTION_CACHE_DEFAULT	8   /*   8 free connections */
#define GFARM_DIRECTORY_QUOTA_COUNT_PER_USER_LIMIT_DEFAULT	100
#define GFARM_DIRECTORY_QUOTA_CHECK_START_DELAY_DEFAULT	60	/*seconds*/
#define GFARM_DIRECTORY_QUOTA_CHECK_RETRY_INTERVAL_DEFAULT 60	/*seconds*/
#define GFARM_QUOTA_CHECK_RETRY_INTERVAL_DEFAULT 60	/*seconds*/
#define GFARM_MAX_DIRECTORY_DEPTH_DEFAULT	100
#define GFARM_MAX_DIRECTORY_DEPTH_MINIMUM	16
#define GFARM_MAX_DIRECTORY_DEPTH_MAXIMUM	65536
#define GFARM_METADB_MAX_DESCRIPTORS_DEFAULT	(2*65536)
#define GFARM_METADB_REPLICA_REMOVER_BY_HOST_SLEEP_TIME_DEFAULT	20000000
							/* nanosec. */
#define GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_UNLIMITED	0 /* no limit */
#define GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_DEFAULT	4096
#define GFARM_METADB_REPLICA_REMOVER_BY_HOST_INODE_STEP_DEFAULT	1024
#define GFARM_CLIENT_DIGEST_CHECK_DEFAULT	0
#define GFARM_CLIENT_FILE_BUFSIZE_DEFAULT	(1024 * 1024)
#define GFARM_CLIENT_PARALLEL_COPY_DEFAULT	4
#define GFARM_CLIENT_PARALLEL_MAX_DEFAULT	16
#define GFARM_PROFILE_DEFAULT 0 /* disable */
#define GFARM_METADB_REPLICATION_ENABLED_DEFAULT	0
#define GFARM_JOURNAL_MAX_SIZE_DEFAULT		(32 * 1024 * 1024) /* 32MB */
#define GFARM_JOURNAL_RECVQ_SIZE_DEFAULT	100000
#define GFARM_JOURNAL_SYNC_FILE_DEFAULT		1
#define GFARM_JOURNAL_SYNC_SLAVE_TIMEOUT_DEFAULT 10 /* 10 second */
#define GFARM_METADB_SERVER_SLAVE_REPLICATION_TIMEOUT_DEFAULT 120 /* 120 sec */
#define GFARM_METADB_SERVER_SLAVE_MAX_SIZE_DEFAULT	16
#define GFARM_METADB_SERVER_FORCE_SLAVE_DEFAULT		0
#define GFARM_METADB_SERVER_NFS_ROOT_SQUASH_SUPPORT_DEFAULT	1 /* enable */
#define GFARM_METADB_SERVER_LONG_TERM_LOCK_TYPE_DEFAULT	\
	GFARM_LOCK_TYPE_TICKETLOCK
#define GFARM_NETWORK_RECEIVE_TIMEOUT_DEFAULT	60 /* 60 seconds */
#define GFARM_NETWORK_SEND_TIMEOUT_DEFAULT	0 /* seconds (disabled) */
#define GFARM_FILE_TRACE_DEFAULT 0 /* disable */
#define GFARM_FATAL_ACTION_DEFAULT GFLOG_FATAL_ACTION_ABORT_BACKTRACE
#ifdef HAVE_INFINIBAND
#define GFARM_IB_RDMA_DEFAULT 1 /* enable */
#else /* HAVE_INFINIBAND */
#define GFARM_IB_RDMA_DEFAULT 0 /* disable */
#endif /* HAVE_INFINIBAND */
#define GFARM_RDMA_REG_MR_DEFAULT GFARM_RDMA_REG_MR_STATIC
#define GFARM_REPLICA_CHECK_DEFAULT 1 /* enable */
#define GFARM_REPLICA_CHECK_REMOVE_DEFAULT 1 /* enable */
#define GFARM_REPLICA_CHECK_REMOVE_GRACE_USED_SPACE_RATIO_DEFAULT 0 /* 0 % */
#define GFARM_REPLICA_CHECK_REMOVE_GRACE_TIME_DEFAULT 0 /* 0 second */
#define GFARM_REPLICA_CHECK_REDUCED_LOG_DEFAULT 1 /* enable */
#define GFARM_REPLICA_CHECK_HOST_DOWN_THRESH_DEFAULT 10800 /* 3 hours */
#define GFARM_REPLICA_CHECK_SLEEP_TIME_DEFAULT 100000 /* nanosec. */
#define GFARM_REPLICA_CHECK_YIELD_TIME_DEFAULT 0 /* nanosec. (disabled) */
#define GFARM_REPLICA_CHECK_MINIMUM_INTERVAL_DEFAULT 10 /* 10 sec. */
#define GFARM_REPLICAINFO_ENABLED_DEFAULT	1 /* enable */

char *gfarm_digest = NULL;
int gfarm_read_only = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_simultaneous_replication_receivers = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replication_busy_host = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_xattr_size_limit = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_xmlattr_size_limit = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_directory_quota_count_per_user_limit = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_directory_quota_check_start_delay = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_directory_quota_check_retry_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_quota_check_retry_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_max_directory_depth = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_version_major = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_version_minor = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_version_teeny = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_max_descriptors = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_stack_size = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_thread_pool_size = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_job_queue_length = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_remover_queue_length = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_remove_scan_log_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_remove_scan_interval_factor = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_heartbeat_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_failover_notify_delay = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_dbq_size = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_server_back_channel_sndbuf_limit = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_server_nfs_root_squash_support = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_server_long_term_lock_type = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_replica_remover_by_host_sleep_time =
	GFARM_CONFIG_MISC_DEFAULT;
int gfarm_metadb_replica_remover_by_host_inode_step =
	GFARM_CONFIG_MISC_DEFAULT;
static int metadb_replication_enabled = GFARM_CONFIG_MISC_DEFAULT;
static char *journal_dir = NULL;
static int journal_max_size = GFARM_CONFIG_MISC_DEFAULT;
static int journal_recvq_size = GFARM_CONFIG_MISC_DEFAULT;
static int journal_sync_file = GFARM_CONFIG_MISC_DEFAULT;
static int journal_sync_slave_timeout = GFARM_CONFIG_MISC_DEFAULT;
static int metadb_server_slave_replication_timeout = GFARM_CONFIG_MISC_DEFAULT;
static int metadb_server_slave_max_size = GFARM_CONFIG_MISC_DEFAULT;
static int metadb_server_force_slave = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_remove = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_remove_grace_used_space_ratio =
	GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_remove_grace_time = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_reduced_log = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_host_down_thresh = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_sleep_time = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_yield_time = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replica_check_minimum_interval = GFARM_CONFIG_MISC_DEFAULT;
int gfarm_replicainfo_enabled = GFARM_CONFIG_MISC_DEFAULT;

void
gfarm_config_clear(void)
{
	static char **vars[] = {
		&gfarm_spool_server_listen_address,
		&gfarm_ldap_server_name,
		&gfarm_ldap_server_port,
		&gfarm_ldap_base_dn,
		&gfarm_ldap_bind_dn,
		&gfarm_ldap_bind_password,
		&gfarm_ldap_tls,
		&gfarm_ldap_tls_cipher_suite,
		&gfarm_ldap_tls_certificate_key_file,
		&gfarm_ldap_tls_certificate_file,
		&gfarm_postgresql_server_name,
		&gfarm_postgresql_server_port,
		&gfarm_postgresql_dbname,
		&gfarm_postgresql_user,
		&gfarm_postgresql_password,
		&gfarm_postgresql_conninfo,
		&gfarm_localfs_datadir,
		&journal_dir,
	};
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(vars); i++) {
		if (*vars[i] != NULL) {
			free(*vars[i]);
			*vars[i] = NULL;
		}
	}
	for (i = 0; i < GFARM_SPOOL_ROOT_NUM; i++) {
		free(gfarm_spool_root[i]);
		gfarm_spool_root[i] = NULL;
	}
#if 0 /* XXX */
	config_read = gfarm_config_not_read;
#endif
}

static gfarm_error_t
set_backend_db_type(enum gfarm_backend_db_type set)
{
	if (gfarm_is_db_access_type_none() && set != GFARM_BACKEND_DB_TYPE_NONE)
		return (GFARM_ERRMSG_DB_ACCESS_TYPE_IS_NONE);
	if (gfarm_backend_db_type == set)
		return (GFARM_ERR_NO_ERROR);
	switch (gfarm_backend_db_type) {
	case GFARM_BACKEND_DB_TYPE_NONE:
		gfarm_backend_db_type = set;
		return (GFARM_ERR_NO_ERROR);
	case GFARM_BACKEND_DB_TYPE_LDAP:
		return (GFARM_ERRMSG_BACKEND_ALREADY_LDAP);
	case GFARM_BACKEND_DB_TYPE_POSTGRESQL:
		return (GFARM_ERRMSG_BACKEND_ALREADY_POSTGRESQL);
	case GFARM_BACKEND_DB_TYPE_LOCALFS:
		return (GFARM_ERRMSG_BACKEND_ALREADY_LOCALFS);
	default:
		assert(0);
		return (GFARM_ERR_UNKNOWN); /* workaround compiler warning */
	}
}

static gfarm_error_t
set_backend_db_type_ldap(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_LDAP));
}

static gfarm_error_t
set_backend_db_type_postgresql(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_POSTGRESQL));
}

static gfarm_error_t
set_backend_db_type_localfs(void)
{
	return (set_backend_db_type(GFARM_BACKEND_DB_TYPE_LOCALFS));
}

int
gfarm_schedule_write_local_priority(void)
{
	return (gfarm_ctxp->schedule_write_local_priority);
}

char *
gfarm_schedule_write_target_domain(void)
{
	return (gfarm_ctxp->schedule_write_target_domain);
}

gfarm_off_t
gfarm_get_minimum_free_disk_space(void)
{
	return (staticp->minimum_free_disk_space);
}

const char *
gfarm_config_get_argv0(void)
{
	return (staticp->argv0);
}

gfarm_error_t
gfarm_config_set_argv0(const char *argv0)
{
	free(staticp->argv0);
	staticp->argv0 = NULL;
	if (argv0 == NULL)
		return (GFARM_ERR_NO_ERROR);
	staticp->argv0 = strdup(argv0);
	if (staticp->argv0 == NULL) {
		gflog_debug(GFARM_MSG_1003409,
		    "failed to allocate argv0 \"%s\": no memory", argv0);
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

#ifndef __KERNEL__	/* gfarm_sig_debug :: not happen */
static char pid_string[] = "XXXXXXXX";

static char *
pid_to_string(long pid)
{
	char *pe = &pid_string[sizeof pid_string - 1];

	while (pid > 0 && pe > pid_string) {
		*--pe = pid % 10 + '0';
		pid /= 10;
	}
	return (pe);
}

/* signal handler */
static void
gfarm_sig_debug(int sig)
{
	static volatile sig_atomic_t already_called = 0;
	pid_t pid;
	const char *message;
	int status, rv;
	char **argv, **argvp;

	switch (sig) {
	case SIGQUIT:
		message = "caught SIGQUIT\n";
		break;
	case SIGILL:
		message = "caught SIGILL\n";
		break;
	case SIGTRAP:
		message = "caught SIGTRAP\n";
		break;
	case SIGFPE:
		message = "caught SIGFPE\n";
		break;
	case SIGBUS:
		message = "caught SIGBUS\n";
		break;
	case SIGSEGV:
		message = "caught SIGSEGV\n";
		break;
	default:
		message = "caught a signal\n";
		break;
	}
	/* ignore return value, since there is no other way here */
	rv = write(2, message, strlen(message));
	(void)rv;

	if (already_called)
		return;
	already_called = 1;

	argv = staticp->debug_command_argv;
	if (argv == NULL)
		_exit(1);

	/* replace '%p' with pid */
	for (argvp = argv; *argvp != NULL; ++argvp) {
		if ((*argvp)[0] == '%' && (*argvp)[1] == 'p' &&
		    (*argvp)[2] == '\0') {
			*argvp = pid_to_string(getpid());
			break;
		}
	}
	pid = fork();
	if (pid == -1) {
		char msg[] = "fork failed\n";

		/* ignore return value */
		rv = write(2, msg, strlen(msg));
		(void)rv;
		_exit(1);
	} else if (pid == 0) {
		execvp(argv[0], argv);
		perror(argv[0]); /* XXX dangerous to call from signal handler */
		_exit(1);
	} else {
		/* not really correct way to wait until attached, but... */
		sleep(5);
		waitpid(pid, &status, 0);
		_exit(1);
	}
}

void
gfarm_setup_debug_command(void)
{
	if (staticp->debug_command_argv == NULL)
		return;

	/*
	 * do not set gfarm_sig_debug for SIGABRT, since free() in
	 * glibc may abort when double free is detected, which causes
	 * a deadlock to execute fork() in gfarm_sig_debug signal
	 * handler.
	 */
	signal(SIGQUIT, gfarm_sig_debug);
	signal(SIGILL,  gfarm_sig_debug);
	signal(SIGTRAP, gfarm_sig_debug);
	signal(SIGFPE,  gfarm_sig_debug);
	signal(SIGBUS,  gfarm_sig_debug);
	signal(SIGSEGV, gfarm_sig_debug);
}
#endif /* __KERNEL__ */

int
gfarm_get_metadb_replication_enabled(void)
{
	return (metadb_replication_enabled ||
	    gfarm_db_access_type == GFARM_DB_ACCESS_TYPE_JOURNAL);
}

int
gfarm_is_db_access_type_none()
{
	return (gfarm_db_access_type == GFARM_DB_ACCESS_TYPE_NONE);
}

static gfarm_error_t
gfarm_set_db_access_type(enum gfarm_db_access_type type)
{
	gfarm_db_access_type = type;
	if (gfarm_is_db_access_type_none() &&
	    gfarm_backend_db_type != GFARM_BACKEND_DB_TYPE_NONE)
		return (GFARM_ERRMSG_DB_ACCESS_TYPE_IS_NONE);
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_set_metadb_replication_enabled(int enable)
{
	metadb_replication_enabled = enable;
	if (enable)
		gfarm_set_db_access_type(GFARM_DB_ACCESS_TYPE_JOURNAL);
	else
		gfarm_set_db_access_type(GFARM_DB_ACCESS_TYPE_DBQ);
}

const char *
gfarm_get_journal_dir(void)
{
	return (journal_dir);
}

int
gfarm_get_journal_max_size(void)
{
	return (journal_max_size);
}

int
gfarm_get_journal_recvq_size(void)
{
	return (journal_recvq_size);
}

int
gfarm_get_journal_sync_file(void)
{
	return (journal_sync_file);
}

int
gfarm_get_journal_sync_slave_timeout(void)
{
	return (journal_sync_slave_timeout);
}

int
gfarm_get_metadb_server_slave_replication_timeout(void)
{
	return (metadb_server_slave_replication_timeout);
}

int
gfarm_get_metadb_server_slave_max_size(void)
{
	return (metadb_server_slave_max_size);
}

int
gfarm_get_metadb_server_force_slave(void)
{
	return (metadb_server_force_slave);
}

void
gfarm_set_metadb_server_force_slave(int slave)
{
	metadb_server_force_slave = slave;
}

char *
gfarm_get_shared_key_file(void)
{
	return (staticp->shared_key_file);
}

int
gfarm_get_client_file_bufsize(void)
{
	return (gfarm_ctxp->client_file_bufsize);
}

void
gfarm_set_client_digest_check(int enable)
{
	gfarm_ctxp->client_digest_check = enable;
}

enum gfarm_spool_check_level
gfarm_spool_check_level_get(void)
{
	return (gfarm_spool_check_level);
}

const char *
gfarm_spool_check_level_get_by_name(void)
{
	return (gfarm_spool_check_level_name);
}

gfarm_error_t
gfarm_spool_check_level_set(enum gfarm_spool_check_level level)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_spool_check_levels); i++) {
		if (level == gfarm_spool_check_levels[i].level) {
			gfarm_spool_check_level = level;
			gfarm_spool_check_level_name
				= gfarm_spool_check_levels[i].name;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_INVALID_ARGUMENT);
}

gfarm_error_t
gfarm_spool_check_level_set_by_name(const char *name)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_spool_check_levels); i++) {
		if (strcmp(name, gfarm_spool_check_levels[i].name) == 0) {
			gfarm_spool_check_level
			    = gfarm_spool_check_levels[i].level;
			gfarm_spool_check_level_name
			    = gfarm_spool_check_levels[i].name;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_INVALID_ARGUMENT);
}

enum gfarm_atime_type
gfarm_atime_type_get(void)
{
	return (gfarm_atime_type);
}

const char *
gfarm_atime_type_get_by_name(void)
{
	return (gfarm_atime_type_name);
}

gfarm_error_t
gfarm_atime_type_set(enum gfarm_atime_type type)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_atime_types); i++) {
		if (type == gfarm_atime_types[i].type) {
			gfarm_atime_type = type;
			gfarm_atime_type_name = gfarm_atime_types[i].name;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_INVALID_ARGUMENT);
}

gfarm_error_t
gfarm_atime_type_set_by_name(const char *name)
{
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(gfarm_atime_types); i++) {
		if (strcmp(name, gfarm_atime_types[i].name) == 0) {
			gfarm_atime_type = gfarm_atime_types[i].type;
			gfarm_atime_type_name = gfarm_atime_types[i].name;
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_INVALID_ARGUMENT);
}

/*
 * get (almost) shell style token.
 * e.g.
 *	string...
 *	'string...' (do not interpret escape character `\')
 *	"string..." (interpret escape character `\')
 *	# comment
 * difference from shell token:
 *	don't allow newline in "..." and '...".
 *
 * return value:
 *	string
 *   OR
 *	NULL	- if error or end-of-line.
 * output parameter:
 *	*cursorp:
 *		next character to read
 *	*errorp:
 *		NULL (if success or end-of-line)
 *	    OR
 *		error message
 */

static gfarm_error_t
gfarm_strtoken(char **cursorp, char **tokenp)
{
	unsigned char *top, *p, *s = *(unsigned char **)cursorp;

	while (*s != '\n' && isspace(*s))
		s++;
	if (*s == '\0' || *s == '\n' || *s == '#') {
		/* end of line */
		*cursorp = (char *)s;
		*tokenp = NULL;
		return (GFARM_ERR_NO_ERROR);
	}
	top = s;
	p = s;
	for (;;) {
		switch (*s) {
		case '\'':
			s++;
			for (;;) {
				if (*s == '\'')
					break;
				if (*s == '\0' || *s == '\n') {
					gflog_debug(GFARM_MSG_1000919,
						"Unterminated single quote "
						"found in string");
					return (GFARM_ERRMSG_UNTERMINATED_SINGLE_QUOTE);
				}
				*p++ = *s++;
			}
			s++;
			break;
		case '"':
			s++;
			for (;;) {
				if (*s == '"')
					break;
				if (*s == '\0' || *s == '\n') {
					gflog_debug(GFARM_MSG_1000920,
						"Unterminated double quote "
						"found in string");
					return (GFARM_ERRMSG_UNTERMINATED_DOUBLE_QUOTE);
				}
				if (*s == '\\') {
					if (s[1] == '\0' || s[1] == '\n') {
						gflog_debug(GFARM_MSG_1000921,
							"Unterminated double "
							"quote found in string"
						);
						return (GFARM_ERRMSG_UNTERMINATED_DOUBLE_QUOTE);
					}
					/*
					 * only interpret `\"' and `\\'
					 * in double quotation.
					 */
					if (s[1] == '"' || s[1] == '\\')
						s++;
				}
				*p++ = *s++;
			}
			s++;
			break;
		case '\\':
			s++;
			if (*s == '\0' || *s == '\n') {
				gflog_debug(GFARM_MSG_1000922,
					"Incomplete escape found in string");
				return (GFARM_ERRMSG_INCOMPLETE_ESCAPE);
			}
			*p++ = *s++;
			break;
		case '\n':
		case '#':
		case '\0':
			*p = '\0';
			*cursorp = (char *)s;
			*tokenp = (char *)top;
			return (GFARM_ERR_NO_ERROR);
		default:
			if (isspace(*s)) {
				*p = '\0';
				*cursorp = (char *)(s + 1);
				*tokenp = (char *)top;
				return (GFARM_ERR_NO_ERROR);
			}
			*p++ = *s++;
			break;
		}
	}
}

static gfarm_error_t
parse_auth_arguments(char *p, enum gfarm_auth_config_position position,
	const char **op)
{
#ifndef __KERNEL__	/* parse_auth_arguments */

	gfarm_error_t e;
	char *tmp, *command, *auth, *host;
	enum gfarm_auth_method auth_method;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "auth") == 0); */

	e = gfarm_strtoken(&p, &command);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000923,
			"parsing of auth command argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (command == NULL) {
		gflog_debug(GFARM_MSG_1000924,
			"Missing first auth command argument");
		return (GFARM_ERRMSG_MISSING_1ST_AUTH_COMMAND_ARGUMENT);
	}

	e = gfarm_strtoken(&p, &auth);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000925,
			"parsing of auth method argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (auth == NULL) {
		gflog_debug(GFARM_MSG_1000926,
			"Missing second auth method argument");
		return (GFARM_ERRMSG_MISSING_2ND_AUTH_METHOD_ARGUMENT);
	}
	if (strcmp(auth, "*") == 0 || strcmp(auth, "ALL") == 0) {
		auth_method = GFARM_AUTH_METHOD_ALL;
	} else {
		e = gfarm_auth_method_parse(auth, &auth_method);
		if (e != GFARM_ERR_NO_ERROR) {
			*op = "2nd(auth-method) argument";
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e = GFARM_ERRMSG_UNKNOWN_AUTH_METHOD;
			gflog_debug(GFARM_MSG_1000927,
				"parsing of auth method (%s) failed: %s",
				auth, gfarm_error_string(e));
			return (e);
		}
	}

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000928,
			"parsing of auth host argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (host == NULL) {
		gflog_debug(GFARM_MSG_1000929,
			"Missing third auth host spec argument");
		return (GFARM_ERRMSG_MISSING_3RD_HOST_SPEC_ARGUMENT);
	}
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000930,
			"parsing of auth arguments (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (tmp != NULL) {
		gflog_debug(GFARM_MSG_1000931,
			"Too many auth arguments passed");
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "3rd(host-spec) argument";
		gflog_debug(GFARM_MSG_1000932,
			"parsing of auth host spec (%s) failed: %s",
			host, gfarm_error_string(e));
		return (e);
	}

	if (strcmp(command, "enable") == 0) {
		e = gfarm_auth_enable(auth_method, hostspecp, position);
	} else if (strcmp(command, "disable") == 0) {
		e = gfarm_auth_disable(auth_method, hostspecp, position);
	} else {
		/*
		 * we don't return `command' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(auth-command) argument";
		gfarm_hostspec_free(hostspecp);
		gflog_debug(GFARM_MSG_1000933,
			"Unknown auth subcommand (%s)",
			command);
		return (GFARM_ERRMSG_UNKNOWN_AUTH_SUBCOMMAND);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000934,
			"Failed to enable/disable auth (%s)(%s)(%s): (%s)",
			command, auth, host,
			gfarm_error_string(e));
		gfarm_hostspec_free(hostspecp);
	}
	return (e);
#else /* __KERNEL__ */
	return (GFARM_ERR_NO_ERROR);
#endif /* __KERNEL__ */
}

#if 0 /* not yet in gfarm v2 */
static gfarm_error_t
parse_netparam_arguments(char *p, const char **op)
{
	gfarm_error_t e;
	char *tmp, *option, *host;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "netparam") == 0); */

	e = gfarm_strtoken(&p, &option);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000935,
			"parsing of netparam option argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (option == NULL) {
		gflog_debug(GFARM_MSG_1000936,
			"Missing first netparam option argument");
		return (GFARM_ERRMSG_MISSING_NETPARAM_OPTION_ARGUMENT);
	}

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000937,
			"parsing of netparam host argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (host == NULL) {
		/* if 2nd argument is omitted, it is treated as "*". */
		host = "*";
	} else if ((e = gfarm_strtoken(&p, &tmp)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000938,
			"parsing of netparam arguments (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	} else if (tmp != NULL) {
		gflog_debug(GFARM_MSG_1000939,
			"Too many netparam arguments passed");
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}

	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		gflog_debug(GFARM_MSG_1000940,
			"parsing of netparam host spec (%s) failed: %s",
			host, gfarm_error_string(e));
		return (e);
	}

	e = gfarm_netparam_config_add_long(option, hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(sockopt-option) argument";
		gfarm_hostspec_free(hostspecp);
		gflog_debug(GFARM_MSG_1000941,
			"add netparam config (%s)(%s) failed: %s",
			host, option, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}
#endif

static gfarm_error_t
parse_sockopt_arguments(char *p, const char **op)
{
	gfarm_error_t e;
	char *tmp, *option, *host;
	struct gfarm_hostspec *hostspecp;
	int is_listener;

	/* assert(strcmp(*op, "sockopt") == 0); */

	e = gfarm_strtoken(&p, &option);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000942,
			"parsing of sockopt option argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (option == NULL) {
		gflog_debug(GFARM_MSG_1000943,
			"Missing sockopt option argument");
		return (GFARM_ERRMSG_MISSING_SOCKOPT_OPTION_ARGUMENT);
	}

	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000944,
			"parsing of sockopt host argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (host == NULL) {
		/*
		 * if 2nd argument is omitted, it is treated as:
		 *	"LISTENER" + "*".
		 */
		is_listener = 1;
	} else {
		is_listener = strcmp(host, "LISTENER") == 0;
		if ((e = gfarm_strtoken(&p, &tmp)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1000945,
				"parsing of sockopt arguments (%s) failed: %s",
				p, gfarm_error_string(e));
			return (e);
		}
		if (tmp != NULL) {
			gflog_debug(GFARM_MSG_1000946,
				"Too many sockopt arguments passed");
			return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
		}
	}

	if (is_listener) {
		e = gfarm_sockopt_listener_config_add(option);
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * we don't return `option' to *op here,
			 * because it may be too long.
			 */
			*op = "1st(sockopt-option) argument";
			gflog_debug(GFARM_MSG_1000947,
			    "cannot set sockopt %s for listener: %s",
			    option, gfarm_error_string(e));
			return (e);
		}
	}
	if (host == NULL || !is_listener) {
		e = gfarm_hostspec_parse(host != NULL ? host : "*",
		    &hostspecp);
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * we don't return `host' to *op here,
			 * because it may be too long.
			 */
			*op = "2nd(host-spec) argument";
			gflog_debug(GFARM_MSG_1000948,
				"parsing of sockopt host (%s) failed: %s",
				host, gfarm_error_string(e));
			return (e);
		}

		e = gfarm_sockopt_config_add(option, hostspecp);
		if (e != GFARM_ERR_NO_ERROR) {
			/*
			 * we don't return `option' to *op here,
			 * because it may be too long.
			 */
			*op = "1st(sockopt-option) argument";
			gfarm_hostspec_free(hostspecp);
			gflog_debug(GFARM_MSG_1000949,
			    "cannot set sockopt %s for host %s: %s",
			    option, host == NULL ? "*" : host,
			    gfarm_error_string(e));
			return (e);
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

#if 0 /* XXX address_use is disabled for now */
static gfarm_error_t
parse_address_use_arguments(char *p, const char **op)
{
	gfarm_error_t e;
	char *tmp, *address;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "address_use") == 0); */

	e = gfarm_strtoken(&p, &address);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (address == NULL)
		return (GFARM_ERRMSG_MISSING_ADDRESS_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);

	e = gfarm_hostspec_parse(address, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		return (e);
	}

	e = gfarm_host_address_use(hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}
#endif

static gfarm_error_t
parse_known_network_arguments(char *p, const char **op)
{
	gfarm_error_t e;
	char *tmp, *address;
	struct gfarm_hostspec *hostspecp;

	/* assert(strcmp(*op, "known_network") == 0); */

	e = gfarm_strtoken(&p, &address);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (address == NULL)
		return (GFARM_ERRMSG_MISSING_ADDRESS_ARGUMENT);
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (tmp != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);

	e = gfarm_hostspec_parse(address, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		return (e);
	}

	e = gfarm_known_network_list_add(hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `option' to *op here,
		 * because it may be too long.
		 */
		*op = "1st(address) argument";
		gfarm_hostspec_free(hostspecp);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_stringlist(char *p, const char **op,
	gfarm_stringlist *list, const char *listname)
{
	gfarm_error_t e;
	char *tmp, *arg;

	e = gfarm_strtoken(&p, &arg);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002448,
		    "parsing argument %s of %s failed: %s",
		    p, listname, gfarm_error_string(e));
		return (e);
	}
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002449,
		    "missing argument for %s", listname);
		return (GFARM_ERRMSG_MISSING_USER_MAP_FILE_ARGUMENT);
	}
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002450,
		    "parsing argument %s of %s failed: %s",
		    p, listname, gfarm_error_string(e));
		return (e);
	}
	if (tmp != NULL) {
		gflog_debug(GFARM_MSG_1002451,
		    "Too many arguments for %s", listname);
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}
	arg = strdup(arg);
	if (arg == NULL) {
		gflog_debug(GFARM_MSG_1002452,
		    "failed to allocate an argument of %s: no memory",
		    listname);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_stringlist_add(list, arg);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002453,
		    "failed to allocate a %s entry for \"%s\": no memory",
		    listname, arg);
		free(arg);
	}
	return (e);
}

#if 0 /* XXX NOTYET */
static gfarm_error_t
parse_client_architecture(char *p, const char **op)
{
	gfarm_error_t e;
	char *architecture, *host, *junk;
	struct gfarm_hostspec *hostspecp;

	e = gfarm_strtoken(&p, &architecture);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (architecture == NULL)
		return (GFARM_ERRMSG_MISSING_1ST_ARCHITECTURE_ARGUMENT);
	e = gfarm_strtoken(&p, &host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (host == NULL)
		return (GFARM_ERRMSG_MISSING_2ND_HOST_SPEC_ARGUMENT);
	e = gfarm_strtoken(&p, &junk);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (junk != NULL)
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	e = gfarm_hostspec_parse(host, &hostspecp);
	if (e != GFARM_ERR_NO_ERROR) {
		/*
		 * we don't return `host' to *op here,
		 * because it may be too long.
		 */
		*op = "2nd(host-spec) argument";
		return (e);
	}
	e = gfarm_set_client_architecture(architecture, hostspecp);
	if (e != GFARM_ERR_NO_ERROR)
		gfarm_hostspec_free(hostspecp);
	return (e);
}
#endif /* XXX NOTYET */

static gfarm_error_t
get_one_argument(char *p, char **rv)
{
	gfarm_error_t e;
	char *tmp, *s;

	e = gfarm_strtoken(&p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000955,
			"parsing of one argument (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (s == NULL) {
		gflog_debug(GFARM_MSG_1000956,
			"Missing argument");
		return (GFARM_ERRMSG_MISSING_ARGUMENT);
	}
	e = gfarm_strtoken(&p, &tmp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000957,
			"parsing of arguments (%s) failed: %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (tmp != NULL) {
		gflog_debug(GFARM_MSG_1000958,
			"Too many arguments passed");
		return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
	}

	*rv = s;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_var(char *p, char **rv)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000959,
			"get_one_argument failed "
			"when parsing var (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	if (*rv != NULL) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	s = strdup(s);
	if (s == NULL) {
		gflog_debug(GFARM_MSG_1000960,
		    "allocation of argument failed when parsing set var: %s",
		    gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*rv = s;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_parse_set_spool_root(char *p)
{
	gfarm_error_t e;
	char *s;
	int i, len1;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004461,
			"get_one_argument failed "
			"when parsing var (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	for (i = 0; i < GFARM_SPOOL_ROOT_NUM; ++i) {
		if (gfarm_spool_root[i] != NULL) {
			if (strcmp(gfarm_spool_root[i], s) == 0)
				break;
		} else {
			gfarm_spool_root[i] = strdup(s);
			if (gfarm_spool_root[i] == NULL) {
				gflog_error(GFARM_MSG_1004462, "no memory");
				return (GFARM_ERR_NO_MEMORY);
			}
			len1 = strlen(gfarm_spool_root[i]) - 1;
			while (len1 >= 0 && gfarm_spool_root[i][len1] == '/')
				gfarm_spool_root[i][len1--] = '\0';
			break;
		}
	}
	if (i == GFARM_SPOOL_ROOT_NUM)
		gflog_fatal(GFARM_MSG_1004463, "too many spool directories");
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_int(char *s, int *vp)
{
	char *ep;
	long v;

	errno = 0;
	v = strtol(s, &ep, 10);
	if (errno == 0 && (v > INT_MAX || v < INT_MIN))
		errno = ERANGE;
	if (errno != 0) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1000962,
			"conversion to integer failed "
			"when parsing misc integer (%s): %s",
			s, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	if (ep == s) {
		gflog_debug(GFARM_MSG_1000963,
			"Integer expected when parsing misc integer but (%s)",
			s);
		return (GFARM_ERRMSG_INTEGER_EXPECTED);
	}
	if (*ep != '\0') {
		gflog_debug(GFARM_MSG_1000964,
			"Invalid character found "
			"when parsing misc integer (%s)",
			s);
		return (GFARM_ERRMSG_INVALID_CHARACTER);
	}
	*vp = (int)v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_int(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000961,
			"get_one_argument failed "
			"when parsing misc integer (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);

	return (parse_set_int(s, vp));
}

static gfarm_error_t
parse_set_misc_percentage(char *p, int *vp)
{
	gfarm_error_t e;

	e = parse_set_misc_int(p, vp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005010,
			"parse_set_misc_int failed (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (*vp > 100 || *vp < 0) {
		gflog_debug(GFARM_MSG_1005011,
			"parse_set_misc_percentage: invalid value (%s): %d",
			p, *vp);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_sockbuf_limit_int(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004464,
			"get_one_argument failed "
			"when sockbuf limit (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);

	if (strcmp(s, "disable") == 0) {
		*vp = GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_UNLIMITED;
		return (GFARM_ERR_NO_ERROR);
	}

	return (parse_set_int(s, vp));
}

/* client cannot use this */
static gfarm_error_t
parse_set_misc_float(char *p, float *vp)
{
#ifndef __KERNEL__	/* parse_set_misc_float */
	gfarm_error_t e;
	char *ep, *s;
	double v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	errno = 0;
	v = strtod(s, &ep);	/* strtof is not ANSI C standard */
	if (errno != 0)
		return (gfarm_errno_to_error(errno));
	if (ep == s)
		return (GFARM_ERRMSG_FLOATING_POINT_NUMBER_EXPECTED);
	if (*ep != '\0')
		return (GFARM_ERRMSG_INVALID_CHARACTER);
	*vp = (float)v;
#else /* __KERNEL__ */
	gflog_warning(GFARM_MSG_1003862, "floating %s is ignored", p);
#endif /* __KERNEL__ */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_offset(char *p, gfarm_off_t *vp)
{
	gfarm_error_t e;
	char *s;
	gfarm_int64_t v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000965,
			"get_one_argument failed "
			"when parsing misc offset (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	errno = 0;
	e = gfarm_humanize_number_to_int64(&v, s);
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

#define GFARM_F2LL_MAX (INT_MAX * GFARM_F2LL_SCALE)

static gfarm_error_t
parse_set_float_to_long_long(char *p, long long *vp)
{
	gfarm_error_t e;
	char *s;
	const unsigned char *pp;
	size_t len;
	int have_dot = 0, num_f = 0, done = 0;
	long long d = 0, f = 0, tmp;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);

	pp = (unsigned char *)s;
	len = strlen(s);
	for (; len > 0; len--) {
		if (*pp == '.') {
			if (have_dot)
				return (
				GFARM_ERRMSG_FLOATING_POINT_NUMBER_EXPECTED);
			have_dot = 1;
			if (*(pp + 1) == '\0')
				return (
				GFARM_ERRMSG_FLOATING_POINT_NUMBER_EXPECTED);
		} else if (!isdigit(*pp))
			return (GFARM_ERRMSG_INVALID_CHARACTER);
		else {
			if (have_dot) {
				if (num_f >= GFARM_F2LL_SCALE_SIZE)
					done = 1;
				num_f++;
			}
			if (!done) {
				if (have_dot) {
					tmp = f * 10 + (*pp - '0');
					if (tmp > GFARM_F2LL_MAX || tmp < f)
						return (
						GFARM_ERR_RESULT_OUT_OF_RANGE);
					f = tmp;
				} else {
					tmp = d * 10 + (*pp - '0');
					if (tmp > GFARM_F2LL_MAX || tmp < d)
						return (
						GFARM_ERR_RESULT_OUT_OF_RANGE);
					d = tmp;
				}
			} /* else: skip */
		}
		pp++;
	}
	for (; num_f < GFARM_F2LL_SCALE_SIZE; num_f++) {
		tmp = f * 10;
		if (tmp > GFARM_F2LL_MAX || tmp < f)
			return (GFARM_ERR_RESULT_OUT_OF_RANGE);
		f = tmp;
	}
	tmp = d * GFARM_F2LL_SCALE + f;
	if (tmp > GFARM_F2LL_MAX || tmp < d)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	d = tmp;
	*vp = d;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_set_misc_enabled(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000969,
			"get_one_argument failed "
			"when parsing misc enabled (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	if (strcmp(s, "enable") == 0)
		v = 1;
	else if (strcmp(s, "disable") == 0)
		v = 0;
	else {
		gflog_debug(GFARM_MSG_1000970,
			"'enable' or 'disable' expected "
			"when parsing misc enabled but (%s)",
			s);
		return (GFARM_ERRMSG_ENABLED_OR_DISABLED_EXPECTED);
	}
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_metadb_server_port(char *p, const char **op)
{
	char *s;
	const char *listname = *op;
	struct servent *sp;
	int port;
	gfarm_error_t e;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		*op = "port argument";
		gflog_debug(GFARM_MSG_1003367, "%s %s: %s: %s",
		    listname, *op, p, gfarm_error_string(e));
		return (e);
	}
	if (gfarm_ctxp->metadb_server_port != GFARM_CONFIG_MISC_DEFAULT)
		return (GFARM_ERR_NO_ERROR);

	sp = getservbyname(s, "tcp");
	if (sp != NULL)
		gfarm_ctxp->metadb_server_port = ntohs(sp->s_port);
	else if ((port = strtol(s, NULL, 0)) != 0 && port > 0 && port < 65536)
		gfarm_ctxp->metadb_server_port = port;
	else {
		*op = "port argument";
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1003368, "%s %s: %s: %s",
		    listname, *op, s, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_cred_config(char *p, char *service,
	gfarm_error_t (*set)(char *, char *))
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000971,
			"get_one_argument failed "
			"when parsing cred config (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	return ((*set)(service, s));
}

static gfarm_error_t
parse_digest_type(char *p, char **rv)
{
	gfarm_error_t e = parse_set_var(p, rv);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (strcmp(*rv, "disable") == 0) {
		free(*rv);
		*rv = NULL; /* back to the default */
	} else if (!gfarm_msgdigest_name_verify(*rv)) {
		/* XXX this leaves `*rv' as is */
		gflog_debug(GFARM_MSG_1003863,
		    "invalid digest type <%s>", *rv);
		return (GFARM_ERRMSG_INVALID_DIGEST_TYPE);
	}

	return (GFARM_ERR_NO_ERROR);
}

struct gfarm_name_value_tuple {
	const char *name;
	int value;
};

static gfarm_error_t
parse_set_misc_name_value_table(char *p, int *vp,
	struct gfarm_name_value_tuple *table, size_t table_size,
	const char *table_name)
{
	gfarm_error_t e;
	char *s;
	size_t i;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005046,
		    "get_one_argument failed "
		    "when parsing %s(%s): %s",
		    table_name, p, gfarm_error_string(e));
		return (e);
	}

	for (i = 0; i < table_size; i++) {
		if (strcmp(s, table[i].name) == 0)
			break;
	}
	if (i >= table_size) {
		gflog_debug(GFARM_MSG_1005047,
		    "%s(%s): unknown %s", table_name, s, table_name);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	*vp = table[i].value;

	return (GFARM_ERR_NO_ERROR);
}

static struct gfarm_name_value_tuple lock_type_name_table[] = {
	{ "mutex", GFARM_LOCK_TYPE_MUTEX },
	{ "ticketlock", GFARM_LOCK_TYPE_TICKETLOCK },
	{ "queuelock", GFARM_LOCK_TYPE_QUEUELOCK },
};

static gfarm_error_t
parse_set_misc_lock_type(char *p, int *vp)
{
	return (parse_set_misc_name_value_table(p, vp,
	    lock_type_name_table, GFARM_ARRAY_LENGTH(lock_type_name_table),
	    "lock_type"));
}

static gfarm_error_t
parse_log_level(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000972,
			"get_one_argument failed "
			"when parsing log level (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	v = gflog_syslog_name_to_priority(s);
	if (v == -1) {
		gflog_debug(GFARM_MSG_1000973,
			"Invalid syslog priority level (%s)",
			s);
		return (GFARM_ERRMSG_INVALID_SYSLOG_PRIORITY_LEVEL);
	}
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_log_file(char *p)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005091,
			"get_one_argument failed "
			"when parsing log file (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	if (gflog_file_open(s) == NULL) {
		e = gfarm_errno_to_error(errno);
		gflog_debug(GFARM_MSG_1005092, "Invalid log file (%s)", s);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_spool_check_level(char *p)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003566,
			"get_one_argument failed "
			"when parsing spool_check_level(%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	/* first line has precedence */
	if (gfarm_spool_check_level != GFARM_SPOOL_CHECK_LEVEL_DEFAULT)
		return (GFARM_ERR_NO_ERROR);
	e = gfarm_spool_check_level_set_by_name(s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003567,
		    "spool_check_level(%s): %s", s, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_atime_type(char *p)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003568,
			"get_one_argument failed "
			"when parsing atime(%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}
	/* first line has precedence */
	if (gfarm_atime_type != GFARM_ATIME_DEFAULT)
		return (GFARM_ERR_NO_ERROR);
	e = gfarm_atime_type_set_by_name(s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003569,
		    "atime(%s): %s", s, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_hostname_and_port(char *host_and_port, const char *listname,
	char **hostname, int *port)
{
	gfarm_error_t e;
	char c, *p, *sport;
	long lport;
	size_t n;

	p = host_and_port;
	n = strcspn(p, ":");
	if (n == 0) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1002538,
		    "parsing of %s host argument failed: %s",
		    listname, gfarm_error_string(e));
		if (*p)
			p++;
		*hostname = NULL;
		return (e);
	}
	*hostname = p;
	p += n;
	c = *p;
	*p = 0;
	if (c != ':')
		return (GFARM_ERR_NO_ERROR);
	p++;
	sport = p;
	errno = 0;
	lport = strtol(sport, NULL, 10);
	if (errno != 0 || lport <= 0 || lport > 0xFFFF) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1002539,
		    "parsing of %s port argument (%s) failed: %s",
		    listname, sport, gfarm_error_string(e));
		return (e);
	}
	*port = (int)lport;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
parse_local_usergroup_map_arguments(char *p, const char **op, int is_user)
{
	gfarm_error_t e;
	char *tmp, *filepath, *host_and_port, *host = NULL;
	const char *listname;
	int port = GFMD_DEFAULT_PORT;

	listname = *op;
	if ((e = gfarm_strtoken(&p, &filepath)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002540,
		    "parsing of %s file path argument (%s) failed: %s",
		    listname, p, gfarm_error_string(e));
		return (e);
	}
	if (filepath == NULL) {
		*op = "1st (file path) argument";
		gflog_debug(GFARM_MSG_1002541,
		    "Missing %s file path argument", listname);
		return (GFARM_ERRMSG_MISSING_USER_MAP_FILE_ARGUMENT);
	}
	if ((e = gfarm_strtoken(&p, &host_and_port))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002542,
		    "parsing of %s file path argument (%s) failed: %s",
		    listname, p, gfarm_error_string(e));
		return (e);
	}
	if (host_and_port) {
		if ((e = parse_hostname_and_port(host_and_port, listname,
		    &host, &port)) != GFARM_ERR_NO_ERROR) {
			*op = "2nd (hostname:port) argument";
			gflog_debug(GFARM_MSG_1002543,
			    "parsing of %s arguments (%s) failed: %s",
			    listname, p, gfarm_error_string(e));
			return (e);
		}
		if ((e = gfarm_strtoken(&p, &tmp)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002544,
			    "parsing of %s arguments (%s) failed: %s",
			    listname, p, gfarm_error_string(e));
			return (e);
		}
		if (tmp) {
			gflog_debug(GFARM_MSG_1002545,
			    "Too many local_%s_map arguments passed",
			    is_user ? "user" : "group");
			return (GFARM_ERRMSG_TOO_MANY_ARGUMENTS);
		}
	}
	if ((e = local_ug_maps_enter(host, (int)port, is_user, filepath))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002546,
		    "local_ug_maps_enter for %s failed: %s",
		    listname, gfarm_error_string(e));
		return (e);
	}

	return (GFARM_ERR_NO_ERROR);
}

static void
eval_profile(int enabled)
{
	if (enabled)
		gfs_profile_set();
	else
		gfs_profile_unset();
}

static gfarm_error_t
parse_profile(char *p, int *vp)
{
	gfarm_error_t e = parse_set_misc_enabled(p, vp);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	eval_profile(*vp);

	return (e);
}

static gfarm_error_t
parse_metadb_server_list_arguments(char *p, const char **op)
{
#ifndef __KERNEL__	/* METADB_SERVER_NUM_MAX :: too big */
#define METADB_SERVER_NUM_MAX 1024
#else /* __KERNEL__ */
#define METADB_SERVER_NUM_MAX 128
#endif /* __KERNEL__ */

	gfarm_error_t e;
	int i, port;
	char *host_and_port, *host = NULL;
	const char *listname = *op;
	struct gfarm_metadb_server *m;
	int n = 0;
	struct gfarm_filesystem *fs;
	struct gfarm_metadb_server *ms[METADB_SERVER_NUM_MAX];

	for (;;) {
		if ((e = gfarm_strtoken(&p, &host_and_port))
		    != GFARM_ERR_NO_ERROR) {
			*op = "hostname:port argument";
			gflog_debug(GFARM_MSG_1002547,
			    "parsing of %s (%s) failed: %s",
			    listname, p, gfarm_error_string(e));
			goto error;
		}
		if (host_and_port == NULL)
			break;
		if (n >= METADB_SERVER_NUM_MAX) {
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_1002548,
			    "Too many arguments passed to %s", listname);
			goto error;
		}
		port = -1;
		if ((e = parse_hostname_and_port(host_and_port, listname,
		    &host, &port)) != GFARM_ERR_NO_ERROR) {
			*op = "hostname:port argument";
			gflog_debug(GFARM_MSG_1002549,
			    "parsing of %s arguments (%s) failed: %s",
			    listname, p, gfarm_error_string(e));
			return (e);
		}
		host = strdup(host);
		if (host == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002550,
			    "%s", gfarm_error_string(e));
			goto error;
		}
		if (port < 0)
			port = GFMD_DEFAULT_PORT;
		if (gfarm_filesystem_get(host, port) != NULL) {
			gflog_debug(GFARM_MSG_1004504,
			    "duplicate metadb server ignored: %s:%d",
			    host, port);
			free(host);
			continue;
		}
		if ((e = gfarm_metadb_server_new(&m, host, port))
		    != GFARM_ERR_NO_ERROR) {
			free(host);
			goto error;
		}
		ms[n++] = m;
	}
	if (n == 0) {
		*op = "1st (hostname:port) argument";
		gflog_debug(GFARM_MSG_1002551,
		    "Too few arguments passed to %s", listname);
		/* allow the same gfarm_metadb_server_list line */
		return (GFARM_ERR_NO_ERROR);
	}
	if ((e = gfarm_filesystem_new(&fs)) != GFARM_ERR_NO_ERROR)
		goto error;
	if ((e = gfarm_filesystem_set_metadb_server_list(fs, ms, n))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002553,
		    "%s", gfarm_error_string(e));
		goto error;
	}
	return (GFARM_ERR_NO_ERROR);
error:
	for (i = 0; i < n; ++i)
		gfarm_metadb_server_free(ms[i]);
	return (e);
}

static gfarm_error_t
parse_db_access_type(char *p)
{
	gfarm_error_t e;
	char *s;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (strcmp(s, "dbq") == 0)
		e = gfarm_set_db_access_type(GFARM_DB_ACCESS_TYPE_DBQ);
	else if (strcmp(s, "journal") == 0)
		e = gfarm_set_db_access_type(GFARM_DB_ACCESS_TYPE_JOURNAL);
	else if (strcmp(s, "none") == 0)
		e = gfarm_set_db_access_type(GFARM_DB_ACCESS_TYPE_NONE);
	else
		e = GFARM_ERR_INVALID_ARGUMENT;

	return (e);
}

static gfarm_error_t
parse_rdma_mr_reg_mode(char *p, int *vp)
{
	gfarm_error_t e;
	char *arg;
	int	mode = 0;

	for (;;) {
		if ((e = gfarm_strtoken(&p, &arg))
		    != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004522,
			"parsing of rdma_mr_reg_mode argument (%s) failed: %s",
				    p, gfarm_error_string(e));
			return (e);
		}
		if (!arg)
			break;
		if (!strcmp(arg, "static"))
			mode |= GFARM_RDMA_REG_MR_STATIC;
		else if (!strcmp(arg, "dynamic"))
			mode |= GFARM_RDMA_REG_MR_DYNAMIC;
		else {
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_1004523,
			"parsing of rdma_mr_reg_mode argument (%s) failed: %s",
				    p, gfarm_error_string(e));
			return (e);
		}
	}
	if (!mode) {
		gflog_debug(GFARM_MSG_1004524,
		    "parsing of rdma_mr_reg_mode argument, no args");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	*vp = mode;
	return (e);
}

#define CMD_SIZE_UNIT	10

static char *
expand_debug_command_arg_pattern(const char *pattern)
{
	const char *p = pattern, *copy_from;
	char *cmd = NULL, *cmd_new;
	size_t cmd_size = 0, cmd_offset = 0, copy_len;
	int nul_copied = 0;

	for (;;) {
		if (*p == '%') {
			switch (*(p + 1)) {
			case '\0':
				copy_from = p + 1;
				copy_len = 1;
				nul_copied = 1;
				break;
			case '%':
				copy_from = p;
				copy_len = 1;
				p += 2;
				break;
			case 'p':
				copy_from = p;
				copy_len = 2;
				p += 2;
				break;
			case 'e':
				copy_from = staticp->argv0;
				copy_len = strlen(staticp->argv0);
				p += 2;
				break;
			default:
				p += 2;
				continue;
			}
		} else if (*p == '\0') {
			copy_from = p;
			copy_len = 1;
			nul_copied = 1;
		} else {
			copy_from = p;
			copy_len = 1;
			p++;
		}

		if (cmd_offset + copy_len > cmd_size) {
			do {
				cmd_size += CMD_SIZE_UNIT;
			} while (cmd_offset + copy_len > cmd_size);
			cmd_new = realloc(cmd, cmd_size);
			if (cmd_new == NULL) {
				free(cmd);
				return (NULL);
			}
			cmd = cmd_new;
		}
		while (copy_len > 0) {
			cmd[cmd_offset++] = *copy_from++;
			copy_len--;
		}

		if (nul_copied)
			break;
	}

	return (cmd);
}

#define MAX_DEBUG_COMMAND_LENGTH	20

static gfarm_error_t
parse_debug_command(char *p, const char **op)
{
	gfarm_error_t e;
	char *argv[MAX_DEBUG_COMMAND_LENGTH], *arg, *diag = "debug_command";
	int argc, i;

	/*
	 * first line has precedence,
	 * to make $HOME/.gfarm2rc more effective than /etc/gfarm2.conf
	 */
	if (staticp->debug_command_argv != NULL || staticp->argv0 == NULL)
		return (GFARM_ERR_NO_ERROR);

	for (argc = 0; argc < MAX_DEBUG_COMMAND_LENGTH; ++argc) {
		e = gfarm_strtoken(&p, &arg);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003410,
			    "invalid argument of %s (%s): %s",
			    diag, p, gfarm_error_string(e));
			goto error;
		}
		if (arg == NULL)
			break;
		argv[argc] = expand_debug_command_arg_pattern(arg);
		if (argv[argc] == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1003411,
			    "failed to allocate an argument of %s: %s",
			    diag, gfarm_error_string(e));
			goto error;
		}
	}
	if (argc == MAX_DEBUG_COMMAND_LENGTH) {
		e = GFARM_ERR_ARGUMENT_LIST_TOO_LONG;
		gflog_error(GFARM_MSG_1003412, "%s: %s", diag,
		    gfarm_error_string(e));
		goto error;
	}
	argv[argc] = NULL;

	if (argc > 0) {
		staticp->debug_command_argv =
		    malloc(sizeof(char *) * (argc + 1));
		if (staticp->debug_command_argv == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1003413,
			    "failed to allocate a list of arguments of %s: %s",
			    diag, gfarm_error_string(e));
			goto error;
		}
		memcpy(staticp->debug_command_argv, argv,
		    sizeof(char *) * (argc + 1));
	}
	return (GFARM_ERR_NO_ERROR);

	/*
	 * In case an error occurs.
	 */
error:
	for (i = 0; i < argc; i++)
		free(argv[i]);
	return (e);
}

static void
debug_command_argv_free(void)
{
	int i;

	if (staticp->debug_command_argv != NULL) {
		for (i = 0; staticp->debug_command_argv[i] != NULL; i++)
			free(staticp->debug_command_argv[i]);
		free(staticp->debug_command_argv);
		staticp->debug_command_argv = NULL;
	}
}

static gfarm_error_t
parse_fatal_action(char *p, int *vp)
{
	gfarm_error_t e;
	char *s;
	int v;

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003445,
			"get_one_argument failed "
			"when parsing fatal action (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	if (*vp != GFARM_CONFIG_MISC_DEFAULT) /* first line has precedence */
		return (GFARM_ERR_NO_ERROR);
	v = gflog_fatal_action_name_to_number(s);
	if (v == GFLOG_ERROR_INVALID_FATAL_ACTION_NAME) {
		gflog_debug(GFARM_MSG_1003446,
			"Invalid fatal action name (%s)", s);
		return (GFARM_ERRMSG_UNKNOWN_KEYWORD);
	}
	*vp = v;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t parse_one_line(const char *, char *,
	enum gfarm_auth_config_position, const char *, int, const char **);

static gfarm_error_t
parse_include(char *p, const char **op, const char *file, int lineno)
{
	gfarm_error_t e;
	char *s, *malloced_filename = NULL;
	FILE *config;
	int nesting_limit =
	    gfarm_ctxp->include_nesting_limit != GFARM_CONFIG_MISC_DEFAULT ?
	    gfarm_ctxp->include_nesting_limit :
	    GFARM_CONFIG_INCLUDE_NESTING_LIMIT_DEFAULT;

	if (file == NULL)
		file = "<no file name>";

	e = get_one_argument(p, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005118,
			"get_one_argument failed "
			"when parsing misc enabled (%s): %s",
			p, gfarm_error_string(e));
		return (e);
	}

	++gfarm_ctxp->include_nesting_level;
	if (gfarm_ctxp->include_nesting_level > nesting_limit) {
		gflog_error(GFARM_MSG_1005119,
		    "file %s, line %d: include nesting level %d "
		    "exceeds include_nesting_limit %d",
		    file, lineno,
		    gfarm_ctxp->include_nesting_level, nesting_limit);
		--gfarm_ctxp->include_nesting_level;
		return (GFARM_ERR_TOO_MANY_OPEN_FILES);
	}

	if (s[0] != '/' && file != NULL)  {
		malloced_filename = gfarm_config_dirname_add(s, file);
		if (malloced_filename == NULL) {
			gflog_error(GFARM_MSG_1005120,
			    "file %s, line %d: no memory to include %s",
			    file, lineno, s);
		}
		s = malloced_filename;
	}
	config = fopen(s, "r");
	if (config == NULL) {
		*op = s;
		gflog_debug(GFARM_MSG_1005121,
		    "%s: cannot open include file", s);
		free(malloced_filename);
		--gfarm_ctxp->include_nesting_level;
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}
	e = gfarm_config_read_file(config, &lineno, s);
	if (e != GFARM_ERR_NO_ERROR) {
		*op = s;
		/* caller will show this error */
		gflog_debug(GFARM_MSG_1005122, "%s: line %d: %s",
		    s, lineno, gfarm_error_string(e));
	}
	/* fclose(config) is done by gfarm_config_read_file() */
	free(malloced_filename);
	--gfarm_ctxp->include_nesting_level;
	return (e);
}

static gfarm_error_t
parse_one_line(const char *s, char *p,
	enum gfarm_auth_config_position position, const char *file, int lineno,
	const char **op)
{
	gfarm_error_t e;
	const char *o;

	if (strcmp(s, o = "include") == 0) {
		e = parse_include(p, &o, file, lineno);
	} else if (strcmp(s, o = "include_nesting_limit") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->include_nesting_limit);
	} else if (strcmp(s, o = "spool") == 0) {
		e = gfarm_parse_set_spool_root(p);
	} else if (strcmp(s, o = "spool_server_listen_address") == 0) {
		e = parse_set_var(p, &gfarm_spool_server_listen_address);
	} else if (strcmp(s, o = "spool_server_listen_backlog") == 0) {
		e = parse_set_misc_int(p, &gfarm_spool_server_listen_backlog);
	} else if (strcmp(s, o = "spool_server_cred_type") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "spool_server_cred_service") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "spool_server_cred_name") == 0) {
		e = parse_cred_config(p, GFS_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);
	} else if (strcmp(s, o = "spool_server_back_channel_rcvbuf_limit")
	    == 0) {
		e = parse_set_sockbuf_limit_int(p,
		    &gfarm_spool_server_back_channel_rcvbuf_limit);
	} else if (strcmp(s, o = "spool_server_read_only_retry_interval") == 0
	    ) {
		e = parse_set_misc_int(p,
		    &gfarm_spool_server_read_only_retry_interval);
	} else if (strcmp(s, o = "spool_check_level") == 0) {
		e = parse_spool_check_level(p);
	} else if (strcmp(s, o = "spool_check_parallel") == 0) {
		e = parse_set_misc_int(p, &gfarm_spool_check_parallel);
	} else if (strcmp(s, o = "spool_check_parallel_max") == 0) {
		e = parse_set_misc_int(p, &gfarm_spool_check_parallel_max);
	} else if (strcmp(s, o = "spool_check_parallel_step") == 0) {
		e = parse_set_misc_int(p, &gfarm_spool_check_parallel_step);
	} else if (strcmp(s, o = "spool_check_parallel_per_capacity") == 0) {
		e = parse_set_misc_offset(p,
		    &gfarm_spool_check_parallel_per_capacity);
	} else if (strcmp(s, o = "spool_base_load") == 0) {
		e = parse_set_misc_float(p, &gfarm_spool_base_load);
	} else if (strcmp(s, o = "spool_digest_error_check") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_spool_digest_error_check);

	} else if (strcmp(s, o = "write_verify") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_write_verify);
	} else if (strcmp(s, o = "write_verify_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_write_verify_interval);
	} else if (strcmp(s, o = "write_verify_retry_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_write_verify_retry_interval);
	} else if (strcmp(s, o = "write_verify_log_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_write_verify_log_interval);
	} else if (strcmp(s, o = "metadb_server_host") == 0) {
		e = parse_set_var(p, &gfarm_ctxp->metadb_server_name);
	} else if (strcmp(s, o = "metadb_server_port") == 0) {
		e = parse_metadb_server_port(p, &o);
	} else if (strcmp(s, o = "metadb_server_list") == 0) {
		e = parse_metadb_server_list_arguments(p, &o);
	} else if (strcmp(s, o = "metadb_server_listen_backlog") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_server_listen_backlog);
	} else if (strcmp(s, o = "admin_user") == 0) {
		e = parse_set_var(p, &gfarm_ctxp->metadb_admin_user);
	} else if (strcmp(s, o = "admin_user_gsi_dn") == 0) {
		e = parse_set_var(p, &gfarm_ctxp->metadb_admin_user_gsi_dn);
	} else if (strcmp(s, o = "metadb_server_cred_type") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_type_set_by_string);
	} else if (strcmp(s, o = "metadb_server_cred_service") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_service_set);
	} else if (strcmp(s, o = "metadb_server_cred_name") == 0) {
		e = parse_cred_config(p, GFM_SERVICE_TAG,
		    gfarm_auth_server_cred_name_set);
	} else if (strcmp(s, o = "metadb_db_access_type") == 0) {
		e = parse_db_access_type(p);

	} else if (strcmp(s, o = "ldap_server_host") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_name);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_server_port") == 0) {
		e = parse_set_var(p, &gfarm_ldap_server_port);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_base_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_base_dn);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_bind_dn") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_dn);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_bind_password") == 0) {
		e = parse_set_var(p, &gfarm_ldap_bind_password);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_cipher_suite") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_cipher_suite);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_certificate_key_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_key_file);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();
	} else if (strcmp(s, o = "ldap_tls_certificate_file") == 0) {
		e = parse_set_var(p, &gfarm_ldap_tls_certificate_file);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_ldap();

	} else if (strcmp(s, o = "postgresql_server_host") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_name);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_server_port") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_server_port);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_dbname") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_dbname);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_user") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_user);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_password") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_password);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();
	} else if (strcmp(s, o = "postgresql_conninfo") == 0) {
		e = parse_set_var(p, &gfarm_postgresql_conninfo);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_postgresql();

	} else if (strcmp(s, o = "localfs_datadir") == 0) {
		e = parse_set_var(p, &gfarm_localfs_datadir);
		if (e == GFARM_ERR_NO_ERROR)
			e = set_backend_db_type_localfs();

	} else if (strcmp(s, o = "shared_key_file") == 0) {
		e = parse_set_var(p, &staticp->shared_key_file);
	} else if (strcmp(s, o = "auth") == 0) {
		e = parse_auth_arguments(p, position, &o);
#if 0 /* not yet in gfarm v2 */
	} else if (strcmp(s, o = "netparam") == 0) {
		e = parse_netparam_arguments(p, &o);
#endif
	} else if (strcmp(s, o = "sockopt") == 0) {
		e = parse_sockopt_arguments(p, &o);
#if 0 /* XXX address_use is disabled for now */
	} else if (strcmp(s, o = "address_use") == 0) {
		e = parse_address_use_arguments(p, &o);
#endif
	} else if (strcmp(s, o = "known_network") == 0) {
		e = parse_known_network_arguments(p, &o);
	} else if (strcmp(s, o = "xattr_cache") == 0) {
		e = parse_stringlist(p, &o,
		    &staticp->xattr_cache_list, "xattr cache");
	} else if (strcmp(s, o = "local_user_map") == 0) {
		e = parse_local_usergroup_map_arguments(p, &o, 1);
	} else if (strcmp(s, o = "local_group_map") == 0) {
		e = parse_local_usergroup_map_arguments(p, &o, 0);
#if 0 /* XXX NOTYET */
	} else if (strcmp(s, o = "client_architecture") == 0) {
		e = parse_client_architecture(p, &o);
#endif

	} else if (strcmp(s, o = "digest") == 0) {
		e = parse_digest_type(p, &gfarm_digest);
	} else if (strcmp(s, o = "log_file") == 0) {
		e = parse_log_file(p);
	} else if (strcmp(s, o = "log_level") == 0) {
		e = parse_log_level(p, &gfarm_ctxp->log_level);
	} else if (strcmp(s, o = "log_message_verbose_level") == 0) {
		e = parse_set_misc_int(p, &staticp->log_message_verbose);
		if (e == GFARM_ERR_NO_ERROR)
			gflog_set_message_verbose(staticp->log_message_verbose);
	} else if (strcmp(s, o = "log_auth_verbose") == 0) {
		int tmp = GFARM_CONFIG_MISC_DEFAULT;
		e = parse_set_misc_enabled(p, &tmp);
		if (e == GFARM_ERR_NO_ERROR)
			gflog_auth_set_verbose(tmp);
	} else if (strcmp(s, o = "no_file_system_node_timeout") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_ctxp->no_file_system_node_timeout);
	} else if (strcmp(s, o = "gfmd_authentication_timeout") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_ctxp->gfmd_authentication_timeout);
	} else if (strcmp(s, o = "gfmd_reconnection_timeout") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_ctxp->gfmd_reconnection_timeout);
	} else if (strcmp(s, o = "gfsd_connection_timeout") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_ctxp->gfsd_connection_timeout);
	} else if (strcmp(s, o = "attr_cache_limit") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->attr_cache_limit);
	} else if (strcmp(s, o = "attr_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->attr_cache_timeout);
	} else if (strcmp(s, o = "page_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->page_cache_timeout);
	} else if (strcmp(s, o = "schedule_rpc_timeout") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_ctxp->schedule_rpc_timeout);
	} else if (strcmp(s, o = "schedule_cache_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->schedule_cache_timeout);
	} else if (strcmp(s, o = "schedule_concurrency") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->schedule_concurrency);
	} else if (strcmp(s, o = "schedule_concurrency_per_net") == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_ctxp->schedule_concurrency_per_net);
	} else if (strcmp(s, o = "schedule_idle_load_thresh") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_idle_load);
	} else if (strcmp(s, o = "schedule_busy_load_thresh") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_busy_load);
	} else if (strcmp(s, o = "schedule_virtual_load") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_virtual_load);
	} else if (strcmp(s, o = "schedule_candidates_ratio") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_candidates_ratio);
	} else if (strcmp(s, o = "schedule_rtt_thresh") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_rtt_thresh_ratio);
	} else if (strcmp(s, o = "schedule_rtt_thresh_ratio") == 0) {
		e = parse_set_float_to_long_long(
		    p, &gfarm_ctxp->schedule_rtt_thresh_ratio);
	} else if (strcmp(s, o = "schedule_rtt_thresh_diff") == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_ctxp->schedule_rtt_thresh_diff);
	} else if (strcmp(s, o = "write_local_priority") == 0) {
		e = parse_set_misc_enabled(p,
		    &gfarm_ctxp->schedule_write_local_priority);
	} else if (strcmp(s, o = "write_target_domain") == 0) {
		e = parse_set_var(p, &gfarm_ctxp->schedule_write_target_domain);
	} else if (strcmp(s, o = "minimum_free_disk_space") == 0) {
		e = parse_set_misc_offset(p,
		    &staticp->minimum_free_disk_space);
	} else if (strcmp(s, o = "direct_local_access") == 0) {
		e = parse_set_misc_enabled(p,
		    &gfarm_ctxp->direct_local_access);
	} else if (strcmp(s, o = "replication_at_write_open") == 0) {
		e = parse_set_misc_enabled(p,
		    &gfarm_ctxp->replication_at_write_open);
	} else if (strcmp(s, o = "read_only") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_read_only);
	} else if (strcmp(s, o = "simultaneous_replication_receivers") == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_simultaneous_replication_receivers);
	} else if (strcmp(s, o = "replication_busy_host") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_replication_busy_host);
	} else if (strcmp(s, o = "gfsd_connection_cache") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->gfsd_connection_cache);
	} else if (strcmp(s, o = "gfmd_connection_cache") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->gfmd_connection_cache);
	} else if (strcmp(s, o = "xattr_size_limit") == 0) {
		e = parse_set_misc_int(p, &gfarm_xattr_size_limit);
		if (e == GFARM_ERR_NO_ERROR &&
		    gfarm_xattr_size_limit > GFARM_XATTR_SIZE_MAX_LIMIT) {
			e = GFARM_ERR_VALUE_TOO_LARGE_TO_BE_STORED_IN_DATA_TYPE;
			gfarm_xattr_size_limit = GFARM_CONFIG_MISC_DEFAULT;
		}
	} else if (strcmp(s, o = "xmlattr_size_limit") == 0) {
		e = parse_set_misc_int(p, &gfarm_xmlattr_size_limit);
		if (e == GFARM_ERR_NO_ERROR &&
		    gfarm_xmlattr_size_limit > GFARM_XMLATTR_SIZE_MAX_LIMIT) {
			e = GFARM_ERR_VALUE_TOO_LARGE_TO_BE_STORED_IN_DATA_TYPE;
			gfarm_xmlattr_size_limit = GFARM_CONFIG_MISC_DEFAULT;
		}
	} else if (strcmp(s, o = "directory_quota_count_per_user_limit")
	    == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_directory_quota_count_per_user_limit);
	} else if (strcmp(s, o = "directory_quota_check_start_delay")
	    == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_directory_quota_check_start_delay);
	} else if (strcmp(s, o = "directory_quota_check_retry_interval")
	    == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_directory_quota_check_retry_interval);
	} else if (strcmp(s, o = "quota_check_retry_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_quota_check_retry_interval);
	} else if (strcmp(s, o = "max_directory_depth")
	    == 0) {
		e = parse_set_misc_int(p, &gfarm_max_directory_depth);
		if (gfarm_max_directory_depth <
		    GFARM_MAX_DIRECTORY_DEPTH_MINIMUM ||
		    gfarm_max_directory_depth >
		    GFARM_MAX_DIRECTORY_DEPTH_MAXIMUM) {
			gflog_debug(GFARM_MSG_1004758,
			    "max_directory_depth out of range");
			e = GFARM_ERR_NUMERICAL_ARGUMENT_OUT_OF_DOMAIN;
		}
	} else if (strcmp(s, o = "metadb_server_max_descriptors") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_max_descriptors);
	} else if (strcmp(s, o = "metadb_server_stack_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_stack_size);
	} else if (strcmp(s, o = "metadb_server_thread_pool_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_thread_pool_size);
	} else if (strcmp(s, o = "metadb_server_job_queue_length") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_job_queue_length);
	} else if (strcmp(s, o = "metadb_server_remover_queue_length") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_remover_queue_length);
	} else if (strcmp(s,
	    o = "metadb_server_remove_scan_log_interval") == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_metadb_remove_scan_log_interval);
	} else if (strcmp(s,
	    o = "metadb_server_remove_scan_interval_factor") == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_metadb_remove_scan_interval_factor);
	} else if (strcmp(s, o = "metadb_server_heartbeat_interval") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_heartbeat_interval);
	} else if (strcmp(s, o = "failover_notify_delay") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_failover_notify_delay);
	} else if (strcmp(s, o = "metadb_server_dbq_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_metadb_dbq_size);
	} else if (strcmp(s, o = "metadb_server_back_channel_sndbuf_limit")
	    == 0) {
		e = parse_set_sockbuf_limit_int(p,
		    &gfarm_metadb_server_back_channel_sndbuf_limit);
	} else if (strcmp(s, o = "metadb_server_nfs_root_squash_support")
	    == 0) {
		e = parse_set_misc_enabled(p,
		    &gfarm_metadb_server_nfs_root_squash_support);
	} else if (strcmp(s, o = "metadb_server_long_term_lock_type") == 0) {
		e = parse_set_misc_lock_type(p,
		    &gfarm_metadb_server_long_term_lock_type);
	} else if (strcmp(s, o = "metadb_replica_remover_by_host_sleep_time")
	     == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_metadb_replica_remover_by_host_sleep_time);
	} else if (strcmp(s, o = "metadb_replica_remover_by_host_inode_step")
	    == 0) {
		e = parse_set_misc_int(p,
		    &gfarm_metadb_replica_remover_by_host_inode_step);
	} else if (strcmp(s, o = "record_atime") == 0) {
		int record_atime;

		e = parse_set_misc_enabled(p, &record_atime);
		if (!record_atime)
			gfarm_atime_type_set(GFARM_ATIME_DISABLE);
	} else if (strcmp(s, o = "atime") == 0) {
		e = parse_atime_type(p);
	} else if (strcmp(s, o = "max_open_files") == 0) {
		e = parse_set_misc_int(p, &gfarm_max_open_files);
	} else if (strcmp(s, o = "client_digest_check") == 0) {
		e = parse_set_misc_enabled(p,
		    &gfarm_ctxp->client_digest_check);
	} else if (strcmp(s, o = "client_file_bufsize") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->client_file_bufsize);
	} else if (strcmp(s, o = "client_parallel_copy") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->client_parallel_copy);
	} else if (strcmp(s, o = "client_parallel_max") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->client_parallel_max);
	} else if (strcmp(s, o = "profile") == 0) {
		e = parse_profile(p, &gfarm_ctxp->profile);
	} else if (strcmp(s, o = "iostat_gfmd_path") == 0) {
		e = parse_set_var(p, &gfarm_iostat_gfmd_path);
	} else if (strcmp(s, o = "iostat_gfsd_path") == 0) {
		e = parse_set_var(p, &gfarm_iostat_gfsd_path);
	} else if (strcmp(s, o = "iostat_max_client") == 0) {
		e = parse_set_misc_int(p, &gfarm_iostat_max_client);
	} else if (strcmp(s, o = "metadb_replication") == 0) {
		e = parse_set_misc_enabled(p, &metadb_replication_enabled);
	} else if (strcmp(s, o = "metadb_journal_dir") == 0) {
		e = parse_set_var(p, &journal_dir);
	} else if (strcmp(s, o = "metadb_journal_max_size") == 0) {
		e = parse_set_misc_int(p, &journal_max_size);
	} else if (strcmp(s, o = "metadb_journal_recvq_size") == 0) {
		e = parse_set_misc_int(p, &journal_recvq_size);
	} else if (strcmp(s, o = "synchronous_journaling") == 0) {
		e = parse_set_misc_enabled(p, &journal_sync_file);
	} else if (strcmp(s, o = "synchronous_replication_timeout") == 0) {
		e = parse_set_misc_int(p, &journal_sync_slave_timeout);
	} else if (strcmp(s, o = "metadb_server_slave_replication_timeout")
	    == 0) {
		e = parse_set_misc_int(p,
		    &metadb_server_slave_replication_timeout);
	} else if (strcmp(s, o = "metadb_server_slave_max_size") == 0) {
		e = parse_set_misc_int(p, &metadb_server_slave_max_size);
	} else if (strcmp(s, o = "metadb_server_force_slave") == 0) {
		e = parse_set_misc_enabled(p, &metadb_server_force_slave);
	} else if (strcmp(s, o = "network_receive_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->network_receive_timeout);
	} else if (strcmp(s, o = "network_send_timeout") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->network_send_timeout);
	} else if (strcmp(s, o = "file_trace") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_ctxp->file_trace);
	} else if (strcmp(s, o = "debug_command") == 0) {
		e = parse_debug_command(p, &o);
	} else if (strcmp(s, o = "fatal_action") == 0) {
		e = parse_fatal_action(p, &gfarm_ctxp->fatal_action);
		gflog_set_fatal_action(gfarm_ctxp->fatal_action);
	} else if (strcmp(s, o = "ib_rdma") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_ctxp->ib_rdma);
	} else if (strcmp(s, o = "rdma_min_size") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->rdma_min_size);
	} else if (strcmp(s, o = "rdma_port") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->rdma_port);
	} else if (strcmp(s, o = "rdma_gid_index") == 0) {
		e = parse_set_misc_int(p, &gfarm_ctxp->rdma_gid_index);
	} else if (strcmp(s, o = "rdma_device") == 0) {
		e = parse_set_var(p, &gfarm_ctxp->rdma_device);
	} else if (strcmp(s, o = "rdma_mr_reg_mode") == 0) {
		e = parse_rdma_mr_reg_mode(p, &gfarm_ctxp->rdma_mr_reg_mode);
	} else if (strcmp(s, o = "rdma_mr_reg_static_min_size") == 0) {
		e = parse_set_misc_int(p,
			&gfarm_ctxp->rdma_mr_reg_static_min_size);
	} else if (strcmp(s, o = "rdma_mr_reg_static_max_size") == 0) {
		e = parse_set_misc_int(p,
			&gfarm_ctxp->rdma_mr_reg_static_max_size);
	} else if (strcmp(s, o = "replica_check") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_replica_check);
	} else if (strcmp(s, o = "replica_check_remove") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_replica_check_remove);
	} else if (strcmp(s,
		   o = "replica_check_remove_grace_used_space_ratio") == 0) {
		e = parse_set_misc_percentage(
		    p, &gfarm_replica_check_remove_grace_used_space_ratio);
	} else if (strcmp(s, o = "replica_check_remove_grace_time") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_replica_check_remove_grace_time);
	} else if (strcmp(s, o = "replica_check_reduced_log") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_replica_check_reduced_log);
	} else if (strcmp(s, o = "replica_check_host_down_thresh") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_replica_check_host_down_thresh);
	} else if (strcmp(s, o = "replica_check_sleep_time") == 0) {
		e = parse_set_misc_int(p, &gfarm_replica_check_sleep_time);
	} else if (strcmp(s, o = "replica_check_yield_time") == 0) {
		e = parse_set_misc_int(p, &gfarm_replica_check_yield_time);
	} else if (strcmp(s, o = "replica_check_minimum_interval") == 0) {
		e = parse_set_misc_int(
		    p, &gfarm_replica_check_minimum_interval);
	} else if (strcmp(s, o = "replicainfo") == 0) {
		e = parse_set_misc_enabled(p, &gfarm_replicainfo_enabled);
	} else {
		o = s;
		gflog_debug(GFARM_MSG_1000974,
			"Unknown keyword encountered "
			"when parsing one line (%s)",
			s);
		e = GFARM_ERRMSG_UNKNOWN_KEYWORD;
	}
	*op = o;
	return (e);
}

gfarm_error_t
gfarm_config_read_file(FILE *config, int *lineno_p, const char *file)
{
	gfarm_error_t e;
	int lineno = 0;
	char *s, *p, buffer[MAX_CONFIG_LINE_LENGTH + 1];
	const char *o = NULL;

	while (fgets(buffer, sizeof buffer, config) != NULL) {
		lineno++;
		p = buffer;
		e = gfarm_strtoken(&p, &s);

		if (e == GFARM_ERR_NO_ERROR) {
			if (s == NULL) /* blank or comment line */
				continue;
			e = parse_one_line(s, p, GFARM_AUTH_CONFIG_AT_TAIL,
			    file, lineno, &o);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fclose(config);
			*lineno_p = lineno;
			gflog_debug(GFARM_MSG_1000975,
			    "line %d: %s: %s: %s", lineno, o == NULL ? "" : o,
			    p, gfarm_error_string(e));
			return (e);
		}
	}
	fclose(config);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * set default value of configurations.
 */
void
gfarm_config_set_default_ports(void)
{
	if (gfarm_ctxp->metadb_server_name == NULL)
		gflog_fatal(GFARM_MSG_1003864,
		    "metadb_server_host isn't specified in "
		    GFARM_CONFIG " file");

	if (gfarm_ctxp->metadb_server_port == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->metadb_server_port = GFMD_DEFAULT_PORT;
}

static gfarm_error_t
gfarm_config_set_default_metadb_server(void)
{
	gfarm_error_t e;
	struct gfarm_metadb_server *m;
	struct gfarm_filesystem *fs;
	char *host;

	/* gfarm_metadb_server_name is checked in
	 * gfarm_config_set_default_ports */
	assert(gfarm_ctxp->metadb_server_name != NULL);

	if ((fs = gfarm_filesystem_get(
	    gfarm_ctxp->metadb_server_name, gfarm_ctxp->metadb_server_port))
	    != NULL) {
		gfarm_filesystem_set_default(fs);
		return (GFARM_ERR_NO_ERROR);
	}
	if ((host = strdup(gfarm_ctxp->metadb_server_name)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1003433,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_metadb_server_new(&m, host,
	    gfarm_ctxp->metadb_server_port)) != GFARM_ERR_NO_ERROR) {
		free(host);
		gflog_debug(GFARM_MSG_1002556,
		    "%s", gfarm_error_string(e));
		return (e);
	}
	if ((e = gfarm_filesystem_new(&fs)) != GFARM_ERR_NO_ERROR)
		goto error;
	gfarm_filesystem_set_default(fs);
	if ((e = gfarm_filesystem_set_metadb_server_list(fs, &m, 1))
	    != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1002557,
		    "%s", gfarm_error_string(e));
error:
	if (e != GFARM_ERR_NO_ERROR)
		gfarm_metadb_server_free(m);
	return (e);
}

void
gfarm_config_set_default_misc(void)
{
	if (gfarm_ctxp->include_nesting_limit == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->include_nesting_limit =
		    GFARM_CONFIG_INCLUDE_NESTING_LIMIT_DEFAULT;

	if (gfarm_spool_check_level == GFARM_SPOOL_CHECK_LEVEL_DEFAULT)
		(void)gfarm_spool_check_level_set(
			GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND);
	if (gfarm_spool_check_parallel == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_check_parallel =
		    GFARM_SPOOL_CHECK_PARALLEL_DEFAULT;
	if (gfarm_spool_check_parallel_max == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_check_parallel_max =
		    GFARM_SPOOL_CHECK_PARALLEL_MAX_DEFAULT;
	if (gfarm_spool_check_parallel_step == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_check_parallel_step =
		    GFARM_SPOOL_CHECK_PARALLEL_STEP_DEFAULT;
	if (gfarm_spool_check_parallel_per_capacity ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_check_parallel_per_capacity =
		    GFARM_SPOOL_CHECK_PARALLEL_PER_CAPACITY_DEFAULT;
	if (gfarm_spool_base_load == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_base_load = GFARM_SPOOL_BASE_LOAD_DEFAULT;
	if (gfarm_spool_digest_error_check == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_digest_error_check =
		    GFARM_SPOOL_DIGEST_ERROR_CHECK_DEFAULT;
	if (gfarm_write_verify == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_write_verify = GFARM_WRITE_VERIFY_DEFAULT;
	if (gfarm_write_verify_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_write_verify_interval =
		    GFARM_WRITE_VERIFY_INTERVAL_DEFAULT;
	if (gfarm_write_verify_retry_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_write_verify_retry_interval =
		    GFARM_WRITE_VERIFY_RETRY_INTERVAL_DEFAULT;
	if (gfarm_write_verify_log_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_write_verify_log_interval =
		    GFARM_WRITE_VERIFY_LOG_INTERVAL_DEFAULT;

	if (gfarm_spool_server_listen_backlog == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_server_listen_backlog = LISTEN_BACKLOG_DEFAULT;
	if (gfarm_metadb_server_listen_backlog == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_server_listen_backlog = LISTEN_BACKLOG_DEFAULT;

	if (gfarm_spool_server_back_channel_rcvbuf_limit ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_server_back_channel_rcvbuf_limit =
		    GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_DEFAULT;
	if (gfarm_spool_server_read_only_retry_interval ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_spool_server_read_only_retry_interval =
		    GFARM_SPOOL_SERVER_READ_ONLY_RETRY_INTERVAL_DEFAULT;
	if (gfarm_metadb_server_back_channel_sndbuf_limit ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_server_back_channel_sndbuf_limit =
		    GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_DEFAULT;

	if (gfarm_ctxp->log_level == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->log_level = GFARM_DEFAULT_PRIORITY_LEVEL_TO_LOG;
	gflog_set_priority_level(gfarm_ctxp->log_level);
	if (staticp->log_message_verbose == GFARM_CONFIG_MISC_DEFAULT)
		staticp->log_message_verbose =
		    GFARM_LOG_MESSAGE_VERBOSE_DEFAULT;
	gflog_set_message_verbose(staticp->log_message_verbose);

	if (gfarm_ctxp->no_file_system_node_timeout ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->no_file_system_node_timeout =
		    GFARM_NO_FILE_SYSTEM_NODE_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->gfmd_authentication_timeout ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->gfmd_authentication_timeout =
		    GFARM_GFMD_AUTHENTICATION_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->gfmd_reconnection_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->gfmd_reconnection_timeout =
		    GFARM_GFMD_RECONNECTION_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->gfsd_connection_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->gfsd_connection_timeout =
		    GFARM_GFSD_CONNECTION_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->attr_cache_limit == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->attr_cache_limit = GFARM_ATTR_CACHE_LIMIT_DEFAULT;
	if (gfarm_ctxp->attr_cache_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->attr_cache_timeout =
		    GFARM_ATTR_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->page_cache_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->page_cache_timeout =
		    GFARM_PAGE_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->schedule_rpc_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_rpc_timeout =
		    GFARM_SCHEDULE_RPC_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->schedule_cache_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_cache_timeout =
		    GFARM_SCHEDULE_CACHE_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->schedule_concurrency == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_concurrency =
		    GFARM_SCHEDULE_CONCURRENCY_DEFAULT;
	if (gfarm_ctxp->schedule_concurrency_per_net ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_concurrency_per_net =
		    GFARM_SCHEDULE_CONCURRENCY_PER_NET_DEFAULT;
	if (gfarm_ctxp->schedule_idle_load == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_idle_load =
		    GFARM_SCHEDULE_IDLE_LOAD_DEFAULT;
	if (gfarm_ctxp->schedule_busy_load == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_busy_load =
		    GFARM_SCHEDULE_BUSY_LOAD_DEFAULT;
	if (gfarm_ctxp->schedule_virtual_load == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_virtual_load =
		    GFARM_SCHEDULE_VIRTUAL_LOAD_DEFAULT;
	if (gfarm_ctxp->schedule_candidates_ratio == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_candidates_ratio =
		    GFARM_SCHEDULE_CANDIDATES_RATIO_DEFAULT;
	if (gfarm_ctxp->schedule_rtt_thresh_ratio == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_rtt_thresh_ratio =
		    GFARM_SCHEDULE_RTT_THRESH_RATIO_DEFAULT;
	if (gfarm_ctxp->schedule_rtt_thresh_diff == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_rtt_thresh_diff =
		    GFARM_SCHEDULE_RTT_THRESH_DIFF_DEFAULT;
	if (gfarm_ctxp->schedule_write_local_priority ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->schedule_write_local_priority =
		    GFARM_SCHEDULE_WRITE_LOCAL_PRIORITY_DEFAULT;
	if (staticp->minimum_free_disk_space == GFARM_CONFIG_MISC_DEFAULT)
		staticp->minimum_free_disk_space =
		    GFARM_MINIMUM_FREE_DISK_SPACE_DEFAULT;
	if (gfarm_ctxp->direct_local_access == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->direct_local_access =
		    GFARM_DIRECT_LOCAL_ACCESS_DEFAULT;
	if (gfarm_ctxp->replication_at_write_open == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->replication_at_write_open =
		    GFARM_REPLICATION_AT_WRITE_OPEN_DEFAULT;
	if (gfarm_read_only == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_read_only = GFARM_READ_ONLY_DEFAULT;
	if (gfarm_simultaneous_replication_receivers ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_simultaneous_replication_receivers =
		    GFARM_SIMULTANEOUS_REPLICATION_RECEIVERS_DEFAULT;
	if (gfarm_replication_busy_host == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replication_busy_host =
		    GFARM_REPLICATION_BUSY_HOST_DEFAULT;
	if (gfarm_ctxp->gfsd_connection_cache == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->gfsd_connection_cache =
		    GFARM_GFSD_CONNECTION_CACHE_DEFAULT;
	if (gfarm_ctxp->gfmd_connection_cache == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->gfmd_connection_cache =
		    GFARM_GFMD_CONNECTION_CACHE_DEFAULT;
	if (gfarm_xattr_size_limit == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_xattr_size_limit = GFARM_XATTR_SIZE_MAX_DEFAULT;
	if (gfarm_xmlattr_size_limit == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_xmlattr_size_limit = GFARM_XMLATTR_SIZE_MAX_DEFAULT;
	if (gfarm_directory_quota_count_per_user_limit
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_directory_quota_count_per_user_limit =
		    GFARM_DIRECTORY_QUOTA_COUNT_PER_USER_LIMIT_DEFAULT;
	if (gfarm_directory_quota_check_start_delay
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_directory_quota_check_start_delay =
		    GFARM_DIRECTORY_QUOTA_CHECK_START_DELAY_DEFAULT;
	if (gfarm_directory_quota_check_retry_interval
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_directory_quota_check_retry_interval =
		    GFARM_DIRECTORY_QUOTA_CHECK_RETRY_INTERVAL_DEFAULT;
	if (gfarm_quota_check_retry_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_quota_check_retry_interval =
		    GFARM_QUOTA_CHECK_RETRY_INTERVAL_DEFAULT;
	if (gfarm_max_directory_depth == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_max_directory_depth = GFARM_MAX_DIRECTORY_DEPTH_DEFAULT;
	if (gfarm_metadb_max_descriptors == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_max_descriptors =
		    GFARM_METADB_MAX_DESCRIPTORS_DEFAULT;
	if (gfarm_metadb_stack_size == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_stack_size = GFARM_METADB_STACK_SIZE_DEFAULT;
	if (gfarm_metadb_thread_pool_size == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_thread_pool_size =
		    GFARM_METADB_THREAD_POOL_SIZE_DEFAULT;
	if (gfarm_metadb_job_queue_length == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_job_queue_length =
		    GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT;
	if (gfarm_metadb_remover_queue_length == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_remover_queue_length =
		    GFARM_METADB_REMOVER_QUEUE_LENGTH_DEFAULT;
	if (gfarm_metadb_heartbeat_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_heartbeat_interval =
		    GFARM_METADB_HEARTBEAT_INTERVAL_DEFAULT;
	if (gfarm_metadb_failover_notify_delay == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_failover_notify_delay =
		    GFARM_METADB_FAILOVER_NOTIFY_DELAY_DEFAULT;
	if (gfarm_metadb_remove_scan_log_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_remove_scan_log_interval =
		    GFARM_METADB_REMOVE_SCAN_LOG_INTERVAL_DEFAULT;
	if (gfarm_metadb_remove_scan_interval_factor
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_remove_scan_interval_factor =
		    GFARM_METADB_REMOVE_SCAN_INTERVAL_FACTOR_DEFAULT;
	if (gfarm_metadb_dbq_size == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_dbq_size = GFARM_METADB_DBQ_SIZE_DEFAULT;
	if (gfarm_metadb_replica_remover_by_host_sleep_time
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_replica_remover_by_host_sleep_time =
		    GFARM_METADB_REPLICA_REMOVER_BY_HOST_SLEEP_TIME_DEFAULT;
	if (gfarm_metadb_replica_remover_by_host_inode_step
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_replica_remover_by_host_inode_step =
		    GFARM_METADB_REPLICA_REMOVER_BY_HOST_INODE_STEP_DEFAULT;
	if (gfarm_atime_type == GFARM_ATIME_DEFAULT)
		(void)gfarm_atime_type_set(GFARM_ATIME_RELATIVE);
	if (gfarm_max_open_files == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_max_open_files = GFARM_MAX_OPEN_FILES_DEFAULT;
	if (gfarm_ctxp->client_digest_check == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->client_digest_check =
		    GFARM_CLIENT_DIGEST_CHECK_DEFAULT;
	if (gfarm_ctxp->client_file_bufsize == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->client_file_bufsize =
		    GFARM_CLIENT_FILE_BUFSIZE_DEFAULT;
	if (gfarm_ctxp->client_parallel_copy == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->client_parallel_copy =
		    GFARM_CLIENT_PARALLEL_COPY_DEFAULT;
	if (gfarm_ctxp->client_parallel_max == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->client_parallel_max =
		    GFARM_CLIENT_PARALLEL_MAX_DEFAULT;
	if (gfarm_ctxp->profile == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->profile = GFARM_PROFILE_DEFAULT;
	if (metadb_replication_enabled == GFARM_CONFIG_MISC_DEFAULT)
		metadb_replication_enabled =
		    GFARM_METADB_REPLICATION_ENABLED_DEFAULT;
	if (journal_max_size == GFARM_CONFIG_MISC_DEFAULT)
		journal_max_size = GFARM_JOURNAL_MAX_SIZE_DEFAULT;
	if (journal_recvq_size == GFARM_CONFIG_MISC_DEFAULT)
		journal_recvq_size = GFARM_JOURNAL_RECVQ_SIZE_DEFAULT;
	if (journal_sync_file == GFARM_CONFIG_MISC_DEFAULT)
		journal_sync_file = GFARM_JOURNAL_SYNC_FILE_DEFAULT;
	if (journal_sync_slave_timeout == GFARM_CONFIG_MISC_DEFAULT)
		journal_sync_slave_timeout =
		    GFARM_JOURNAL_SYNC_SLAVE_TIMEOUT_DEFAULT;
	if (metadb_server_slave_replication_timeout ==
	    GFARM_CONFIG_MISC_DEFAULT)
		metadb_server_slave_replication_timeout =
		    GFARM_METADB_SERVER_SLAVE_REPLICATION_TIMEOUT_DEFAULT;
	if (metadb_server_slave_max_size == GFARM_CONFIG_MISC_DEFAULT)
		metadb_server_slave_max_size =
		    GFARM_METADB_SERVER_SLAVE_MAX_SIZE_DEFAULT;
	if (metadb_server_force_slave == GFARM_CONFIG_MISC_DEFAULT)
		metadb_server_force_slave =
		    GFARM_METADB_SERVER_FORCE_SLAVE_DEFAULT;
	if (gfarm_metadb_server_nfs_root_squash_support
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_server_nfs_root_squash_support =
		    GFARM_METADB_SERVER_NFS_ROOT_SQUASH_SUPPORT_DEFAULT;
	if (gfarm_metadb_server_long_term_lock_type
	    == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_metadb_server_long_term_lock_type =
		    GFARM_METADB_SERVER_LONG_TERM_LOCK_TYPE_DEFAULT;
	if (gfarm_ctxp->network_receive_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->network_receive_timeout =
		    GFARM_NETWORK_RECEIVE_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->network_send_timeout == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->network_send_timeout =
		    GFARM_NETWORK_SEND_TIMEOUT_DEFAULT;
	if (gfarm_ctxp->file_trace == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->file_trace = GFARM_FILE_TRACE_DEFAULT;
	if (gfarm_ctxp->fatal_action == GFARM_CONFIG_MISC_DEFAULT)
		gflog_set_fatal_action(GFARM_FATAL_ACTION_DEFAULT);
	if (gfarm_ctxp->ib_rdma == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->ib_rdma = GFARM_IB_RDMA_DEFAULT;
	if (gfarm_ctxp->rdma_min_size == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->rdma_min_size = GFARM_RDMA_MIN_SIZE;
	if (gfarm_ctxp->rdma_mr_reg_mode == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->rdma_mr_reg_mode = GFARM_RDMA_REG_MR_DEFAULT;
	if (gfarm_ctxp->rdma_mr_reg_static_max_size ==
				GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->rdma_mr_reg_static_max_size =
				GFARM_RDMA_REG_MAX_SIZE;
	if (gfarm_ctxp->rdma_mr_reg_static_min_size ==
				GFARM_CONFIG_MISC_DEFAULT)
		gfarm_ctxp->rdma_mr_reg_static_min_size =
				GFARM_RDMA_REG_MIN_SIZE;
	if (gfarm_replica_check == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check = GFARM_REPLICA_CHECK_DEFAULT;
	if (gfarm_replica_check_remove == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_remove =
		    GFARM_REPLICA_CHECK_REMOVE_DEFAULT;
	if (gfarm_replica_check_remove_grace_used_space_ratio ==
	    GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_remove_grace_used_space_ratio =
		    GFARM_REPLICA_CHECK_REMOVE_GRACE_USED_SPACE_RATIO_DEFAULT;
	if (gfarm_replica_check_remove_grace_time == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_remove_grace_time =
		    GFARM_REPLICA_CHECK_REMOVE_GRACE_TIME_DEFAULT;
	if (gfarm_replica_check_reduced_log == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_reduced_log =
		    GFARM_REPLICA_CHECK_REDUCED_LOG_DEFAULT;
	if (gfarm_replica_check_host_down_thresh == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_host_down_thresh =
		    GFARM_REPLICA_CHECK_HOST_DOWN_THRESH_DEFAULT;
	if (gfarm_replica_check_sleep_time == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_sleep_time =
		    GFARM_REPLICA_CHECK_SLEEP_TIME_DEFAULT;
	if (gfarm_replica_check_yield_time == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_yield_time =
		    GFARM_REPLICA_CHECK_YIELD_TIME_DEFAULT;
	if (gfarm_replica_check_minimum_interval == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replica_check_minimum_interval =
		    GFARM_REPLICA_CHECK_MINIMUM_INTERVAL_DEFAULT;

	if (gfarm_iostat_max_client == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_iostat_max_client = GFARM_IOSTAT_MAX_CLIENT;

	if (gfarm_replicainfo_enabled == GFARM_CONFIG_MISC_DEFAULT)
		gfarm_replicainfo_enabled = GFARM_REPLICAINFO_ENABLED_DEFAULT;

	gfarm_config_set_default_metadb_server();
}

/*
 * configuration manipulation
 *
 * - gfarm_config_copyin() in gfmd:
 *	gfarm_config_storage -> (*type->copyin)()
 *		-> gfmd configuration
 * - gfarm_config_copyin_default():
 *	gfarm_config_storage
 *		-> checking by (*type->validator)()
 *		-> memory at gfarm_config_addr(), i.e. gfmd configuration
 *
 * - gfarm_config_copyout() in both gfmd and client:
 *	configuration -> (*type->copyout)()
 *		-> gfarm_config_storage
 * - gfarm_config_copyout_default():
 *	 memory at gfarm_config_addr(), i.e. configuration
 *		-> gfarm_config_storage
 *
 * - gfarm_config_client_side_parse_default():
 *	parameters -> parse_one_line()
 *		-> memory at gfarm_config_addr(), i.e. client configuration
 *		-> gfarm_config_storage
 *
 * - gfm_client_config_set_by_string():
 *	parameters -> (*type->client_side_parse)()
 *		-> gfarm_config_storage
 *		-> RPC to gfmd
 *
 * - gfm_client_config_name_to_string():
 *	gfmd configuration -> gfm_client_config_get()
 *		-> gfarm_config_storage -> (*type->printer)()
 *		-> a string
 *
 * - gfarm_config_local_name_to_string():
 *	client configuration -> (*type->copyout)()
 *		-> gfarm_config_storage -> (*type->printer)()
 *		-> a string
 *
 */

struct gfarm_config_type {
	const char *name;
	int target;
	gfarm_error_t (*client_side_parse)(const struct gfarm_config_type *,
		char *, union gfarm_config_storage *storage);
	gfarm_error_t (*copyin)(const struct gfarm_config_type *,
		union gfarm_config_storage *);
	gfarm_error_t (*copyout)(const struct gfarm_config_type *,
		union gfarm_config_storage *);

	char fmt;
	int (*printer)(void *, char *, size_t);
	void (*set_default)(void *);

	/* the followings are only for gfarm_config_*_default() functions */
	int (*validator)(union gfarm_config_storage *);
	void *addr; /* maybe NULL, if it's in gfarm_ctxp-> */
	size_t offset; /* only available if it's in gfarm_ctxp-> */
};

void
gfarm_config_storage_free(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	if (type->fmt == 's') {
		free(storage->s);
		storage->s = NULL;
	}
}

gfarm_error_t
gfarm_config_storage_dup(const struct gfarm_config_type *type,
	union gfarm_config_storage *dst, union gfarm_config_storage *src)
{
	switch (type->fmt) {
	case 'i':
		dst->i = src->i;
		break;
	case 's':
		if (src->s == NULL) {
			dst->s = NULL;
		} else {
			dst->s = strdup(src->s);
			if (dst->s == NULL)
				return (GFARM_ERR_NO_MEMORY);
		}
		break;
	default:
		return (GFARM_ERR_UNKNOWN);
	}
	return (GFARM_ERR_NO_ERROR);
}

static void *
gfarm_config_addr(const struct gfarm_config_type *type)
{
	if (type->addr != NULL)
		return (type->addr);
	if (gfarm_ctxp == NULL)
		return (NULL);
	return ((char *)gfarm_ctxp + type->offset);
}

static gfarm_error_t
gfarm_config_copyin_default(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	void *addr = gfarm_config_addr(type);
	char *t;

	if (addr == NULL)
		return (GFARM_ERR_BAD_ADDRESS);

	if (!(*type->validator)(storage))
		return (GFARM_ERR_INVALID_ARGUMENT);

	switch (type->fmt) {
	case 'i':
		*(int *)addr = storage->i;
		break;
	case 's':
		t = storage->s;

		/* "" means: change the variable to default (NULL) */
		if (t != NULL && *t == '\0')
			t = NULL;

		if (t != NULL) {
			t = strdup(t);
			if (t == NULL)
				return (GFARM_ERR_NO_MEMORY);
		}
		free(*(char **)addr);
		*(char **)addr = t;
		break;
	default:
		return (GFARM_ERR_UNKNOWN);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_config_copyin_parse(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	gfarm_error_t e;
	void *addr = gfarm_config_addr(type);
	const char *o;

	if (type->fmt != 's')
		return (GFARM_ERR_INTERNAL_ERROR); /* shouldn't happen */

	/* addr may be NULL */
	(*type->set_default)(addr);
	e = parse_one_line(type->name, storage->s, GFARM_AUTH_CONFIG_AT_HEAD,
	    NULL, 0, &o);
	return (e);
}

static gfarm_error_t
gfarm_config_copyout_default(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	void *addr = gfarm_config_addr(type);

	if (addr == NULL)
		return (GFARM_ERR_BAD_ADDRESS);

	switch (type->fmt) {
	case 'i':
		storage->i = *(int *)addr;
		break;
	case 's':
		if (*(char **)addr == NULL)
			storage->s = NULL;
		else if ((storage->s = strdup(*(char **)addr)) == NULL)
			return (GFARM_ERR_NO_MEMORY);
		break;
	default:
		return (GFARM_ERR_UNKNOWN);
	}
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_config_copyout_auth(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	assert(type->fmt == 's');

	storage->s = gfarm_auth_config_string_dup();
	if (storage->s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_config_client_side_parse_default(
	const struct gfarm_config_type *type, char *args,
	union gfarm_config_storage *storage)
{
	gfarm_error_t e;
	void *addr = gfarm_config_addr(type);;
	const char *o = NULL;

	/* hack to prevent unwanted gfs_display_timers() output */
	int profile_save = gfarm_ctxp->profile;

	if (addr == NULL)
		return (GFARM_ERR_BAD_ADDRESS);

	(*type->set_default)(addr);
	e = parse_one_line(type->name, args, GFARM_AUTH_CONFIG_AT_HEAD,
	    NULL, 0, &o);
	if (e != GFARM_ERR_NO_ERROR) {
		if (o != NULL) {
			gflog_debug(GFARM_MSG_1005123,
			    "%s %s: %s: %s", type->name, args, o,
			    gfarm_error_string(e));
		} else {
			gflog_debug(GFARM_MSG_1005124,
			    "%s %s: %s", type->name, args,
			    gfarm_error_string(e));
		}
		return (e);
	}
	e = gfarm_config_copyout_default(type, storage);

	/* hack to prevent unwanted gfs_display_timers() output */
	gfarm_ctxp->profile = profile_save;

	return (e);
}

static gfarm_error_t
gfarm_config_client_side_parse_nop(
	const struct gfarm_config_type *type, char *args,
	union gfarm_config_storage *storage)
{
	assert(gfarm_config_type_get_format(type) == 's');

	if ((storage->s = strdup(args)) == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_config_set_default_nop(void *addr)
{
	/* no operation */
}

static int
gfarm_config_print_int(void *addr, char *string, size_t sz)
{
	int *ip = addr;

	return (snprintf(string, sz, "%d", *ip));
}

static void
gfarm_config_set_default_int(void *addr)
{
	int *ip = addr;

	*ip = GFARM_CONFIG_MISC_DEFAULT;
}

#if 0 /* not used */
static int
gfarm_config_validate_true(union gfarm_config_storage *storage)
{
	return (1);
}
#endif

static int
gfarm_config_validate_false(union gfarm_config_storage *storage)
{
	return (0);
}

static int
gfarm_config_validate_positive_int(union gfarm_config_storage *storage)
{
	return (storage->i > 0);
}

static int
gfarm_config_validate_non_negative_int(union gfarm_config_storage *storage)
{
	return (storage->i >= 0);
}

static int
gfarm_config_validate_percentage(union gfarm_config_storage *storage)
{
	return (storage->i >= 0 && storage->i <= 100);
}

static int
gfarm_config_validate_max_directory_depth(union gfarm_config_storage *storage)
{
	/*
	 * allow at least 16 level,
	 * do not allow more than 4096 level.
	 */
	return (GFARM_MAX_DIRECTORY_DEPTH_MINIMUM <= storage->i &&
	    storage->i <= GFARM_MAX_DIRECTORY_DEPTH_MAXIMUM);
}

static int
gfarm_config_print_enabled(void *addr, char *string, size_t sz)
{
	int *enabledp = addr;

	if (*enabledp)
		return (snprintf(string, sz, "%s", "enabled"));
	else
		return (snprintf(string, sz, "%s", "disabled"));
}

static void
gfarm_config_set_default_enabled(void *addr)
{
	int *ip = addr;

	*ip = GFARM_CONFIG_MISC_DEFAULT;
}

static int
gfarm_config_validate_enabled(union gfarm_config_storage *storage)
{
	return (storage->i == 0 || storage->i == 1);
}

static int
gfarm_config_validate_profile(union gfarm_config_storage *storage)
{
	if (!gfarm_config_validate_enabled(storage))
		return (0);

	/* XXX abuse about the role of gfarm_config_type::validator() */
	eval_profile(storage->i);

	return (1);
}

static int
gfarm_config_print_string(void *addr, char *string, size_t sz)
{
	char **sp = addr;

	return (snprintf(string, sz, "%s", *sp));
}

static void
gfarm_config_set_default_string(void *addr)
{
	char **sp = addr;

	*sp = NULL;
}

static int
gfarm_config_validate_digest(union gfarm_config_storage *storage)
{
	if (storage->s == NULL)
		return (1);
	if (*storage->s == '\0')
		return (1);
	return (gfarm_msgdigest_name_verify(storage->s));
}

#define FOR_METADB	GFARM_CONFIG_NAME_FLAG_FOR_METADB
#define FOR_CLIENT	GFARM_CONFIG_NAME_FLAG_FOR_CLIENT

#define CLIENT_PARSE	gfarm_config_client_side_parse_default, \
			gfarm_config_copyin_default, \
			gfarm_config_copyout_default
#define	SERVER_PARSE(copyout) \
			gfarm_config_client_side_parse_nop, \
			gfarm_config_copyin_parse, \
			copyout

#define	INT_IMMUTABLE	'i', \
			gfarm_config_print_int, \
			gfarm_config_set_default_int, \
			gfarm_config_validate_false
#define	INT_POSITIVE	'i', \
			gfarm_config_print_int, \
			gfarm_config_set_default_int, \
			gfarm_config_validate_positive_int
#define	INT_NON_NEGATIVE \
			'i', \
			gfarm_config_print_int, \
			gfarm_config_set_default_int, \
			gfarm_config_validate_non_negative_int
#define	INT_PERCENTAGE	'i', \
			gfarm_config_print_int, \
			gfarm_config_set_default_int, \
			gfarm_config_validate_percentage

#define	TYPE_ENABLED	'i', \
			gfarm_config_print_enabled, \
			gfarm_config_set_default_enabled, \
			gfarm_config_validate_enabled

/*
 * BE CAREFUL to prevent information leak:
 * To add an entry to config_types[] table, you have to review the following
 * functions to prevent information leak in log:
 *	gfarm_config_change_log()
 *	gfarm_config_apply_to_metadb()
 * Maybe we should add a secret flag and these functions should check the flag.
 *
 * In gfmd, giant_lock() or config_var_lock() is needed
 * to access the variables pointed by gfarm_config_addr()
 *
 * NOTE:
 * CLIENT_PARSE/SERVER_PARSE setting shouldn't be changed,
 * because such change introduces protocol incompatibility.
 */

static const struct gfarm_config_type config_types[] = {
	{ "protocol_major",
	  FOR_METADB, CLIENT_PARSE, INT_IMMUTABLE,
	  &gfarm_metadb_version_major, 0 },
	{ "protocol_minor",
	  FOR_METADB, CLIENT_PARSE, INT_IMMUTABLE,
	  &gfarm_metadb_version_minor, 0 },
	{ "protocol_teeny",
	  FOR_METADB, CLIENT_PARSE, INT_IMMUTABLE,
	  &gfarm_metadb_version_teeny, 0 },
	{ "include_nesting_limit",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  NULL, offsetof(struct gfarm_context, include_nesting_limit) },
	{ "auth",
	  FOR_METADB|FOR_CLIENT, SERVER_PARSE(gfarm_config_copyout_auth),
	  's', gfarm_config_print_string, gfarm_config_set_default_nop, NULL,
	  NULL, 0 },
	{ "digest",
	  FOR_METADB, CLIENT_PARSE, 's', gfarm_config_print_string,
	  gfarm_config_set_default_string, gfarm_config_validate_digest,
	  &gfarm_digest, 0 },
	{ "write_verify",
	  FOR_METADB, CLIENT_PARSE, TYPE_ENABLED,
	  &gfarm_write_verify, 0 },
	{ "write_verify_interval",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_write_verify_interval, 0 },
	{ "write_verify_retry_interval",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_write_verify_retry_interval, 0 },
	{ "write_verify_log_interval",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_write_verify_log_interval, 0 },
	{ "direct_local_access",
	  FOR_CLIENT, CLIENT_PARSE, TYPE_ENABLED,
	  NULL, offsetof(struct gfarm_context, direct_local_access) },
	{ "replication_at_write_open",
	  FOR_CLIENT, CLIENT_PARSE, TYPE_ENABLED,
	  NULL, offsetof(struct gfarm_context, replication_at_write_open) },
	{ "read_only",
	  FOR_METADB, CLIENT_PARSE, TYPE_ENABLED,
	  &gfarm_read_only, 0 },
	{ "simultaneous_replication_receivers",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_simultaneous_replication_receivers, 0 },
	{ "client_digest_check",
	  FOR_CLIENT, CLIENT_PARSE, TYPE_ENABLED,
	  NULL, offsetof(struct gfarm_context, client_digest_check) },
	{ "client_file_bufsize",
	  FOR_CLIENT, CLIENT_PARSE, INT_POSITIVE,
	  NULL, offsetof(struct gfarm_context, client_file_bufsize) },
	{ "max_open_files",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_max_open_files, 0 },
	{ "directory_quota_count_per_user_limit",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_directory_quota_count_per_user_limit, 0 },
	{ "directory_quota_check_start_delay",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_directory_quota_check_start_delay, 0 },
	{ "directory_quota_check_retry_interval",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_directory_quota_check_retry_interval, 0 },
	{ "quota_check_retry_interval",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_quota_check_retry_interval, 0 },
	{ "max_directory_depth",
	  FOR_METADB, CLIENT_PARSE,
	  'i', gfarm_config_print_int, gfarm_config_set_default_int,
	  gfarm_config_validate_max_directory_depth,
	  &gfarm_max_directory_depth, 0 },
	{ "metadb_server_remove_scan_log_interval",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_metadb_remove_scan_log_interval, 0 },
	{ "metadb_server_remove_scan_interval_factor",
	  FOR_METADB, CLIENT_PARSE, INT_POSITIVE,
	  &gfarm_metadb_remove_scan_interval_factor, 0 },
	{ "profile",
	  FOR_METADB, CLIENT_PARSE,
	  'i', gfarm_config_print_enabled, gfarm_config_set_default_enabled,
	  gfarm_config_validate_profile,
	  NULL, offsetof(struct gfarm_context, profile) },
	{ "replicainfo",
	  FOR_METADB, CLIENT_PARSE, TYPE_ENABLED,
	  &gfarm_replicainfo_enabled, 0 },
	/*
	 * replica_check, replica_check_remove and replica_check_reduced_log
	 * are treated in replica_check.c to maintain compatibility.
	 */
	{ "replica_check_remove_grace_used_space_ratio",
	  FOR_METADB, CLIENT_PARSE, INT_PERCENTAGE,
	  &gfarm_replica_check_remove_grace_used_space_ratio, 0 },
	{ "replica_check_remove_grace_time",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_replica_check_remove_grace_time, 0 },
	{ "replica_check_host_down_thresh",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_replica_check_host_down_thresh, 0 },
	{ "replica_check_sleep_time",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_replica_check_sleep_time, 0 },
	{ "replica_check_yield_time",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_replica_check_yield_time, 0 },
	{ "replica_check_minimum_interval",
	  FOR_METADB, CLIENT_PARSE, INT_NON_NEGATIVE,
	  &gfarm_replica_check_minimum_interval, 0 },
	{ "replication_busy_host",
	  FOR_METADB, CLIENT_PARSE, TYPE_ENABLED,
	  &gfarm_replication_busy_host, 0 },
};

static gfarm_error_t
gfarm_config_type_by_var(void *var, const struct gfarm_config_type **typep)
{
	int i;

	/* XXX linear search */
	for (i = 0; i < GFARM_ARRAY_LENGTH(config_types); i++) {
		if (var == gfarm_config_addr(&config_types[i])) {
			*typep = &config_types[i];
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
gfarm_config_type_by_name(const char *name,
	const struct gfarm_config_type **typep)
{
	int i;

	/* XXX linear search.  use hash, when config_types[] becomes big */
	for (i = 0; i < GFARM_ARRAY_LENGTH(config_types); i++) {
		if (strcmp(name, config_types[i].name) == 0) {
			*typep = &config_types[i];
			return (GFARM_ERR_NO_ERROR);
		}
	}
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfarm_config_type_by_name_for_metadb(const char *name,
	const struct gfarm_config_type **typep)
{
	gfarm_error_t e;
	const struct gfarm_config_type *type;

	e = gfarm_config_type_by_name(name, &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/* NOTE: gfarm_metadb_config_set_by_string() uses this error code */
	if ((type->target & FOR_METADB) == 0)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	*typep = type;
	return (GFARM_ERR_NO_ERROR);
}

char
gfarm_config_type_get_format(const struct gfarm_config_type *type)
{
	return (type->fmt);
}

int
gfarm_config_type_is_privileged_to_get(const struct gfarm_config_type *type)
{
	return (0); /* currently, privileged config does not exist */
}

gfarm_error_t
gfarm_config_local_name_to_string(const char *name, char *string, size_t sz)
{
	gfarm_error_t e;
	const struct gfarm_config_type *type;
	union gfarm_config_storage storage;
	int len;

	e = gfarm_config_type_by_name(name, &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	if ((type->target & FOR_CLIENT) == 0)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	e = (*type->copyout)(type, &storage);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	len = (*type->printer)(&storage, string, sz);
	if (len == -1)
		return (gfarm_errno_to_error(errno));
	if (len >= sz)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_config_name_foreach(gfarm_error_t (*callback)(void *, const char *),
	void *closure, int flags)
{
	gfarm_error_t e_save = GFARM_ERR_NO_ERROR;
	int i;

	for (i = 0; i < GFARM_ARRAY_LENGTH(config_types); i++) {
		const struct gfarm_config_type *type = &config_types[i];

		if ((type->target & flags) != 0) {
			gfarm_error_t e = callback(closure, type->name);

			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
	}
	return (e_save);
}

gfarm_error_t
gfarm_config_copyin(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	return ((*type->copyin)(type, storage));
}

gfarm_error_t
gfarm_config_copyout(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage)
{
	return ((*type->copyout)(type, storage));
}

void
gfarm_config_log_change(const struct gfarm_config_type *type,
	union gfarm_config_storage *storage,
	const char *user, const char *host)
{
	int len;
	size_t bufsize = 2048;
	char *buffer = malloc(bufsize);
	static const char diag[] = "gfarm_config_log_change";

	if (buffer == NULL) {
		gflog_error(GFARM_MSG_1005125, "%s: no memory for %zd bytes",
		    diag, bufsize);
		gflog_info(GFARM_MSG_1005126,
		    "config changed by (%s@%s): %s: no memory",
		    user, host, type->name);
		return;
	}

	/*
	 * other use of (*type->printer)() refers storage as a result of
	 * (*type->copyout)(), but this (*type->printer)() refers storage
	 * as an input of (*type->copyin)().
	 * with current implmentation, these two storage types are always
	 * same, and same type->printer function works for both.
	 * but be careful about future extension.
	 */
	len = (*type->printer)(storage, buffer, bufsize);
	if (len == -1) {
		gflog_error(GFARM_MSG_1005127,
		    "config changed by (%s@%s) %s: cannot log: %s",
		    user, host, type->name, strerror(errno));
		return;
	}
	if (len >= bufsize) {
		char *b = realloc(buffer, len + 1);

		if (b == NULL) {
			gflog_error(GFARM_MSG_1005128,
			    "%s: no memory for %d bytes", diag, len + 1);
		} else {
			buffer = b;

			len = (*type->printer)(storage, buffer, len + 1);
			if (len == -1) {
				gflog_error(GFARM_MSG_1005129,
				    "config changed by (%s@%s) %s: "
				    "cannot log: %s",
				    user, host, type->name, strerror(errno));
				return;
			}
		}
	}
	gflog_info(GFARM_MSG_1005130, "config changed by (%s@%s): %s %s",
	    user, host, type->name, buffer);
	free(buffer);
}

void
gfarm_config_apply_begin(void)
{
	gfarm_auth_config_set_mark();
}

void
gfarm_config_apply_end(void)
{
}

/* error message will be logged by this function instead of callers  */
gfarm_error_t
gfarm_config_apply_to_metadb(char *directive, char *rest_of_line,
	const char *file, int lineno, int log_changes)
{
	gfarm_error_t e;
	const struct gfarm_config_type *type = NULL;
	void *addr;
	char *rest_of_line_save;
	const char *o = NULL;

	e = gfarm_config_type_by_name_for_metadb(directive, &type);
	if (e != GFARM_ERR_NO_ERROR) {
		if (e == GFARM_ERR_OPERATION_NOT_PERMITTED) {
			gflog_error(GFARM_MSG_1005131,
			    "%s, line %d: %s: not available in gfmd",
			    file, lineno, directive);
		} else {
			gflog_error(GFARM_MSG_1005132,
			    "%s, line %d: %s: %s",
			    file, lineno, directive, gfarm_error_string(e));
		}
		return (e);
	}

	/* parse_one_line() breaks rest_of_line */
	rest_of_line_save = strdup(rest_of_line); /* NULL is OK, see below */

	addr = gfarm_config_addr(type);
	/* addr may be NULL */
	(*type->set_default)(addr);
	e = parse_one_line(directive, rest_of_line, GFARM_AUTH_CONFIG_AT_MARK,
	    file, lineno, &o);

	if (e != GFARM_ERR_NO_ERROR) {
		if (o == NULL) {
			gflog_error(GFARM_MSG_1005133, "%s, line %d: %s",
			    file, lineno, gfarm_error_string(e));
		} else {
			gflog_error(GFARM_MSG_1005134, "%s, line %d: %s: %s",
			    file, lineno, o, gfarm_error_string(e));
		}
	} else if (log_changes) {
		/*
		 * BE CAREFUL to prevent information leak:
		 *
		 * if there is secret information (e.g. password)
		 * in the directive or the rest_of_line above,
		 * we cannot log them.
		 * but currently no such secret thing in config_types[],
		 * we just log them.
		 */
		gflog_info(GFARM_MSG_1005135,
		    "%s, line %d: config set: %s %s",
		    file, lineno, directive,
		    rest_of_line_save != NULL ? rest_of_line_save :
		    rest_of_line /* better than nothing */);
	}
	free(rest_of_line_save);

	return (e);
}

char *
gfarm_config_dirname_add(const char *basename, const char *base_filename)
{
	char *basename_of_base_filename =
	    (char *)gfarm_path_dir_skip(base_filename); /* UNCONST */
	char *p;
	size_t sz;
	int overflow = 0;

	if (basename_of_base_filename == base_filename) /* i.e. no dir */
		return (strdup(basename));

	sz = gfarm_size_add(&overflow, strlen(basename),
	    basename_of_base_filename - base_filename + 1);
	if (overflow)
		return (NULL);
	p = malloc(sz);
	if (p == NULL)
		return (NULL);
	snprintf(p, sz, "%.*s%s",
	    (int)(basename_of_base_filename - base_filename),
	    base_filename, basename);
	return (p);
}

/* XXX - rename all gfarm_strtoken() to gfarm_config_strtoken() */
gfarm_error_t
gfarm_config_strtoken(char **cursorp, char **tokenp)
{
	return (gfarm_strtoken(cursorp, tokenp));
}

gfarm_error_t
gfm_client_config_name_to_string(
	struct gfm_connection *gfm_server, const char *name,
	char *string, size_t sz)
{
	gfarm_error_t e;
	const struct gfarm_config_type *type;
	union gfarm_config_storage storage;
	int len;

	e = gfarm_config_type_by_name(name, &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	e = gfm_client_config_get(gfm_server, type->name, type->fmt, &storage);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	len = (*type->printer)(&storage, string, sz);
	if (len == -1)
		return (gfarm_errno_to_error(errno));
	if (len >= sz)
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_client_config_set_by_string(
	struct gfm_connection *gfm_server, char *string)
{
	gfarm_error_t e;
	const struct gfarm_config_type *type = NULL;
	union gfarm_config_storage storage;
	char *s, *p;

	p = string;
	e = gfarm_strtoken(&p, &s);

	if (e == GFARM_ERR_NO_ERROR) {
		if (s == NULL) /* blank or comment line */
			return (GFARM_ERRMSG_MISSING_ARGUMENT);

		e = gfarm_config_type_by_name_for_metadb(s, &type);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);

		e = (*type->client_side_parse)(type, p, &storage);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1005136,
		    "%s %s: %s", s, p, gfarm_error_string(e));
	} else {
		e = gfm_client_config_set(gfm_server,
		    type->name, type->fmt, &storage);
		gfarm_config_storage_free(type, &storage);
	}

	return (e);
}

gfarm_error_t
gfm_client_config_get_vars_request(struct gfm_connection *gfm_server,
	int n_config_vars, void **config_vars)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	const struct gfarm_config_type *type;
	void *addr;
	int i;

	for (i = 0; i < n_config_vars; i++) {
		e = gfarm_config_type_by_var(config_vars[i], &type);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			continue;
		}
		addr = gfarm_config_addr(type);
		if (addr == NULL) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = GFARM_ERR_BAD_ADDRESS;
			continue;
		}
		e = gfm_client_config_get_request(gfm_server,
		    type->name, type->fmt);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (e_save);
}

gfarm_error_t
gfm_client_config_get_vars_result(struct gfm_connection *gfm_server,
	int n_config_vars, void **config_vars)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	const struct gfarm_config_type *type;
	void *addr;
	int i;

	for (i = 0; i < n_config_vars; i++) {
		e = gfarm_config_type_by_var(config_vars[i], &type);
		if (e != GFARM_ERR_NO_ERROR) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
			continue;
		}
		addr = gfarm_config_addr(type);
		if (addr == NULL) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = GFARM_ERR_BAD_ADDRESS;
			continue;
		}
		e = gfm_client_config_get_result(gfm_server, type->fmt, addr);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	return (e_save);
}



gfarm_error_t
gfarm_sockbuf_apply_limit(int sock, int opt, int limit, const char *optname)
{
	int rv, save_errno, oldval;
	socklen_t vallen = sizeof(oldval);

	if (limit == GFARM_BACK_CHANNEL_SOCKBUF_LIMIT_UNLIMITED)
		return (GFARM_ERR_NO_ERROR);

	rv = getsockopt(sock, SOL_SOCKET, opt, &oldval, &vallen);
	if (rv == -1) {
		save_errno = errno;
		gflog_error_errno(GFARM_MSG_1004306, "%s: getsockopt",
		    optname);
		return (gfarm_errno_to_error(save_errno));
	}
	if (oldval <= limit)
		return (GFARM_ERR_NO_ERROR);

	rv = setsockopt(sock, SOL_SOCKET, opt, &limit, sizeof(limit));
	if (rv != 0) {
		save_errno = errno;
		gflog_error_errno(GFARM_MSG_1004307,
		    "%s: limiting to %d (old: %d)", optname, limit, oldval);
		return (gfarm_errno_to_error(save_errno));
	}
	gflog_info(GFARM_MSG_1004308, "%s: limiting to %d (old: %d)",
	    optname, limit, oldval);
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_display_timers(void)
{
#ifndef __KERNEL__	/*  gfs_display_timers :: profile */
	gfs_pio_display_timers();
	gfs_pio_section_display_timers();
	gfs_pio_local_display_timers();
	gfs_pio_remote_display_timers();
	gfs_stat_display_timers();
	gfs_unlink_display_timers();
	gfs_xattr_display_timers();
#endif /* __KERNEL__ */
}

gfarm_error_t
gfarm_config_profile_value(const char *name, char *value, size_t *sizep)
{
#ifndef __KERNEL__	/*  gfarm_config_profile_value :: profile */
	gfarm_error_t err;

	if ((err = gfs_pio_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_pio_section_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_pio_local_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_pio_remote_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_stat_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_unlink_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	else if ((err = gfs_xattr_profile_value(name, value, sizep)) ==
	    GFARM_ERR_NO_ERROR)
		;
	return (err);
#endif /* __KERNEL__ */
}

struct config_get_set {
	char *name;
	void *addr;
	int settable;
	char type; /* currently compatible with gfp_xdr format type, but... */
};

#ifdef STRTOKEN_TEST
main()
{
	char buffer[MAX_CONFIG_LINE_LENGTH + 1];
	char *cursor, *token, *error;

	while (fgets(buffer, sizeof buffer, stdin) != NULL) {
		cursor = buffer;
		while ((token = strtoken(&cursor, &error)) != NULL)
			printf("token: <%s>\n", token);
		if (error == NULL)
			printf("newline\n");
		else
			printf("error: %s\n", error);
	}
}
#endif

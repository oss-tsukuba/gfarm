#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <stddef.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "context.h"
#include "config.h"
#include "gfs_dir.h"
#include "gfs_dirplusxattr.h"
#include "gfs_dircache.h"
#include "gfs_attrplus.h"

/* #define DIRCACHE_DEBUG */

/*
 * gfs_stat_cache
 */

#define STAT_HASH_SIZE	3079	/* prime number */

struct stat_cache_data {
	struct stat_cache_data *next, *prev; /* doubly linked circular list */
	struct gfarm_hash_entry *entry;
	struct timeval expiration;
	struct gfs_stat st;
	int nattrs;
	char **attrnames;
	void **attrvalues;
	size_t *attrsizes;
};

struct stat_cache {
	/* doubly linked circular list head */
	struct stat_cache_data data_list;
	gfarm_error_t (*stat_op)(const char *path, struct gfs_stat *s);
	struct gfarm_hash_table *table;
	struct timeval lifespan;
	int count;
	int lifespan_is_set;
};

#define STAT_CACHE_DATA_HEAD(c) (&(c)->data_list)
#define FOREACH_STAT_CACHE_DATA(p, c) \
	for (p = (c)->data_list.next; \
		p != STAT_CACHE_DATA_HEAD(c); p = p->next)
#define FOREACH_STAT_CACHE_DATA_SAFE(p, q, c) \
	for (p = (c)->data_list.next, q = p->next; \
		p != STAT_CACHE_DATA_HEAD(c); p = q, q = p->next)

static struct stat_cache stat_cache = {
	{
		STAT_CACHE_DATA_HEAD(&stat_cache),
		STAT_CACHE_DATA_HEAD(&stat_cache),
	},
	gfs_stat
};

static struct stat_cache lstat_cache = {
	{
		STAT_CACHE_DATA_HEAD(&lstat_cache),
		STAT_CACHE_DATA_HEAD(&lstat_cache),
	},
	gfs_lstat
};

static gfarm_error_t
gfs_stat_cache_init0(struct stat_cache *cache)
{
	if (!cache->lifespan_is_set) {
		/* always reflect gfarm_attr_cache_timeout */
		cache->lifespan.tv_sec = gfarm_ctxp->attr_cache_timeout /
		    (GFARM_SECOND_BY_MICROSEC / GFARM_MILLISEC_BY_MICROSEC);
		cache->lifespan.tv_usec = (gfarm_ctxp->attr_cache_timeout -
		    cache->lifespan.tv_sec *
		    (GFARM_SECOND_BY_MICROSEC / GFARM_MILLISEC_BY_MICROSEC)) *
		    GFARM_MILLISEC_BY_MICROSEC;
	}

	if (cache->table != NULL) /* already initialized */
		return (GFARM_ERR_NO_ERROR);

	cache->table = gfarm_hash_table_alloc(
	    STAT_HASH_SIZE, gfarm_hash_default, gfarm_hash_key_equal_default);
	if (cache->table == NULL) {
		gflog_debug(GFARM_MSG_1001282,
			"allocation of stat_cache failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_stat_cache_init(void)
{
	gfarm_error_t e1, e2;

	e1 = gfs_stat_cache_init0(&stat_cache);
	e2 = gfs_stat_cache_init0(&lstat_cache);
	return (e1 != GFARM_ERR_NO_ERROR ? e1 : e2);
}

static void
gfarm_anyptrs_free_deeply(int n, void **values)
{
	int i;

	for (i = 0; i < n; i++)
		free(values[i]);

	free(values);
}

static void
gfs_stat_cache_data_free(struct stat_cache_data *p)
{
	gfs_stat_free(&p->st);
	gfarm_strings_free_deeply(p->nattrs, p->attrnames);
	gfarm_anyptrs_free_deeply(p->nattrs, p->attrvalues);
	free(p->attrsizes);
}

static void
gfs_stat_cache_clear0(struct stat_cache *cache)
{
	struct stat_cache_data *p, *q;
	struct gfarm_hash_entry *entry;

	FOREACH_STAT_CACHE_DATA_SAFE(p, q, cache) {
		gfs_stat_cache_data_free(p);

		entry = p->entry;
		gfarm_hash_purge(cache->table, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
	}
	STAT_CACHE_DATA_HEAD(cache)->next = STAT_CACHE_DATA_HEAD(cache)->prev =
	    STAT_CACHE_DATA_HEAD(cache);
	cache->count = 0;
}

void
gfs_stat_cache_clear(void)
{
	gfs_stat_cache_clear0(&stat_cache);
	gfs_stat_cache_clear0(&lstat_cache);
}

static void
gfs_stat_cache_expire_internal0(struct stat_cache *cache,
	const struct timeval *nowp)
{
	struct stat_cache_data *p, *q;
	struct gfarm_hash_entry *entry;

	FOREACH_STAT_CACHE_DATA_SAFE(p, q, cache) {
		/* assumes monotonic time */
		if (gfarm_timeval_cmp(&p->expiration, nowp) > 0)
			break;

		gfs_stat_cache_data_free(p);

		entry = p->entry;
		gfarm_hash_purge(cache->table, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--cache->count;
	}
	STAT_CACHE_DATA_HEAD(cache)->next = p;
	p->prev = STAT_CACHE_DATA_HEAD(cache);
}

static void
gfs_stat_cache_expire0(struct stat_cache *cache)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	gfs_stat_cache_expire_internal0(cache, &now);
}

void
gfs_stat_cache_expire(void)
{
    gfs_stat_cache_expire0(&stat_cache);
    gfs_stat_cache_expire0(&lstat_cache);
}

static void
gfs_stat_cache_expiration_set0(struct stat_cache *cache,
	long lifespan_millsecond)
{
	struct timeval old_lifespan = cache->lifespan;
	struct stat_cache_data *p;

	cache->lifespan_is_set = 1;
	cache->lifespan.tv_sec = lifespan_millsecond / 1000;
	cache->lifespan.tv_usec =
	    (lifespan_millsecond - cache->lifespan.tv_sec * 1000) * 1000;

	FOREACH_STAT_CACHE_DATA(p, cache) {
		gfarm_timeval_sub(&p->expiration, &old_lifespan);
		gfarm_timeval_add(&p->expiration, &cache->lifespan);
	}
}

void
gfs_stat_cache_expiration_set(long lifespan_millsecond)
{
	gfs_stat_cache_expiration_set0(&stat_cache, lifespan_millsecond);
	gfs_stat_cache_expiration_set0(&lstat_cache, lifespan_millsecond);
}

static gfarm_error_t
attrnames_copy(int nattrs, char ***attrnamesp, char **attrnames)
{
	gfarm_error_t e;
	char **attrs;

	GFARM_MALLOC_ARRAY(attrs, nattrs);
	if (attrs == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = gfarm_fixedstrings_dup(nattrs, attrs, attrnames);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*attrnamesp = attrs;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
attrvalues_copy(int nattrs, void ***attrvaluesp, size_t **attrsizesp,
	    void **attrvalues, size_t *attrsizes)
{
	void **values;
	size_t *sizes;
	int i;

	GFARM_MALLOC_ARRAY(values, nattrs);
	GFARM_MALLOC_ARRAY(sizes, nattrs);
	if (values == NULL || sizes == NULL) {
		free(values);
		free(sizes);
		return (GFARM_ERR_NO_MEMORY);
	}
	for (i = 0; i < nattrs; i++) {
		values[i] = malloc(attrsizes[i]);
		if (values[i] == NULL) {
			while (--i >= 0)
				free(values[i]);

			free(values);
			free(sizes);
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(values[i], attrvalues[i], attrsizes[i]);
		sizes[i] = attrsizes[i];
	}
	*attrvaluesp = values;
	*attrsizesp = sizes;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_stat_cache_enter_internal0(struct stat_cache *cache,
	const char *path, const struct gfs_stat *st,
	int nattrs, char **attrnames, void **attrvalues, size_t *attrsizes,
	const struct timeval *nowp)
{
	gfarm_error_t e, e2, e3;
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;
	int created;

	if (cache->table == NULL) {
		if ((e = gfs_stat_cache_init0(cache)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001283,
				"initialization of stat_cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	gfs_stat_cache_expire_internal0(cache, nowp);

	if (cache->count >= gfarm_ctxp->attr_cache_limit) {
		/* remove the head of the list (i.e. oldest entry) */
		data = STAT_CACHE_DATA_HEAD(cache)->next;
		data->prev->next = data->next;
		data->next->prev = data->prev;
		gfs_stat_cache_data_free(data);
		entry = data->entry;
		gfarm_hash_purge(cache->table, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--cache->count;
	}

	entry = gfarm_hash_enter(cache->table, path, strlen(path) + 1,
	    sizeof(*data), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001284,
			"allocation of hash entry for stat cache failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	data = gfarm_hash_entry_data(entry);
	if (created) {
		++cache->count;
		data->entry = entry;
	} else {
		/* remove from the list, to move this to the end of the list */
		data->prev->next = data->next;
		data->next->prev = data->prev;

		gfs_stat_cache_data_free(data);
	}

	e = gfs_stat_copy(&data->st, st);
	if (nattrs == 0) {
		data->attrnames = NULL;
		data->attrvalues = NULL;
		data->attrsizes = NULL;
		e2 = e3 = GFARM_ERR_NO_ERROR;
	} else {
		e2 = attrnames_copy(nattrs, &data->attrnames, attrnames);
		e3 = attrvalues_copy(nattrs,
		    &data->attrvalues, &data->attrsizes,
		    attrvalues, attrsizes);
	}
	if (e != GFARM_ERR_NO_ERROR ||
	    e2 != GFARM_ERR_NO_ERROR ||
	    e3 != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(cache->table, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--cache->count;
		gflog_debug(GFARM_MSG_1001285,
			"gfs_stat_copy() failed: %s",
			gfarm_error_string(e));
		if (e == GFARM_ERR_NO_ERROR)
			gfs_stat_free(&data->st);
		if (e2 == GFARM_ERR_NO_ERROR)
			gfarm_strings_free_deeply(
			    data->nattrs, data->attrnames);
		if (e3 == GFARM_ERR_NO_ERROR) {
			gfarm_anyptrs_free_deeply(
			    data->nattrs, data->attrvalues);
			free(data->attrsizes);
		}
		return (e != GFARM_ERR_NO_ERROR ? e :
			e2 != GFARM_ERR_NO_ERROR ? e2 : e3);
	}
	data->nattrs = nattrs;

	data->expiration = *nowp;
	gfarm_timeval_add(&data->expiration, &cache->lifespan);
	/* add to the end of the cache list, i.e. assumes monotonic time */
	data->next = STAT_CACHE_DATA_HEAD(cache);
	data->prev = STAT_CACHE_DATA_HEAD(cache)->prev;
	STAT_CACHE_DATA_HEAD(cache)->prev->next = data;
	STAT_CACHE_DATA_HEAD(cache)->prev = data;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_stat_cache_purge0(struct stat_cache *cache, const char *path)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;

	if (cache->table == NULL) /* there is nothing to purge */
		return (GFARM_ERR_NO_ERROR);

	gfs_stat_cache_expire0(cache);
	if (!gfarm_hash_iterator_lookup(
		cache->table, path, strlen(path)+1, &it)) {
#if 0
		gflog_debug(GFARM_MSG_1001286,
			"lookup for path (%s) in stat cache failed: %s",
			path,
			gfarm_error_string(
				GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY));
#endif
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}
	entry = gfarm_hash_iterator_access(&it);
	assert(entry != NULL);
	data = gfarm_hash_entry_data(entry);
	data->prev->next = data->next;
	data->next->prev = data->prev;
	gfs_stat_cache_data_free(data);
	gfarm_hash_iterator_purge(&it);
	--cache->count;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_stat_cache_purge(const char *path)
{
	gfarm_error_t e1, e2;

	/* both cache must be purged, regardless of any error */
	e1 = gfs_stat_cache_purge0(&lstat_cache, path);
	e2 = gfs_stat_cache_purge0(&stat_cache, path);
	/* only if not found in both lstat cache and stat cache */
	if (e1 == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY &&
	    e2 == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
#if 0 /* this is ok with current gfs_stat_cache_purge0() implementation */
	return (GFARM_ERR_NO_ERROR);
#else /* if gfs_stat_cache_purge0() returns other error, this is necessary */
	if (e1 == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		e1 = GFARM_ERR_NO_ERROR;
	if (e2 == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
		e2 = GFARM_ERR_NO_ERROR;
	return (e1 != GFARM_ERR_NO_ERROR ? e1 : e2);
#endif
}

/* this returns uncached result, but enter the result to the cache */
static gfarm_error_t
gfs_getattrplus_caching0(struct stat_cache *cache,
	const char *path, char **patterns, int npatterns,
	struct gfs_stat *st, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	gfarm_error_t e;
	struct timeval now;
	int no_follow = cache == &lstat_cache;

	e = (no_follow ? gfs_lgetattrplus : gfs_getattrplus)
		(path, patterns, npatterns, 0,
		st, nattrsp, attrnamesp, attrvaluesp, attrsizesp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002465, "gfs_getattrplusstat(%s): %s",
		    path, gfarm_error_string(e));
		return (e);
	}

	gettimeofday(&now, NULL);
	if ((e = gfs_stat_cache_enter_internal0(cache, path, st,
	    *nattrsp, *attrnamesp, *attrvaluesp, *attrsizesp, &now)) !=
	    GFARM_ERR_NO_ERROR) {
		/*
		 * It's ok to fail in entering the cache,
		 * since it's merely cache.
		 */
		gflog_warning(GFARM_MSG_1002466,
		    "gfs_getattrplus_caching: failed to cache %s: %s",
		    path, gfarm_error_string(e));
	}

	/** Also cache to stat_cache if the path is not symlink. */
	if (no_follow && !GFARM_S_ISLNK(st->st_mode) &&
	    (e = gfs_stat_cache_enter_internal0(&stat_cache, path, st,
	    *nattrsp, *attrnamesp, *attrvaluesp, *attrsizesp, &now)) !=
	    GFARM_ERR_NO_ERROR) {
		/*
		 * It's ok to fail in entering the cache,
		 * since it's merely cache.
		 */
		gflog_warning(GFARM_MSG_1002654,
		    "gfs_getattrplus_caching: failed to cache %s: %s",
		    path, gfarm_error_string(e));
	}

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_stat_caching0(struct stat_cache *cache, const char *path,
	struct gfs_stat *st)
{
	int nattrs;
	char **attrnames;
	void **attrvalues;
	size_t *attrsizes;

	gfarm_error_t e = gfs_getattrplus_caching0(cache, path,
	    gfarm_xattr_caching_patterns(),
	    gfarm_xattr_caching_patterns_number(),
	    st, &nattrs, &attrnames, &attrvalues, &attrsizes);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gfarm_strings_free_deeply(nattrs, attrnames);
	gfarm_anyptrs_free_deeply(nattrs, attrvalues);
	free(attrsizes);
	return (e);
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_stat_caching(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_caching0(&stat_cache, path, st));
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_lstat_caching(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_caching0(&lstat_cache, path, st));
}

/* this returns uncached result, but enter the result to the cache */
static gfarm_error_t
gfs_getxattr_caching0(struct stat_cache *cache, const char *path,
	const char *name, void *value, size_t *sizep)
{
	struct gfs_stat st;
	int npat, nattrs, i, found = 0;
	char **patterns, **pat;
	char **attrnames;
	void **attrvalues;
	size_t *attrsizes;
	gfarm_error_t e;

	npat = gfarm_xattr_caching_patterns_number();
	GFARM_MALLOC_ARRAY(patterns, npat + 1);
	if (patterns == NULL)
		return (GFARM_ERR_NO_MEMORY);
	pat = gfarm_xattr_caching_patterns();
	for (i = 0; i < npat; i++)
		patterns[i] = pat[i];
	/* XXX FIXME: need to add escape characters for metacharacters */
	patterns[npat] = (char *)name; /* UNCONST */

	e = gfs_getattrplus_caching0(cache, path, patterns, npat + 1,
	    &st, &nattrs, &attrnames, &attrvalues, &attrsizes);
	free(patterns);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < nattrs; i++) {
		if (strcmp(attrnames[i], name) == 0) {
			if (*sizep >= attrsizes[i]) {
				memcpy(value, attrvalues[i], attrsizes[i]);
			} else if (*sizep != 0) {
				gflog_debug(GFARM_MSG_1002467,
				    "gfs_getxattr_caching(%s, %s, size:%d): "
				    "too large result: %d bytes",
				    path, name, (int)*sizep,
				    (int)attrsizes[i]);
				e = GFARM_ERR_RESULT_OUT_OF_RANGE;
			}
			*sizep = attrsizes[i];
			found = 1;
			break;
		}
	}
	if (!found)
		e = GFARM_ERR_NO_SUCH_OBJECT;

	gfs_stat_free(&st);
	gfarm_strings_free_deeply(nattrs, attrnames);
	gfarm_anyptrs_free_deeply(nattrs, attrvalues);
	free(attrsizes);
	return (e);
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_getxattr_caching(const char *path, const char *name,
	void *value, size_t *sizep)
{
	return (gfs_getxattr_caching0(&stat_cache, path, name, value, sizep));
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_lgetxattr_caching(const char *path, const char *name,
	void *value, size_t *sizep)
{
	return (gfs_getxattr_caching0(&lstat_cache, path, name, value, sizep));
}

static gfarm_error_t
gfs_stat_cache_data_get0(struct stat_cache *cache, const char *path,
	struct stat_cache_data **datap)
{
	struct gfarm_hash_entry *entry;
	struct timeval now;

	if (cache->table == NULL) {
		gfarm_error_t e = gfs_stat_cache_init0(cache);

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001288,
				"initialization of stat cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	gettimeofday(&now, NULL);
	gfs_stat_cache_expire_internal0(cache, &now);
	entry = gfarm_hash_lookup(cache->table, path, strlen(path) + 1);
	if (entry != NULL) {
#ifdef DIRCACHE_DEBUG
		gflog_debug(GFARM_MSG_1000092,
		    "%ld.%06ld: gfs_stat_cached(%s): hit (%d)",
		    (long)now.tv_sec, (long)now.tv_usec, path, cache->count);
#endif
		*datap = gfarm_hash_entry_data(entry);
		return (GFARM_ERR_NO_ERROR);
	}
#ifdef DIRCACHE_DEBUG
	gflog_debug(GFARM_MSG_1000093,
	    "%ld.%06ld: gfs_stat_cached(%s): miss (%d)",
	    (long)now.tv_sec, (long)now.tv_usec, path, cache->count);
#endif
	*datap = NULL;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_stat_cached_internal0(struct stat_cache *cache, const char *path,
	struct gfs_stat *st)
{
	struct stat_cache_data *data;
	gfarm_error_t e = gfs_stat_cache_data_get0(cache, path, &data);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (data == NULL) /* not hit */
		return (gfs_stat_caching0(cache, path, st));

	return (gfs_stat_copy(st, &data->st)); /* hit */
}

/* this returns cached result */
gfarm_error_t
gfs_stat_cached_internal(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_cached_internal0(&stat_cache, path, st));
}

/* this returns cached result */
gfarm_error_t
gfs_lstat_cached_internal(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_cached_internal0(&lstat_cache, path, st));
}

/* this returns cached result */
static gfarm_error_t
gfs_getxattr_cached_internal0(struct stat_cache *cache,
	const char *path, const char *name, void *value, size_t *sizep)
{
	struct stat_cache_data *data;
	gfarm_error_t e = gfs_stat_cache_data_get0(cache, path, &data);
	int i, found = 0;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (data == NULL) /* not hit */
		return (gfs_getxattr_caching0(cache, path, name, value,
			sizep));

	/* hit */
	for (i = 0; i < data->nattrs; i++) {
		if (strcmp(data->attrnames[i], name) == 0) {
			if (*sizep >= data->attrsizes[i]) {
				memcpy(value, data->attrvalues[i],
				    data->attrsizes[i]);
			} else if (*sizep != 0) {
				gflog_debug(GFARM_MSG_1002468,
				    "gfs_getxattr_cached_internal"
				    "(%s, %s, size:%d): "
				    "too large result: %d bytes",
				    path, name, (int)*sizep,
				    (int)data->attrsizes[i]);
				e = GFARM_ERR_RESULT_OUT_OF_RANGE;
			}
			*sizep = data->attrsizes[i];
			found = 1;
			break;
		}
	}
	if (!found) {
		if (gfarm_xattr_caching(name)) { /* negative cache */
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else { /* this xattr is uncachable */
			int no_follow = cache == &lstat_cache;

			return ((no_follow ? gfs_lgetxattr : gfs_getxattr)
			    (path, name, value, sizep));
		}
	}
	return (e);
}

/* this returns cached result */
gfarm_error_t
gfs_getxattr_cached_internal(const char *path, const char *name,
	void *value, size_t *sizep)
{
	return (gfs_getxattr_cached_internal0(&stat_cache, path, name,
		value, sizep));
}

gfarm_error_t
gfs_lgetxattr_cached_internal(const char *path, const char *name,
	void *value, size_t *sizep)
{
	return (gfs_getxattr_cached_internal0(&lstat_cache, path, name,
	    value, sizep));
}

/*
 * gfs_opendir_caching()/readdir_caching()/closedir_caching()
 */

#define DIRENTSPLUS_BUFCOUNT	256

struct gfs_dir_caching {
	struct gfs_dir super;

	GFS_DirPlusXAttr dp;
	char *path;
};

static gfarm_error_t
gfs_readdir_caching_internal(GFS_Dir super, struct gfs_dirent **entryp)
{
	struct gfs_dir_caching *dir = (struct gfs_dir_caching *)super;
	struct gfs_dirent *ep;
	struct gfs_stat *stp;
	int nattrs;
	char **attrnames;
	void **attrvalues;
	size_t *attrsizes;
	char *path;
	gfarm_error_t e = gfs_readdirplusxattr(dir->dp,
		&ep, &stp, &nattrs, &attrnames, &attrvalues, &attrsizes);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001289,
			"gfs_readdirplusxattr() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	if (ep != NULL) { /* i.e. not EOF */
		GFARM_MALLOC_ARRAY(path,
		    strlen(dir->path) + strlen(ep->d_name) + 1);
		if (path == NULL) {
			/*
			 * It's ok to fail in entering the cache,
			 * since it's merely cache.
			 */
			gflog_warning(GFARM_MSG_UNUSED,
			    "dircache: failed to cache %s%s due to no memory",
			    dir->path, ep->d_name);
		} else {
			struct timeval now;

			gettimeofday(&now, NULL);
			sprintf(path, "%s%s", dir->path, ep->d_name);
#ifdef DIRCACHE_DEBUG
			gflog_debug(GFARM_MSG_1000094,
			    "%ld.%06ld: gfs_readdir_caching()->"
			    "\"%s\" (%d)",
			    (long)now.tv_sec, (long)now.tv_usec,
			    path, stat_cache.count);
#endif
			/*
			 * It's ok to fail in entering the cache,
			 * since it's merely cache.
			 *
			 * Also cache to stat_cache if the path is not symlink.
			 */
			if ((e = gfs_stat_cache_enter_internal0(
			    &lstat_cache, path,
			    stp, nattrs, attrnames, attrvalues,
			    attrsizes, &now))
			    != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNUSED,
				    "dircache: failed to cache %s: %s",
				    path, gfarm_error_string(e));
			} else if (!GFARM_S_ISLNK(stp->st_mode) &&
			    (e = gfs_stat_cache_enter_internal0(
			    &stat_cache, path,
			    stp, nattrs, attrnames, attrvalues,
			    attrsizes, &now)) != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_UNUSED,
				    "dircache: failed to cache %s: %s",
				    path, gfarm_error_string(e));
			}
			free(path);
		}
	}

	*entryp = ep;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_seekdir_caching_internal(GFS_Dir super, gfarm_off_t off)
{
	struct gfs_dir_caching *dir = (struct gfs_dir_caching *)super;

	return (gfs_seekdirplusxattr(dir->dp, off));
}

static gfarm_error_t
gfs_telldir_caching_internal(GFS_Dir super, gfarm_off_t *offp)
{
	struct gfs_dir_caching *dir = (struct gfs_dir_caching *)super;

	return (gfs_telldirplusxattr(dir->dp, offp));
}

static gfarm_error_t
gfs_closedir_caching_internal(GFS_Dir super)
{
	struct gfs_dir_caching *dir = (struct gfs_dir_caching *)super;
	gfarm_error_t e = gfs_closedirplusxattr(dir->dp);

	free(dir->path);
	free(dir);
	return (e);
}

gfarm_error_t
gfs_opendir_caching_internal(const char *path, GFS_Dir *dirp)
{
	gfarm_error_t e;
	GFS_DirPlusXAttr dp;
	struct gfs_dir_caching *dir;
	char *p;
	static struct gfs_dir_ops ops = {
		gfs_closedir_caching_internal,
		gfs_readdir_caching_internal,
		gfs_seekdir_caching_internal,
		gfs_telldir_caching_internal
	};

	if ((e = gfs_opendirplusxattr(path, &dp)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001290,
			"gfs_opendirplusxattr(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	GFARM_MALLOC(dir);
	if (*gfarm_url_dir_skip(path) != '\0') {
		GFARM_MALLOC_ARRAY(p, strlen(path) + 1 + 1);
		if (p != NULL)
			sprintf(p, "%s/", path);
	} else {
		GFARM_MALLOC_ARRAY(p, strlen(path) + 1);
		if (p != NULL)
			strcpy(p, path);
	}

	if (dir == NULL || p == NULL) {
		gfs_closedirplusxattr(dp);
		if (dir != NULL)
			free(dir);
		if (p != NULL)
			free(p);
		gflog_debug(GFARM_MSG_1001291,
			"allocation of dir or path failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	dir->super.ops = &ops;
	dir->dp = dp;
	dir->path = p;
	*dirp = &dir->super;
	return (GFARM_ERR_NO_ERROR);
}

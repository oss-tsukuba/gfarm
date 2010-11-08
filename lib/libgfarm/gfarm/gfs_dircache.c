#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

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

static struct gfarm_hash_table *stat_cache;

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

/* doubly linked circular list head */
static struct stat_cache_data stat_cache_list_head = {
	&stat_cache_list_head, &stat_cache_list_head
};

static int stat_cache_count;

static struct timeval stat_cache_lifespan;
static int stat_cache_lifespan_is_set;

gfarm_error_t
gfs_stat_cache_init(void)
{
	if (!stat_cache_lifespan_is_set) {
		/* always reflect gfarm_attr_cache_timeout */
		stat_cache_lifespan.tv_sec = gfarm_attr_cache_timeout /
		    (GFARM_SECOND_BY_MICROSEC / GFARM_MILLISEC_BY_MICROSEC);
		stat_cache_lifespan.tv_usec = (gfarm_attr_cache_timeout -
		    stat_cache_lifespan.tv_sec *
		    (GFARM_SECOND_BY_MICROSEC / GFARM_MILLISEC_BY_MICROSEC)) *
		    GFARM_MILLISEC_BY_MICROSEC;
	}

	if (stat_cache != NULL) /* already initialized */
		return (GFARM_ERR_NO_ERROR);

	stat_cache = gfarm_hash_table_alloc(
	    STAT_HASH_SIZE, gfarm_hash_default, gfarm_hash_key_equal_default);
	if (stat_cache == NULL) {
		gflog_debug(GFARM_MSG_1001282,
			"allocation of stat_cache failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

static void
gfarm_anyptrs_free_deeply(int n, void **values)
{
	int i;

	for (i = 0; i < n; i++) {
		free(values[i]);
	}
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

void
gfs_stat_cache_clear(void)
{
	struct stat_cache_data *p, *q;
	struct gfarm_hash_entry *entry;

	for (p = stat_cache_list_head.next; p != &stat_cache_list_head; p = q) {
		q = p->next;
		gfs_stat_cache_data_free(p);

		entry = p->entry;
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
	}
	stat_cache_list_head.next = stat_cache_list_head.prev =
	    &stat_cache_list_head;
	stat_cache_count = 0;
}

static void
gfs_stat_cache_expire_internal(const struct timeval *nowp)
{
	struct stat_cache_data *p, *q;
	struct gfarm_hash_entry *entry;

	for (p = stat_cache_list_head.next; p != &stat_cache_list_head; p = q) {
		/* assumes monotonic time */
		if (gfarm_timeval_cmp(&p->expiration, nowp) > 0)
			break;

		q = p->next;
		gfs_stat_cache_data_free(p);

		entry = p->entry;
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--stat_cache_count;
	}
	stat_cache_list_head.next = p;
	p->prev = &stat_cache_list_head;
}

void
gfs_stat_cache_expire(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	gfs_stat_cache_expire_internal(&now);
}


void
gfs_stat_cache_expiration_set(long lifespan_millsecond)
{
	struct timeval old_lifespan = stat_cache_lifespan;
	struct stat_cache_data *p;

	stat_cache_lifespan_is_set = 1;
	stat_cache_lifespan.tv_sec = lifespan_millsecond / 1000;
	stat_cache_lifespan.tv_usec =
	    (lifespan_millsecond - stat_cache_lifespan.tv_sec * 1000) * 1000;

	for (p = stat_cache_list_head.next; p != &stat_cache_list_head;
	    p = p->next) {
		gfarm_timeval_sub(&p->expiration, &old_lifespan);
		gfarm_timeval_add(&p->expiration, &stat_cache_lifespan);
	}
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
			while (--i >= 0) {
				free(values[i]);
			}
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
gfs_stat_cache_enter_internal(const char *path, const struct gfs_stat *st,
	int nattrs, char **attrnames, void **attrvalues, size_t *attrsizes,
	const struct timeval *nowp)
{
	gfarm_error_t e, e2, e3;
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;
	int created;

	if (stat_cache == NULL) {
		if ((e = gfs_stat_cache_init()) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001283,
				"initialization of stat_cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	gfs_stat_cache_expire_internal(nowp);

	if (stat_cache_count >= gfarm_attr_cache_limit) {
		/* remove the head of the list (i.e. oldest entry) */
		data = stat_cache_list_head.next;
		data->prev->next = data->next;
		data->next->prev = data->prev;
		gfs_stat_cache_data_free(data);
		entry = data->entry;
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--stat_cache_count;
	}

	entry = gfarm_hash_enter(stat_cache, path, strlen(path) + 1,
	    sizeof(*data), &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_1001284,
			"allocation of hash entry for stat cache failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	data = gfarm_hash_entry_data(entry);
	if (created) {
		++stat_cache_count;
		data->entry = entry;
	} else {
		/* remove from the list, to move this to the end of the list */
		data->prev->next = data->next;
		data->next->prev = data->prev;

		gfs_stat_cache_data_free(data);
	}

	e = gfs_stat_copy(&data->st, st);
	e2 = attrnames_copy(nattrs, &data->attrnames, attrnames);
	e3 = attrvalues_copy(nattrs, &data->attrvalues, &data->attrsizes,
	    attrvalues, attrsizes);
	if (e != GFARM_ERR_NO_ERROR ||
	    e2 != GFARM_ERR_NO_ERROR ||
	    e3 != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--stat_cache_count;
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
	gfarm_timeval_add(&data->expiration, &stat_cache_lifespan);
	/* add to the end of the cache list, i.e. assumes monotonic time */
	data->next = &stat_cache_list_head;
	data->prev = stat_cache_list_head.prev;
	stat_cache_list_head.prev->next = data;
	stat_cache_list_head.prev = data;
	return (GFARM_ERR_NO_ERROR);
		
}

gfarm_error_t
gfs_stat_cache_purge(const char *path)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;

	if (stat_cache == NULL) /* there is nothing to purge */
		return (GFARM_ERR_NO_ERROR);

	gfs_stat_cache_expire();
	if (!gfarm_hash_iterator_lookup(
		stat_cache, path, strlen(path)+1, &it)) {
		gflog_debug(GFARM_MSG_1001286,
			"lookup for path (%s) in stat cache failed: %s",
			path,
			gfarm_error_string(
				GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY));
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}
	entry = gfarm_hash_iterator_access(&it);
	assert(entry != NULL);
	data = gfarm_hash_entry_data(entry);
	data->prev->next = data->next;
	data->next->prev = data->prev;
	gfs_stat_cache_data_free(data);
	gfarm_hash_iterator_purge(&it);
	--stat_cache_count;
	return (GFARM_ERR_NO_ERROR);
}

/* this returns uncached result, but enter the result to the cache */
static gfarm_error_t
gfs_getattrplus_caching(const char *path, char **patterns, int npatterns,
	struct gfs_stat *st, int *nattrsp,
	char ***attrnamesp, void ***attrvaluesp, size_t **attrsizesp)
{
	gfarm_error_t e;
	struct timeval now;

	e = gfs_getattrplus(path, patterns, npatterns, 0,
	    st, nattrsp, attrnamesp, attrvaluesp, attrsizesp);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "gfs_getattrplusstat(%s): %s",
		    path, gfarm_error_string(e));
		return (e);
	}

	gettimeofday(&now, NULL);
	if ((e = gfs_stat_cache_enter_internal(path, st, *nattrsp,
	    *attrnamesp, *attrvaluesp, *attrsizesp, &now)) !=
	    GFARM_ERR_NO_ERROR) {
		/*
		 * It's ok to fail in entering the cache,
		 * since it's merely cache.
		 */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "gfs_getattrplus_caching: failed to cache %s: %s",
		    path, gfarm_error_string(e));
	}
	return (GFARM_ERR_NO_ERROR);
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_stat_caching(const char *path, struct gfs_stat *st)
{
	int nattrs;
	char **attrnames;
	void **attrvalues;
	size_t *attrsizes;
	gfarm_error_t e = gfs_getattrplus_caching(path,
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
gfs_lstat_caching(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_caching(path, st)); /* XXX FIXME */
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_getxattr_caching(const char *path, const char *name,
	void *value, size_t *sizep)
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

	e = gfs_getattrplus_caching(path, patterns, npat + 1,
	    &st, &nattrs, &attrnames, &attrvalues, &attrsizes);
	free(patterns);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < nattrs; i++) {
		if (strcmp(attrnames[i], name) == 0) {
			if (*sizep >= attrsizes[i]) {
				memcpy(value, attrvalues[i], attrsizes[i]);
			} else {
				gflog_debug(GFARM_MSG_UNFIXED,
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

gfarm_error_t
gfs_stat_cache_data_get(const char *path, struct stat_cache_data **datap)
{
	struct gfarm_hash_entry *entry;
	struct timeval now;

	if (stat_cache == NULL) {
		gfarm_error_t e = gfs_stat_cache_init();

		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001288,
				"initialization of stat cache failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	}
	gettimeofday(&now, NULL);
	gfs_stat_cache_expire_internal(&now);
	entry = gfarm_hash_lookup(stat_cache, path, strlen(path) + 1);
	if (entry != NULL) {
#ifdef DIRCACHE_DEBUG
		gflog_debug(GFARM_MSG_1000092,
		    "%ld.%06ld: gfs_stat_cached(%s): hit (%d)",
		    (long)now.tv_sec,(long)now.tv_usec, path,stat_cache_count);
#endif
		*datap = gfarm_hash_entry_data(entry);
		return (GFARM_ERR_NO_ERROR);
	}
#ifdef DIRCACHE_DEBUG
	gflog_debug(GFARM_MSG_1000093,
	    "%ld.%06ld: gfs_stat_cached(%s): miss (%d)",
	    (long)now.tv_sec, (long)now.tv_usec, path, stat_cache_count);
#endif
	*datap = NULL;
	return (GFARM_ERR_NO_ERROR);
}

/* this returns cached result */
gfarm_error_t
gfs_stat_cached_internal(const char *path, struct gfs_stat *st)
{
	struct stat_cache_data *data;
	gfarm_error_t e = gfs_stat_cache_data_get(path, &data);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (data == NULL) /* not hit */
		return (gfs_stat_caching(path, st));

	return (gfs_stat_copy(st, &data->st)); /* hit */
}

/* this returns cached result */
gfarm_error_t
gfs_getxattr_cached_internal(const char *path, const char *name,
	void *value, size_t *sizep)
{
	struct stat_cache_data *data;
	gfarm_error_t e = gfs_stat_cache_data_get(path, &data);
	int i, found = 0;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (data == NULL) /* not hit */
		return (gfs_getxattr_caching(path, name, value, sizep));

	/* hit */
	for (i = 0; i < data->nattrs; i++) {
		if (strcmp(data->attrnames[i], name) == 0) {
			if (*sizep >= data->attrsizes[i]) {
				memcpy(value, data->attrvalues[i],
				    data->attrsizes[i]);
			} else {
				gflog_debug(GFARM_MSG_UNFIXED,
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
			return (gfs_getxattr(path, name, value, sizep));
		}
	}
	return (e);
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
			    path, stat_cache_count);
#endif
			/*
			 * It's ok to fail in entering the cache,
			 * since it's merely cache.
			 */
			if ((e = gfs_stat_cache_enter_internal(path, stp,
			    nattrs, attrnames, attrvalues, attrsizes, &now))
			    != GFARM_ERR_NO_ERROR) {
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
		gfs_seekdir_unimpl,
		gfs_telldir_unimpl
	};

	if ((e = gfs_opendirplusxattr(path, &dp)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001290,
			"gfs_opendirplusxattr(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	GFARM_MALLOC(dir);
	if (*gfarm_path_dir_skip(path) != '\0') {
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

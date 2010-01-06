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
#include "gfs_dircache.h"

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
	if (stat_cache == NULL)
		return (GFARM_ERR_NO_MEMORY);

	return (GFARM_ERR_NO_ERROR);
}

void
gfs_stat_cache_clear(void)
{
	struct stat_cache_data *p, *q;
	struct gfarm_hash_entry *entry;

	for (p = stat_cache_list_head.next; p != &stat_cache_list_head; p = q) {
		q = p->next;
		gfs_stat_free(&p->st);

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
		gfs_stat_free(&p->st);

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
gfs_stat_cache_enter_internal(const char *path, const struct gfs_stat *st,
	const struct timeval *nowp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;
	int created;

	if (stat_cache == NULL) {
		if ((e = gfs_stat_cache_init()) != GFARM_ERR_NO_ERROR)
			return (e);
	}
	gfs_stat_cache_expire_internal(nowp);

	if (stat_cache_count >= gfarm_attr_cache_limit) {
		/* remove the head of the list (i.e. oldest entry) */
		data = stat_cache_list_head.next;
		data->prev->next = data->next;
		data->next->prev = data->prev;
		gfs_stat_free(&data->st);
		entry = data->entry;
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--stat_cache_count;
	}

	entry = gfarm_hash_enter(stat_cache, path, strlen(path) + 1,
	    sizeof(*data), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);

	data = gfarm_hash_entry_data(entry);
	if (created) {
		++stat_cache_count;
		data->entry = entry;
	} else {
		/* remove from the list, to move this to the end of the list */
		data->prev->next = data->next;
		data->next->prev = data->prev;

		gfs_stat_free(&data->st);
	}
	e = gfs_stat_copy(&data->st, st);
	if (e != GFARM_ERR_NO_ERROR) {
		gfarm_hash_purge(stat_cache, gfarm_hash_entry_key(entry),
		    gfarm_hash_entry_key_length(entry));
		--stat_cache_count;
		return (e);
	}
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
	if (!gfarm_hash_iterator_lookup(stat_cache, path, strlen(path)+1, &it))
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	entry = gfarm_hash_iterator_access(&it);
	assert(entry != NULL);
	data = gfarm_hash_entry_data(entry);
	data->prev->next = data->next;
	data->next->prev = data->prev;
	gfs_stat_free(&data->st);
	gfarm_hash_iterator_purge(&it);
	--stat_cache_count;
	return (GFARM_ERR_NO_ERROR);
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_stat_caching(const char *path, struct gfs_stat *st)
{
	gfarm_error_t e;
	struct timeval now;

	e = gfs_stat(path, st);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gettimeofday(&now, NULL);
	/* It's ok to fail in entering the cache, since it's merely cache. */
	(void)gfs_stat_cache_enter_internal(path, st, &now);
	return (GFARM_ERR_NO_ERROR);
}

/* this returns uncached result, but enter the result to the cache */
gfarm_error_t
gfs_lstat_caching(const char *path, struct gfs_stat *st)
{
	return (gfs_stat_caching(path, st)); /* XXX FIXME */
}

/* this returns cached result */
gfarm_error_t
gfs_stat_cached_internal(const char *path, struct gfs_stat *st)
{
	struct gfarm_hash_entry *entry;
	struct stat_cache_data *data;
	struct timeval now;

	if (stat_cache == NULL) {
		gfarm_error_t e = gfs_stat_cache_init();

		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	gettimeofday(&now, NULL);
	gfs_stat_cache_expire_internal(&now);
	entry = gfarm_hash_lookup(stat_cache, path, strlen(path) + 1);
	if (entry != NULL) {
#ifdef DIRCACHE_DEBUG
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%ld.%06ld: gfs_stat_cached(%s): hit (%d)",
		    (long)now.tv_sec,(long)now.tv_usec, path,stat_cache_count);
#endif
		data = gfarm_hash_entry_data(entry);
		return (gfs_stat_copy(st, &data->st));
	}
#ifdef DIRCACHE_DEBUG
	gflog_debug(GFARM_MSG_UNFIXED,
	    "%ld.%06ld: gfs_stat_cached(%s): miss (%d)",
	    (long)now.tv_sec, (long)now.tv_usec, path, stat_cache_count);
#endif
	return (gfs_stat_caching(path, st));
}

/*
 * gfs_opendir_caching()/readdir_caching()/closedir_caching()
 */

#define DIRENTSPLUS_BUFCOUNT	256

struct gfs_dir_caching {
	struct gfs_dir super;

	GFS_DirPlus dp;
	char *path;
};

static gfarm_error_t
gfs_readdir_caching_internal(GFS_Dir super, struct gfs_dirent **entryp)
{
	struct gfs_dir_caching *dir = (struct gfs_dir_caching *)super;
	struct gfs_dirent *ep;
	struct gfs_stat *stp;
	char *path;
	gfarm_error_t e = gfs_readdirplus(dir->dp, &ep, &stp);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (ep != NULL) { /* i.e. not EOF */
		GFARM_MALLOC_ARRAY(path,
		    strlen(dir->path) + strlen(ep->d_name) + 1);
		if (path != NULL) {
			struct timeval now;

			gettimeofday(&now, NULL);
			sprintf(path, "%s%s", dir->path, ep->d_name);
#ifdef DIRCACHE_DEBUG
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%ld.%06ld: gfs_readdir_caching()->"
			    "\"%s\" (%d)",
			    (long)now.tv_sec, (long)now.tv_usec,
			    path, stat_cache_count);
#endif
			/*
			 * It's ok to fail in entering the cache,
			 * since it's merely cache.
			 */
			(void)gfs_stat_cache_enter_internal(path, stp, &now);
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
	gfarm_error_t e = gfs_closedirplus(dir->dp);

	free(dir->path);
	free(dir);
	return (e);
}

gfarm_error_t
gfs_opendir_caching_internal(const char *path, GFS_Dir *dirp)
{
	gfarm_error_t e;
	GFS_DirPlus dp;
	struct gfs_dir_caching *dir;
	char *p;
	static struct gfs_dir_ops ops = {
		gfs_closedir_caching_internal,
		gfs_readdir_caching_internal,
		gfs_seekdir_unimpl,
		gfs_telldir_unimpl
	};

	if ((e = gfs_opendirplus(path, &dp)) != GFARM_ERR_NO_ERROR)
		return (e);

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
		gfs_closedirplus(dp);
		if (dir != NULL)
			free(dir);
		if (p != NULL)
			free(p);
		return (GFARM_ERR_NO_MEMORY);
	}

	dir->super.ops = &ops;
	dir->dp = dp;
	dir->path = p;
	*dirp = &dir->super;
	return (GFARM_ERR_NO_ERROR);
}

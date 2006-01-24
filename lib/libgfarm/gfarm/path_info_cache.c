/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <gfarm/gfarm.h>
#include "hash.h"
#include "gfutil.h"
#include "metadb_access.h"

/* #define DEBUG */
#include <stdio.h>
#ifdef DEBUG
#define _debug printf
#else
#define _debug 1 ? (void)0: printf
#endif

/* default parameters */
static int hash_size = 1009; /* prime */
static struct timeval cache_timeout = {0, 0}; /* disable */
static struct gfarm_timespec update_time_interval = {0, 0};

static int current_cache_num;
static int prepare_cache_table = 0;
static struct gfarm_hash_table *cache_table;

#define CACHE_NOSET  0
#define CACHE_NOENT  1
#define CACHE_SET    2

struct path_info_cache {
	int noent;   /* CACHE_* */
	struct gfarm_path_info info;
	struct timeval time;
	int set_size;
};

static char *
gfs_stat_dup(struct gfs_stat *src, struct gfs_stat *dest)
{
	*dest = *src;

	if (src->st_user != NULL) {
		dest->st_user = strdup(src->st_user);
		if (dest->st_user == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}
	if (src->st_group != NULL) {
		dest->st_group = strdup(src->st_group);
		if (dest->st_group == NULL) {
			if (src->st_user != NULL)
				free(dest->st_user);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	return (NULL);
}

static char *
gfarm_path_info_dup(struct gfarm_path_info *src, struct gfarm_path_info *dest)
{
	char *e;

	e = gfs_stat_dup(&src->status, &dest->status);
	if (e != NULL)
		return (e);
	if (src->pathname != NULL) {
		dest->pathname = strdup(src->pathname);
		if (dest->pathname == NULL) {
			gfs_stat_free(&dest->status);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	return (NULL);
}

static void
cache_hash_size_set(unsigned int size)
{
	if (size > 0)
		hash_size = size;
}

static void
cache_timeout_set(unsigned int timeout)
{
	if (timeout >= 0) {
		/* millisec -> microsec */
		cache_timeout.tv_sec = timeout / 1000;
		cache_timeout.tv_usec
			= (timeout % 1000) * 1000;
	}
}

static void
cache_update_time_interval_set(unsigned int interval)
{
	if (interval >= 0) {
		/* millisec -> nanosec */
		update_time_interval.tv_sec = interval / 1000;
		update_time_interval.tv_nsec
			= (interval % 1000) * 1000000;
	}
}

static int
cache_path_info_init()
{
	static int env_init = 0;

	if (env_init == 0) {
		char *envval;

		envval = getenv("GFARM_PATH_INFO_HASH_SIZE");
		if (envval != NULL)
			cache_hash_size_set(atoi(envval));

		/* millisecond */
		envval = getenv("GFARM_PATH_INFO_TIMEOUT");
		if (envval != NULL)
			cache_timeout_set(atoi(envval));

		/* millisecond */
		envval = getenv("GFARM_UPDATE_TIME_INTERVAL");
		if (envval != NULL)
			cache_update_time_interval_set(atoi(envval));

		env_init = 1;
	}

	if (cache_timeout.tv_sec == 0 && cache_timeout.tv_usec == 0)
		return (0); /* disable */

	if (prepare_cache_table == 1)
		return (1); /* enable */

	cache_table = gfarm_hash_table_alloc(hash_size,
					     gfarm_hash_default,
					     gfarm_hash_key_equal_default);
	prepare_cache_table = 1;
	current_cache_num = 0;
	_debug("! cache_path_info_init: hash_size=%d\n", hash_size);
	_debug("! path_info_timeout=%u.%u(sec.microsec)\n",
	       (unsigned int) cache_timeout.tv_sec,
	       (unsigned int) cache_timeout.tv_usec);
	_debug("! update_time_interval=%u.%u(sec.nanosec)\n",
	       (unsigned int) update_time_interval.tv_sec,
	       (unsigned int) update_time_interval.tv_nsec);

	return (1); /* enable */
}

#include <limits.h> /* PATH_MAX */

static void
cache_path_info_free()
{
	struct gfarm_hash_iterator iterator;
	struct gfarm_hash_entry *he;
	struct path_info_cache *pic;
#ifdef DEBUG
	char *key;
	char path[PATH_MAX];
#endif
	gfarm_hash_iterator_begin(cache_table, &iterator);
	while (1) {
		he = gfarm_hash_iterator_access(&iterator);
		if (he == NULL)
		        break;
		pic = gfarm_hash_entry_data(he);
#ifdef DEBUG
		key = gfarm_hash_entry_key(he);
		memset(path, 0, PATH_MAX);
		memcpy(path, key, gfarm_hash_entry_key_length(he));
		_debug("! free path_info cache: %d: %s\n", pic->noent, path);
#endif
		if (pic->noent == CACHE_SET)
			gfarm_path_info_free(&pic->info);
		gfarm_hash_iterator_next(&iterator);
	}
	/* ?? gfarm_hash_iterator_purge(&iterator); */

	gfarm_hash_table_free(cache_table);
	prepare_cache_table = 0;
}

static char GFARM_PATH_INFO_CACHE_CANCEL[] = "disable path_info cache";

static char *
cache_path_info_get(const char *pathname, struct gfarm_path_info *info)
{
	struct gfarm_hash_entry *he;
	int pathlen;
	struct path_info_cache *pic;
	struct timeval now;

	if (!cache_path_info_init())
		return (GFARM_PATH_INFO_CACHE_CANCEL);

	pathlen = strlen(pathname);
	he = gfarm_hash_lookup(cache_table, pathname, pathlen);
	if (he != NULL) {
		pic = gfarm_hash_entry_data(he);
		if (pic != NULL) {
			/* check term of validity */
			gettimeofday(&now, NULL);
			gfarm_timeval_sub(&now, &pic->time);
			if (gfarm_timeval_cmp(&now, &cache_timeout) >= 0) {
				_debug("! expire path_info cache: %s\n",
				       pathname);
#if 1  /* purge */
				if (pic->noent == CACHE_SET)
					gfarm_path_info_free(&pic->info);
				if (gfarm_hash_purge(cache_table, pathname,
						     strlen(pathname)))
					current_cache_num--;
#endif
				return "expired path_info cache content";
			}
			_debug("! use path_info cache: %s\n", pathname);
			if (pic->noent == CACHE_NOENT) /* NOENT cache */
				return (GFARM_ERR_NO_SUCH_OBJECT);

			return gfarm_path_info_dup(&pic->info, info);
		}
	}
	return "cache_path_info_get: no path_info cache";
}

static char *
cache_path_info_put(const char *pathname, struct gfarm_path_info *info)
{
	struct gfarm_hash_entry *he;
	int pathlen;
	int created;
	struct path_info_cache *pic;

	if (!cache_path_info_init())
		return (GFARM_PATH_INFO_CACHE_CANCEL);

	if (current_cache_num >= hash_size) {
		cache_path_info_free();  /* clear all cache */
		if (!cache_path_info_init())
			return (GFARM_PATH_INFO_CACHE_CANCEL);
	}

	pathlen = strlen(pathname);
	
	/* set cache */
	he = gfarm_hash_enter(cache_table, pathname, pathlen,
			      sizeof(struct path_info_cache), &created);
	if (he == NULL) {
		_debug("! cache_path_info_put: no memory\n");
		return (GFARM_ERR_NO_MEMORY);
	}
	pic = gfarm_hash_entry_data(he);
	_debug("! put path_info cache: %s\n", pathname);
	if (created)  /* new cache */
		current_cache_num++;
	else if (pic->noent == CACHE_SET)  /* have path_info */
		gfarm_path_info_free(&pic->info);

	if (info == NULL) {  /* set NOENT */
		pic->noent = CACHE_NOENT;
		_debug("! -> set NOENT: %s\n", pathname);
	}
	else {
#ifdef DEBUG
		if (pic->noent == CACHE_NOENT) {
			_debug("! -> update cache from NOENT: %s\n", pathname);
		}
#endif
		(void)gfarm_path_info_dup(info, &pic->info);
		pic->noent = CACHE_SET;
	}
	/* current time */
	gettimeofday(&pic->time, NULL);

	return (NULL);
}

static char *
cache_path_info_remove(const char *pathname)
{
	struct gfarm_hash_entry *he;
	int pathlen;
	struct path_info_cache *pic;

	if (!cache_path_info_init())
		return (GFARM_PATH_INFO_CACHE_CANCEL);

	pathlen = strlen(pathname);
	he = gfarm_hash_lookup(cache_table, pathname, pathlen);
	if (he != NULL) {
		pic = gfarm_hash_entry_data(he);
		if (pic != NULL) {
			if (pic->noent == CACHE_SET)
				gfarm_path_info_free(&pic->info);
			if (gfarm_hash_purge(cache_table, pathname, pathlen)) {
				_debug("! remove path_info cache: %s\n",
				       pathname);
				current_cache_num--;
				return (NULL);
			}
		}
	}
	return "cache_path_info_remove: no path_info cache";
}

/**********************************************************************/

int
gfarm_timespec_cmp(struct gfarm_timespec *t1, struct gfarm_timespec *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return (1);
	if (t1->tv_sec < t2->tv_sec)
		return (-1);
	if (t1->tv_nsec > t2->tv_nsec)
		return (1);
	if (t1->tv_nsec < t2->tv_nsec)
		return (-1);
	return (0);
}

/* #define GFARM_MILLISEC_BY_NANOSEC 1000000 */
#define GFARM_SECOND_BY_NANOSEC  1000000000

static void
gfarm_timespec_normalize(struct gfarm_timespec *t)
{
	long n;

	if (t->tv_nsec >= GFARM_SECOND_BY_NANOSEC) {
		n = t->tv_nsec / GFARM_SECOND_BY_NANOSEC;
		t->tv_nsec -= n * GFARM_SECOND_BY_NANOSEC;
		t->tv_sec += n;
	}
	else if (t->tv_nsec < 0) {
		n = -t->tv_nsec / GFARM_SECOND_BY_NANOSEC + 1;
		t->tv_nsec += n * GFARM_SECOND_BY_NANOSEC;
		t->tv_sec -= n;
	}
}

void
gfarm_timespec_add(struct gfarm_timespec *t1, const struct gfarm_timespec *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_nsec += t2->tv_nsec;
	gfarm_timespec_normalize(t1);
}

void
gfarm_timespec_sub(struct gfarm_timespec *t1, const struct gfarm_timespec *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	gfarm_timespec_normalize(t1);
}

void
gfarm_timespec_add_microsec(struct gfarm_timespec *t, long microsec)
{
	t->tv_nsec += microsec * 1000;
	gfarm_timespec_normalize(t);
}

/**********************************************************************/

static int
compare_path_info_except_time(struct gfarm_path_info *info1,
			      struct gfarm_path_info *info2)
{
	struct gfs_stat *s1 = &info1->status, *s2 = &info2->status;

#if 0  /* for debug */
	_debug("mtime 1: %u %u\n",
	       s1->st_mtimespec.tv_sec, s1->st_mtimespec.tv_nsec);
	_debug("mtime 2: %u %u\n",
	       s2->st_mtimespec.tv_sec, s2->st_mtimespec.tv_nsec);
	_debug("atime 1: %u %u\n",
	       s1->st_atimespec.tv_sec, s1->st_atimespec.tv_nsec);
	_debug("atime 2: %u %u\n",
	       s2->st_atimespec.tv_sec, s2->st_atimespec.tv_nsec);
	_debug("ctime 1: %u %u\n",
	       s1->st_ctimespec.tv_sec, s1->st_ctimespec.tv_nsec);
	_debug("ctime 1: %u %u\n",
	       s2->st_ctimespec.tv_sec, s2->st_ctimespec.tv_nsec);
#endif

#if 1
	if (s1->st_ino == s2->st_ino &&
	    s1->st_mode == s2->st_mode &&
	    s1->st_size == s2->st_size &&
	    s1->st_nsections == s2->st_nsections &&
	    strcmp(s1->st_user, s2->st_user) == 0
      /* && strcmp(s1->st_group, s2->st_group) == 0 */
		)
		return (0); /* equal */
	else
		return (1); /* different */
#else  /* for debug */
	{
		int ok = 0;

		if (s1->st_ino != s2->st_ino) {
			_debug("different st_ino: %d %d\n",
			       (int)s1->st_ino, (int)s2->st_ino);
			ok = 1;
		}
		if (s1->st_mode != s2->st_mode) {
			_debug("different st_mode: %d %d\n",
			       (int)s1->st_mode, (int)s2->st_mode);
			ok = 1;
		}
		if (s1->st_size != s2->st_size) {
			_debug("different st_size: %lu %lu\n",
			       (unsigned long)s1->st_size,
			       (unsigned long)s2->st_size);
			ok = 1;
		}
		if (s1->st_nsections != s2->st_nsections) {
			_debug("different st_nsections: %d %d\n",
			       (int)s1->st_nsections, (int)s2->st_nsections);
			ok = 1;
		}
		if (strcmp(s1->st_user, s2->st_user) != 0) {
			_debug("different st_user: %s %s\n",
			       s1->st_user, s2->st_user);
			ok = 1;
		}
		/* if (strcmp(s1->st_group, s2->st_group) != 0) */
		
		return (ok);
	}
#endif
}

static int
check_update_time_interval(const char *pathname, struct gfarm_path_info *info)
{
	char *e;
	struct gfarm_path_info nowinfo;
	struct gfarm_timespec tmp;
	struct gfarm_timespec zero = {0, 0};

	e = cache_path_info_get(pathname, &nowinfo);
        if (e != NULL)
		return (0); /* need updating */

	while (compare_path_info_except_time(info, &nowinfo) == 0) {
#if 1
		tmp = info->status.st_mtimespec;
		gfarm_timespec_sub(&tmp, &nowinfo.status.st_mtimespec);
		if (gfarm_timespec_cmp(&tmp, &zero) < 0 ||
		    gfarm_timespec_cmp(&tmp, &update_time_interval) > 0)
			break;
#else /* force updating */
		if (gfarm_timespec_cmp(&info->status.st_mtimespec,
				       &nowinfo.status.st_mtimespec) != 0) {
			_debug("!!! mtime updating.\n");
			break;
		}
#endif

#if 1
		tmp = info->status.st_atimespec;
		gfarm_timespec_sub(&tmp, &nowinfo.status.st_atimespec);
		if (gfarm_timespec_cmp(&tmp, &zero) < 0 ||
		    gfarm_timespec_cmp(&tmp, &update_time_interval) > 0)
			break;
#else /* force updating */
		if (gfarm_timespec_cmp(&info->status.st_atimespec,
				       &nowinfo.status.st_atimespec) != 0) {
			_debug("!!! atime updating.\n");
			break;
		}
#endif

#if 1
		tmp = info->status.st_ctimespec;
		gfarm_timespec_sub(&tmp, &nowinfo.status.st_ctimespec);
		if (gfarm_timespec_cmp(&tmp, &zero) < 0 ||
		    gfarm_timespec_cmp(&tmp, &update_time_interval) > 0)
			break;
#else /* force updating */
		if (gfarm_timespec_cmp(&info->status.st_ctimespec,
				       &nowinfo.status.st_ctimespec) != 0) {
			_debug("!!! ctime updating.\n");
			break;
		}
#endif
		_debug("! cancel updating mtime/atime/ctime: %s\n", pathname);
		gfarm_path_info_free(&nowinfo);
		return (1); /* do nothing */
	}
	gfarm_path_info_free(&nowinfo);
	return (0); /* need updating */
}

/**********************************************************************/

void
gfarm_cache_path_info_param_set(unsigned int timeout,
				unsigned int update_time_interval)
{
	/* millisec. */
	cache_timeout_set(timeout);
	cache_update_time_interval_set(update_time_interval);
}

char *
gfarm_cache_path_info_get(const char *pathname, struct gfarm_path_info *info)
{
	char *e;

	e = cache_path_info_get(pathname, info);
#if 0  /* for debug */
	if (e == NULL) {
		struct gfarm_path_info tmp;
		char *e2;
		e2 = gfarm_metadb_path_info_get(pathname, &tmp);
		if (e2 == NULL) {
			if (compare_path_info_except_time(info, &tmp)
			    != 0) {
				_debug("! different cache\n");
			}
			gfarm_path_info_free(&tmp);
		}
	}
#endif
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		e = gfarm_metadb_path_info_get(pathname, info);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			cache_path_info_put(pathname, NULL); /* cache NOENT */
		else if (e == NULL)
			cache_path_info_put(pathname, info);
	}
	return (e);
}

char *
gfarm_cache_path_info_set(char *pathname, struct gfarm_path_info *info)
{
	char *e;

	e = gfarm_metadb_path_info_set(pathname, info);
	if (e == NULL)
		cache_path_info_put(pathname, info);
	else
		cache_path_info_remove(pathname);
	return (e);
}

char *
gfarm_cache_path_info_replace(char *pathname, struct gfarm_path_info *info)
{
	char *e;

	if (check_update_time_interval(pathname, info))
		return (NULL); /* cancel updating time */

	e = gfarm_metadb_path_info_replace(pathname, info);
	if (e == NULL)
		cache_path_info_put(pathname, info);
	else
		cache_path_info_remove(pathname);
	return (e);
}

char *
gfarm_cache_path_info_remove(const char *pathname)
{
	char *e;

	e = gfarm_metadb_path_info_remove(pathname);
	if (e == NULL)
		cache_path_info_put(pathname, NULL); /* cache NOENT */
	else
		cache_path_info_remove(pathname);
	return (e);
}

char *
gfarm_cache_size_set(const char *pathname, file_offset_t size)
{
	char *e;
	struct gfarm_path_info info;

	e = cache_path_info_get(pathname, &info);
	if (e == NULL) {
		if (info.status.st_size != size) {
			_debug("! cache size: %s\n", pathname);
			info.status.st_size = size;
			e = cache_path_info_put(pathname, &info);
		}
		gfarm_path_info_free(&info);
	}
	return (e);
}

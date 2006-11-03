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
#include "config.h"

#define HOSTCACHE_HASHTAB_SIZE	101	/* prime number */

static struct gfarm_cache_host {
	struct gfarm_host_info *hosts;
	int nhosts;
	struct gfarm_hash_table *hash;
	int is_invalid;
	struct timeval last_cache;
} *host_cache;

static char *
gfarm_cache_host_info_copy(
	struct gfarm_host_info *dest, const struct gfarm_host_info *src)
{
	*dest = *src;
	dest->hostname = strdup(src->hostname);
	/* hostaliases is optional */
	if (src->hostaliases && src->nhostaliases > 0)
		dest->hostaliases = gfarm_strarray_dup(src->hostaliases);
	else
		dest->hostaliases = NULL;
	dest->architecture = strdup(src->architecture);
	return (NULL);
}

static void
gfarm_cache_host_info_cache_free(struct gfarm_cache_host *h)
{
	gfarm_hash_table_free(h->hash);
	gfarm_metadb_host_info_free_all(h->nhosts, h->hosts);
	free(h);
}

static char *
gfarm_cache_host_info_hash_add(struct gfarm_hash_table *hash,
	char *hostname, struct gfarm_host_info *host)
{
	struct gfarm_hash_entry *entry;
	int created;

	entry = gfarm_hash_enter(hash, hostname, strlen(hostname) + 1,
			sizeof(struct gfarm_host_info *), &created);
	if (entry == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (!created)
		return (GFARM_ERR_ALREADY_EXISTS);

	*(struct gfarm_host_info **)gfarm_hash_entry_data(entry) = host;
	return (NULL);
}

static char *
gfarm_cache_host_info_hash(struct gfarm_cache_host *h)
{
	int i, j;
	char *e;

	if (h->nhosts == 0 || h->hosts == NULL) {
		h->hash = NULL;
		return (NULL);
	}

	h->hash = gfarm_hash_table_alloc(HOSTCACHE_HASHTAB_SIZE,
		gfarm_hash_casefold, gfarm_hash_key_equal_casefold);
	if (h->hash == NULL)
		return (GFARM_ERR_NO_MEMORY);

	for (i = 0; i < h->nhosts; ++i) {
		struct gfarm_host_info *hi = &h->hosts[i];
		e = gfarm_cache_host_info_hash_add(h->hash, hi->hostname, hi);
		if (e != NULL) {
			gfarm_hash_table_free(h->hash);
			return (e);
		}
		for (j = 0; j < hi->nhostaliases; ++j) {
			e = gfarm_cache_host_info_hash_add(
				h->hash, hi->hostaliases[j], hi);
			if (e != NULL) {
				gfarm_hash_table_free(h->hash);
				return (e);
			}
		}
	}	
	return (NULL);
}

char GFARM_ERR_CACHE_EXPIRED[] = "expired cache content";

static char *
gfarm_cache_host_info_cache()
{
	char *e;
	int nhosts;
	struct gfarm_host_info *hosts;

	/* when metadb server is down, keep old cache information */
	e = gfarm_metadb_host_info_get_all(&nhosts, &hosts);
	if (e != NULL && host_cache != NULL)
		return (GFARM_ERR_CACHE_EXPIRED);
	if (e != NULL)
		return (e);

	if (host_cache != NULL)
		gfarm_cache_host_info_cache_free(host_cache);

	GFARM_MALLOC(host_cache);
	if (host_cache == NULL)
		return (GFARM_ERR_NO_MEMORY);

	host_cache->nhosts = nhosts;
	host_cache->hosts = hosts;
	host_cache->hash = NULL;
	e = gfarm_cache_host_info_hash(host_cache);
	if (e != NULL)
		goto free_hosts;
	host_cache->is_invalid = 0;
	gettimeofday(&host_cache->last_cache, NULL);
	return (e);
free_hosts:
	gfarm_metadb_host_info_free_all(nhosts, hosts);
	free(host_cache);
	host_cache = NULL;
	return (e);
}

static void
gfarm_cache_host_info_invalidate()
{
	if (host_cache != NULL)
		host_cache->is_invalid = 1;
}

static int
gfarm_cache_host_info_expired()
{
	struct timeval now, timeout;

	timeout.tv_sec = gfarm_host_cache_timeout;
	timeout.tv_usec = 0;
	gettimeofday(&now, NULL);
	gfarm_timeval_sub(&now, &host_cache->last_cache);
	return (gfarm_timeval_cmp(&now, &timeout) >= 0);
}

static char *
gfarm_cache_host_info_check()
{
	if (host_cache == NULL || host_cache->is_invalid ||
	    gfarm_cache_host_info_expired())
		return gfarm_cache_host_info_cache();
	return (NULL);
}

void
gfarm_cache_host_info_free(struct gfarm_host_info *info)
{
	gfarm_metadb_host_info_free(info);
}

char *
gfarm_cache_host_info_get_by_name_alias(
	const char *alias, struct gfarm_host_info *info)
{
	struct gfarm_hash_entry *entry;
	char *e;

	e = gfarm_cache_host_info_check();
	if (e != NULL && e != GFARM_ERR_CACHE_EXPIRED)
		return (e);

	entry = gfarm_hash_lookup(
		host_cache->hash, alias, strlen(alias) + 1);
	if (entry != NULL) {
		gfarm_cache_host_info_copy(info,
		    *(struct gfarm_host_info **)gfarm_hash_entry_data(entry));
		return (NULL);
	}
	else
		return (GFARM_ERR_NO_SUCH_OBJECT);
}

char *
gfarm_cache_host_info_get(const char *hostname, struct gfarm_host_info *info)
{
	return gfarm_cache_host_info_get_by_name_alias(hostname, info);
}

char *
gfarm_cache_host_info_remove_hostaliases(const char *hostname)
{
	gfarm_cache_host_info_invalidate();

	return (gfarm_metadb_host_info_remove_hostaliases(hostname));
}

char *
gfarm_cache_host_info_set(char *hostname, struct gfarm_host_info *info)
{
	gfarm_cache_host_info_invalidate();

	return (gfarm_metadb_host_info_set(hostname, info));
}

char *
gfarm_cache_host_info_replace(char *hostname, struct gfarm_host_info *info)
{
	gfarm_cache_host_info_invalidate();

	return (gfarm_metadb_host_info_replace(hostname, info));
}

char *
gfarm_cache_host_info_remove(const char *hostname)
{
	gfarm_cache_host_info_invalidate();

	return (gfarm_metadb_host_info_remove(hostname));
}

void
gfarm_cache_host_info_free_all(int n, struct gfarm_host_info *infos)
{
	int i;

	for (i = 0; i < n; ++i)
		gfarm_cache_host_info_free(&infos[i]);
}

char *
gfarm_cache_host_info_get_all(int *np, struct gfarm_host_info **infosp)
{
	struct gfarm_host_info *hosts;
	int i;
	char *e;

	e = gfarm_cache_host_info_check();
	if (e != NULL && e != GFARM_ERR_CACHE_EXPIRED)
		return (e);

	GFARM_MALLOC_ARRAY(hosts, host_cache->nhosts);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	for (i = 0; i < host_cache->nhosts; ++i)
		gfarm_cache_host_info_copy(&hosts[i], &host_cache->hosts[i]);

	*np = host_cache->nhosts;
	*infosp = hosts;

	return (NULL);
}

char *
gfarm_cache_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	struct gfarm_host_info *hosts;
	int n, i;
	char *e;

	e = gfarm_cache_host_info_check();
	if (e != NULL && e != GFARM_ERR_CACHE_EXPIRED)
		return (e);

	/* XXX - linear search */
	GFARM_MALLOC_ARRAY(hosts, host_cache->nhosts);
	if (hosts == NULL)
		return (GFARM_ERR_NO_MEMORY);
	n = 0;
	for (i = 0; i < host_cache->nhosts; ++i) {
		if (strcmp(architecture, host_cache->hosts[i].architecture)
		    == 0)
			hosts[n++] = host_cache->hosts[i];
	}
	if (n == 0) {
		free(hosts);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	*np = n;
	*infosp = hosts;
	return (NULL);
}

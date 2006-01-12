#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/host_info.h>

#include "gfutil.h"
#include "hash.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "host.h"
#include "user.h"
#include "peer.h"

#define HOST_HASHTAB_SIZE	3079	/* prime number */

struct dead_file_copy {
	struct dead_file_copy *next;
	gfarm_ino_t inum;
};

/* in-core gfarm_host_info */
struct host {
	struct gfarm_host_info hi;

	struct dead_file_copy *to_be_removed;

	struct gfarm_timespec last_report;
	double loadavg_1min, loadavg_5min, loadavg_15min;
};

char REMOVED_HOST_NAME[] = "gfarm-removed-host";

static struct gfarm_hash_table *host_hashtab = NULL;
static struct gfarm_hash_table *hostalias_hashtab = NULL;

int
hash_host(const void *key, int keylen)
{
	const unsigned char *const *hostnamep = key;
	const unsigned char *k = *hostnamep;

	return (gfarm_hash_casefold(k, strlen(k)));
}

int
hash_key_equal_host(
	const void *key1, int key1len,
	const void *key2, int key2len)
{
	const unsigned char *const *u1 = key1, *const *u2 = key2;
	const unsigned char *k1 = *u1, *k2 = *u2;
	int l1, l2;

	/* short-cut on most case */
	if (*k1 != *k2)
		return (0);
	l1 = strlen(k1);
	l2 = strlen(k2);
	if (l1 != l2)
		return (0);

	return (gfarm_hash_key_equal_casefold(k1, l1, k2, l2));
}

gfarm_error_t host_load_all(void);

gfarm_error_t
host_init(void)
{
	host_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		hash_host, hash_key_equal_host);
	if (host_hashtab == NULL)
		return (GFARM_ERR_NO_MEMORY);
	hostalias_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		hash_host, hash_key_equal_host);
	if (hostalias_hashtab == NULL) {
		gfarm_hash_table_free(host_hashtab);
		return (GFARM_ERR_NO_MEMORY);
	}
	return (host_load_all());
}

/* XXX FIXME missing hostaliases */
struct host *
host_lookup(const char *hostname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(host_hashtab, &hostname, sizeof(hostname));
	if (entry == NULL)
		return (NULL);
	return (*(struct host **)gfarm_hash_entry_data(entry));
}

/* XXX FIXME missing hostaliases */
gfarm_error_t
host_enter(struct gfarm_host_info *hi, struct host **hpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct host *h = malloc(sizeof(*h));

	if (h == NULL)
		return (GFARM_ERR_NO_MEMORY);
	h->hi = *hi;

	entry = gfarm_hash_enter(host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname), sizeof(struct host *),
	    &created);
	if (entry == NULL) {
		free(h);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		free(h);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	h->to_be_removed = NULL;
	h->last_report.tv_sec = 0;
	h->last_report.tv_nsec = 0;
	h->loadavg_1min =
	h->loadavg_5min =
	h->loadavg_15min = 0.0;
	*(struct host **)gfarm_hash_entry_data(entry) = h;
	if (hpp != NULL)
		*hpp = h;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX FIXME missing hostaliases */
gfarm_error_t
host_remove(const char *hostname)
{
	struct gfarm_hash_entry *entry;
	struct host *h;

	entry = gfarm_hash_lookup(host_hashtab, &hostname, sizeof(hostname));
	if (entry == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	h = *(struct host **)gfarm_hash_entry_data(entry);
	gfarm_hash_purge(host_hashtab, &hostname, sizeof(hostname));

	/* free gfarm_host_info */
	free(h->hi.hostname);
	free(h->hi.architecture);

	/* mark this as removed */
	h->hi.hostname = REMOVED_HOST_NAME;
	h->hi.architecture = NULL;
	/* XXX We should have a list which points all removed hosts */
	return (GFARM_ERR_NO_ERROR);
}

char *
host_name(struct host *h)
{
	return (h->hi.hostname);
}

gfarm_error_t
host_get_loadav(struct host *host, double *loadavp)
{
	/* XXX FIXME: need to check expiration */
	*loadavp = host->loadavg_1min;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_remove_replica(struct host *host, gfarm_ino_t inum)
{
	struct dead_file_copy *dfc = malloc(sizeof(*dfc));

	if (dfc == NULL)
		return (GFARM_ERR_NO_MEMORY);
	dfc->inum = inum;
	dfc->next = host->to_be_removed;
	host->to_be_removed = dfc;
	return (GFARM_ERR_NO_ERROR);
}

/*
 * I/O
 */

static FILE *host_fp;

gfarm_error_t
host_info_open_for_seq_read(void)
{
	host_fp = fopen("host", "r");
	if (host_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_open_for_seq_write(void)
{
	host_fp = fopen("host", "w");
	if (host_fp == NULL)
		return (gfarm_errno_to_error(errno));
	return (GFARM_ERR_NO_ERROR);
}

/* This function needs to allocate strings */
gfarm_error_t
host_info_read_next(struct gfarm_host_info *hi)
{
	int port, ncpu, flags;
	char buffer[2048], hostname[2048], architecture[2048];

	if (fgets(buffer, sizeof buffer, host_fp) == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	if (sscanf(buffer, "%s %d %d %d %s",
	    hostname, &port, &ncpu, &flags, architecture) != 5) {
		gflog_warning("host: invalid field number: %s", buffer);
	} else {
	}
	
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_write_next(struct gfarm_host_info *hi)
{
	int i;

	fprintf(host_fp, "%s %d %d %d %s", hi->hostname, hi->port,
	    hi->ncpu, hi->flags, hi->architecture);
	for (i = 0; i < hi->nhostaliases; i++)
		fprintf(host_fp, " %s", hi->hostaliases[i]);
	fprintf(host_fp, "\n");
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_close_for_seq_read(void)
{
	fclose(host_fp);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_info_close_for_seq_write(void)
{
	fclose(host_fp);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
host_load_all(void)
{
	gfarm_error_t e;
	struct gfarm_host_info hi;

	e = host_info_open_for_seq_read();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	while (host_info_read_next(&hi) == GFARM_ERR_NO_ERROR) {
		e = host_enter(&hi, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning("host_load: %s", gfarm_error_string(e));
	}
	host_info_close_for_seq_read();
	return (GFARM_ERR_NO_ERROR);
}

#ifndef TEST
/*
 * protocol handler
 */

gfarm_error_t
host_info_send(struct gfp_xdr *client, struct gfarm_host_info *host)
{
	gfarm_error_t e;

	e = gfp_xdr_send(client, "ssiiii",
	    host->hostname, host->architecture,
	    host->ncpu, host->port, host->flags, host->nhostaliases);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (e);
}

gfarm_error_t
host_info_recv(struct gfp_xdr *client, struct gfarm_host_info *host)
{
	return (gfp_xdr_send(client, "ssiii",
	    host->hostname, host->architecture,
	    host->ncpu, host->port, host->flags));
}

gfarm_error_t
gfm_server_host_info_get_all(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	struct gfarm_hash_iterator it;
	gfarm_int32_t nhosts;

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* XXX FIXME too long giant lock */
	giant_lock();

	nhosts = 0;
	for (gfarm_hash_iterator_begin(host_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		++nhosts;
	}
	e = gfm_server_put_reply(peer, "host_info_get_all",
	    GFARM_ERR_NO_ERROR, "i", nhosts);
	if (e != GFARM_ERR_NO_ERROR) {
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(host_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		e = host_info_send(client,
		    gfarm_hash_entry_data(gfarm_hash_iterator_access(&it)));
		if (e != GFARM_ERR_NO_ERROR) {
			giant_unlock();
			return (e);
		}
	}
	giant_unlock();
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_host_info_get_by_architecture(struct peer *peer,
	int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	char *architecture;
	gfarm_int32_t nhosts;
	struct gfarm_host_info *host;
	struct gfarm_hash_iterator it;

	e = gfm_server_get_request(peer, "host_info_get_by_architecture",
	    "s", &architecture);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	nhosts = 0;
	for (gfarm_hash_iterator_begin(host_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		host = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (strcmp(host->architecture, architecture) == 0)
			++nhosts;
	}
	if (nhosts == 0) {
		e = gfm_server_put_reply(peer, "host_info_get_all",
		    GFARM_ERR_NO_SUCH_OBJECT, "");
	} else {
		e = gfm_server_put_reply(peer, "host_info_get_all",
		    GFARM_ERR_NO_ERROR, "i", nhosts);
	}
	if (e != GFARM_ERR_NO_ERROR || nhosts == 0) {
		free(architecture);
		giant_unlock();
		return (e);
	}
	for (gfarm_hash_iterator_begin(host_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it);
	     gfarm_hash_iterator_next(&it)) {
		host = gfarm_hash_entry_data(gfarm_hash_iterator_access(&it));
		if (strcmp(host->architecture, architecture) == 0) {
			e = host_info_send(client, host);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	free(architecture);
	giant_unlock();
	return (e);
}

gfarm_error_t
gfm_server_host_info_get_by_names_common(struct peer *peer,
	int from_client, int skip,
	struct gfarm_hash_table *hashtab, char *diag)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t nhosts;
	char *host, **hosts;
	int i, j, eof, no_memory = 0;
	struct gfarm_hash_entry *entry;

	e = gfm_server_get_request(peer, diag, "i", &nhosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	hosts = malloc(sizeof(*hosts) * nhosts);
	if (hosts == NULL)
		no_memory = 1;
	for (i = 0; i < nhosts; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &host);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (hosts != NULL) {
				for (j = 0; j < i; j++) {
					if (hosts[j] != NULL)
						free(hosts[j]);
				}
				free(hosts);
			}
			return (e);
		}
		if (hosts == NULL) {
			free(host);
		} else {
			if (host == NULL)
				no_memory = 1;
			hosts[i] = host;
		}
	}
	if (no_memory) {
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_MEMORY,"");
	} else {
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "");
	}
	if (no_memory || e != GFARM_ERR_NO_ERROR) {
		if (hosts != NULL) {
			for (i = 0; i < nhosts; i++) {
				if (hosts[i] != NULL)
					free(hosts[i]);
			}
			free(hosts);
		}
		return (e);
	}
	/* XXX FIXME too long giant lock */
	giant_lock();
	for (i = 0; i < nhosts; i++) {
		entry = gfarm_hash_lookup(hashtab, hosts[i], strlen(hosts[i]));
		if (entry == NULL) {
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_UNKNOWN_HOST, "");
		} else {
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = host_info_send(client,
				    gfarm_hash_entry_data(entry));
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	for (i = 0; i < nhosts; i++)
		free(hosts[i]);
	free(hosts);
	giant_unlock();
	return (e);
}

gfarm_error_t
gfm_server_host_info_get_by_names(struct peer *peer, int from_client, int skip)
{
	return(gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, host_hashtab, "host_info_get_by_names"));
}

gfarm_error_t
gfm_server_host_info_get_by_namealises(struct peer *peer,
	int from_client, int skip)
{
	return(gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, hostalias_hashtab,
	    "host_info_get_by_names"));
}

gfarm_error_t
gfm_server_host_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user = peer_get_user(peer);
	char *hostname, *architecture;
	gfarm_int32_t ncpu, port, flags;
	struct gfarm_host_info hi;

	e = gfm_server_get_request(peer, "host_info_set", "ssiii",
	    &hostname, &architecture, &ncpu, &port, &flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(hostname);
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (host_lookup(hostname) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
	} else {
		hi.hostname = hostname;
		/* XXX FIXME missing hostaliases */
		hi.nhostaliases = 0;
		hi.hostaliases = NULL;
		hi.architecture = architecture;
		hi.ncpu = ncpu;
		hi.port = port;
		hi.flags = flags;
		e = host_enter(&hi, NULL);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		free(hi.hostname);
		free(hi.architecture);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, "host_info_set", e, ""));
}

gfarm_error_t
gfm_server_host_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("host_info_modify: not implemented");

	e = gfm_server_put_reply(peer, "host_info_modify",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_host_info_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("host_info_remove: not implemented");

	e = gfm_server_put_reply(peer, "host_info_remove",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* TEST */

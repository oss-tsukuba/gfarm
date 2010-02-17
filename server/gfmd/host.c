/*
 * $Id$
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* for host_addr_lookup() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h" /* GFM_PROTO_SCHED_FLAG_* */
#include "config.h"

#include "thrsubr.h"
#include "callout.h"
#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "peer.h"
#include "inode.h"
#include "back_channel.h"

#define HOST_HASHTAB_SIZE	3079	/* prime number */

struct dead_file_copy {
	struct dead_file_copy *next;
	gfarm_ino_t inum;
	gfarm_uint64_t igen;
};

static pthread_mutex_t total_disk_mutex = PTHREAD_MUTEX_INITIALIZER;
static gfarm_off_t total_disk_used, total_disk_avail;

/* in-core gfarm_host_info */
struct host {
	struct gfarm_host_info hi;

	struct peer *peer;

	struct dead_file_copy *to_be_removed;
	pthread_mutex_t remover_mutex;
	pthread_cond_t remover_cond;

	pthread_mutex_t status_mutex;

	int invalid;	/* set when deleted */

	volatile int is_active;
	gfarm_time_t last_report;
	struct host_status status;
	gfarm_int32_t report_flags;

	int protocol_version, peer_unset_pending;
	struct callout *status_callout;

	struct file_replicating replicating_inodes; /* dummy header */
};

char REMOVED_HOST_NAME[] = "gfarm-removed-host";

static struct gfarm_hash_table *host_hashtab = NULL;
static struct gfarm_hash_table *hostalias_hashtab = NULL;

#define FOR_ALL_HOSTS(it) \
	for (gfarm_hash_iterator_begin(host_hashtab, (it)); \
	    !gfarm_hash_iterator_is_end(it); \
	     gfarm_hash_iterator_next(it))

struct host *
host_hashtab_lookup(struct gfarm_hash_table *hashtab, const char *hostname)
{
	struct gfarm_hash_entry *entry;

	entry = gfarm_hash_lookup(hashtab, &hostname, sizeof(hostname));
	if (entry == NULL)
		return (NULL);
	return (*(struct host **)gfarm_hash_entry_data(entry));
}

struct host *
host_iterator_access(struct gfarm_hash_iterator *it)
{
	struct host **hp =
	    gfarm_hash_entry_data(gfarm_hash_iterator_access(it));

	return (*hp);
}

static void
host_invalidate(struct host *h)
{
	h->invalid = 1;
}

static void
host_activate(struct host *h)
{
	h->invalid = 0;
}

int
host_is_invalidated(struct host *h)
{
	return (h->invalid == 1);
}

int
host_is_active(struct host *h)
{
	return (h != NULL && !host_is_invalidated(h));
}

struct host *
host_lookup(const char *hostname)
{
	return (host_hashtab_lookup(host_hashtab, hostname));
}

struct host *
host_addr_lookup(const char *hostname, struct sockaddr *addr)
{
	struct host *h = host_lookup(hostname);
#if 0
	struct gfarm_hash_iterator it;
	struct sockaddr_in *addr_in;
	struct hostent *hp;
	int i;
#endif

	if (h != NULL)
		return (h);
	if (addr->sa_family != AF_INET)
		return (NULL);

#if 0
	/*
	 * skip the following case since it is extraordinarily slow
	 * when there are some nodes that cannot be resolved.
	 */
	addr_in = (struct sockaddr_in *)addr;

	/* XXX FIXME - this is too damn slow */

	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (!host_is_active(h))
			continue;
		hp = gethostbyname(h->hi.hostname);
		if (hp == NULL || hp->h_addrtype != AF_INET)
			continue;
		for (i = 0; hp->h_addr_list[i] != NULL; i++) {
			if (memcmp(hp->h_addr_list[i], &addr_in->sin_addr,
			    sizeof(addr_in->sin_addr)) == 0)
				return (h);
		}
	}
#endif
	return (NULL);
}

struct host *
host_namealiases_lookup(const char *hostname)
{
	struct host *h = host_lookup(hostname);

	if (h != NULL)
		return (h);
	return (host_hashtab_lookup(hostalias_hashtab, hostname));
}

/* XXX FIXME missing hostaliases */
gfarm_error_t
host_enter(struct gfarm_host_info *hi, struct host **hpp)
{
	struct gfarm_hash_entry *entry;
	int created;
	struct host *h;

	h = host_lookup(hi->hostname);
	if (h != NULL) {
		if (host_is_invalidated(h)) {
			host_activate(h);
			if (hpp != NULL)
				*hpp = h;
			/* copy host info but keeping address of hostname */
			free(hi->hostname);
			hi->hostname = h->hi.hostname;
			h->hi.hostname = NULL; /* prevent to free this area */
			gfarm_host_info_free(&h->hi);
			h->hi = *hi;
			return (GFARM_ERR_NO_ERROR);
		} else
			return (GFARM_ERR_ALREADY_EXISTS);
	}

	GFARM_MALLOC(h);
	if (h == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of host failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	h->hi = *hi;

	entry = gfarm_hash_enter(host_hashtab,
	    &h->hi.hostname, sizeof(h->hi.hostname), sizeof(struct host *),
	    &created);
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_hash_enter() failed");
		free(h);
		return (GFARM_ERR_NO_MEMORY);
	}
	if (!created) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"create entry failed");
		free(h);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	h->peer = NULL;
	h->to_be_removed = NULL;
	h->is_active = 0;
	h->last_report = 0;
	h->status.loadavg_1min =
	h->status.loadavg_5min =
	h->status.loadavg_15min = 0.0;
	h->status.disk_used =
	h->status.disk_avail = 0;
	h->report_flags = 0;
	h->protocol_version = 0;
	h->peer_unset_pending = 0;
	h->status_callout = NULL;

	/* make circular list `replicating_inodes' empty */
	h->replicating_inodes.prev_inode =
	h->replicating_inodes.next_inode = &h->replicating_inodes;

	mutex_init(&h->status_mutex, "host", "status");
	mutex_init(&h->remover_mutex, "host", "remover");
	cond_init(&h->remover_cond, "host", "remover");
	*(struct host **)gfarm_hash_entry_data(entry) = h;
	host_activate(h);
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
	if (entry == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_hash_lookup() failed");
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	h = *(struct host **)gfarm_hash_entry_data(entry);
	if (host_is_invalidated(h))
		return (GFARM_ERR_NO_SUCH_OBJECT);
	/*
	 * do not purge the hash entry.  Instead, invalidate it so
	 * that it can be activated later.
	 */
	host_invalidate(h);

	return (GFARM_ERR_NO_ERROR);
}

void
host_peer_set(struct host *h, struct peer *p, int version, struct callout *c)
{
	pthread_mutex_lock(&h->remover_mutex);
	h->peer = p;
	h->is_active = 1;
	h->protocol_version = version;
	h->status_callout = c;
	pthread_mutex_unlock(&h->remover_mutex);
}

/* giant_lock should be held before calling this */
void
host_peer_unset(struct host *h)
{
	pthread_mutex_lock(&h->remover_mutex);
	if (callout_invoking(h->status_callout)) {
		/* avoid race condition of callout */
		h->peer_unset_pending = 1;
		pthread_mutex_unlock(&h->remover_mutex);
	} else {
		h->peer_unset_pending = 0;
		callout_stop(h->status_callout);
		callout_free(h->status_callout);
		h->status_callout = NULL;

		h->peer = NULL;
		h->is_active = 0;
		/* terminate a remover thread */
		pthread_cond_broadcast(&h->remover_cond);
		pthread_mutex_unlock(&h->remover_mutex);

		/* the following is the reason why giant_lock is necessary */
		while (h->replicating_inodes.next_inode !=
		    &h->replicating_inodes)
			file_replicating_free(h->replicating_inodes.next_inode);
	}
}

/* giant_lock should be held before calling this */
void
host_peer_disconnect(struct host *h)
{
	struct peer *peer;

	/* disconnect the back channel */
	if ((peer = host_peer(h)) != NULL) {
		peer_record_protocol_error(peer);
		host_peer_unset(h);
	}
}

struct callout *
host_status_callout(struct host *h)
{
	return (h->status_callout);
}

int
host_peer_unset_pending(struct host *h)
{
	int pending;

	pthread_mutex_lock(&h->remover_mutex);
	pending = h->peer_unset_pending;
	pthread_mutex_unlock(&h->remover_mutex);
	return (pending);
}


struct peer *
host_peer(struct host *h)
{
	return (h->peer);
}

char *
host_name(struct host *h)
{
	return (host_is_active(h) ? h->hi.hostname : REMOVED_HOST_NAME);
}

int
host_port(struct host *h)
{
	return (h->hi.port);
}

int
host_is_up(struct host *h)
{
	/*
	 * XXX - should be called with mutex h->remover_mutex,
	 * but it is not always satisfied.
	 */
	return (host_is_active(h) && h->is_active);
}

/* only file_replicating_new() is allowed to call this routine */
struct file_replicating *
host_replicating_new(struct host *dst)
{
	struct file_replicating *fr;

	GFARM_MALLOC(fr);
	if (fr == NULL)
		return (NULL);

	fr->dst = dst;
	fr->handle = -1;

	fr->prev_inode = &dst->replicating_inodes;
	fr->next_inode = dst->replicating_inodes.next_inode;
	dst->replicating_inodes.next_inode = fr;
	fr->next_inode->prev_inode = fr;

	/* the followings should be initialized by inode_replicating() */
	fr->prev_host = fr;
	fr->next_host = fr;
	return (fr);
}

/* only file_replicating_free() is allowed to call this routine */
void
host_replicating_free(struct file_replicating *fr)
{
	fr->prev_inode->next_inode = fr->next_inode;
	fr->next_inode->prev_inode = fr->prev_inode;
	free(fr);
}

void
file_replicating_set_handle(struct file_replicating *fr, gfarm_int64_t handle)
{
	fr->handle = handle;
}

gfarm_int64_t
file_replicating_get_handle(struct file_replicating *fr)
{
	return (fr->handle);
}

gfarm_error_t
host_replicated(struct host *host, gfarm_int32_t errcode,
	gfarm_int64_t handle, gfarm_off_t size)
{
	struct file_replicating *fr;

	for (fr = host->replicating_inodes.next_inode;
	    fr != &host->replicating_inodes; fr = fr->next_inode) {
		if (fr->handle == handle)
			break;
	}
	if (fr == &host->replicating_inodes)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	return (inode_replicated(fr, errcode, size));
}

void
host_remove_replica_enq_copy(
	struct host *host, struct dead_file_copy *dfc)
{
	pthread_mutex_lock(&host->remover_mutex);
	dfc->next = host->to_be_removed;
	host->to_be_removed = dfc;
	pthread_cond_broadcast(&host->remover_cond);
	pthread_mutex_unlock(&host->remover_mutex);
}

gfarm_error_t
host_remove_replica_enq(
	struct host *host, gfarm_ino_t inum, gfarm_uint64_t igen)
{
	struct dead_file_copy *dfc;

	GFARM_MALLOC(dfc);
	if (dfc == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of dead_file_copy failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	dfc->inum = inum;
	dfc->igen = igen;
	host_remove_replica_enq_copy(host, dfc);
	return (GFARM_ERR_NO_ERROR);
}

/*
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
int
host_count_dead_copies_all(gfarm_ino_t inum, int up_only)
{
	struct gfarm_hash_iterator it;
	struct host *h;
	struct dead_file_copy *r;
	int i = 0;

	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (!up_only || host_is_active(h)) {

			pthread_mutex_lock(&h->remover_mutex);

			for (r = h->to_be_removed; r != NULL; r = r->next) {
				if (r->inum == inum)
					i++;
			}

			pthread_mutex_unlock(&h->remover_mutex);
		}
	}
	return (i);
}

gfarm_error_t
host_dead_copies_info_all(gfarm_ino_t inum, int up_only,
	int *np, char **hosts, gfarm_int64_t *gens, gfarm_int32_t *flags)
{
	struct gfarm_hash_iterator it;
	struct host *h;
	struct dead_file_copy *r;
	int i = 0, n = *np;
	gfarm_int32_t down;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	FOR_ALL_HOSTS(&it) {
		if (i >= n)
			break;

		h = host_iterator_access(&it);
		if (!up_only || host_is_up(h)) {

			pthread_mutex_lock(&h->remover_mutex);

			if (host_is_up(h))
				down = 0;
			else
				down = GFM_PROTO_REPLICA_FLAG_DEAD_HOST;

			for (r = h->to_be_removed; r != NULL; r = r->next) {
				if (r->inum == inum) {
					hosts[i] = strdup(host_name(h));
					gens[i] = r->igen;
					flags[i] = down;
					if (hosts[i] == NULL) {
						e = GFARM_ERR_NO_MEMORY;
						break;
					}
					i++;
				}
			}

			pthread_mutex_unlock(&h->remover_mutex);

			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			free(hosts[i]);
	} else {
		*np = i;
	}
	return (e);
}

gfarm_error_t
host_remove_replica(struct host *host, struct timespec *timeout)
{
	struct dead_file_copy *r;
	gfarm_error_t e;
	int retcode;

	pthread_mutex_lock(&host->remover_mutex);
	while (host->to_be_removed == NULL) {
		retcode = pthread_cond_timedwait(
			&host->remover_cond, &host->remover_mutex,
			timeout);
		if (retcode == ETIMEDOUT) {
			pthread_mutex_unlock(&host->remover_mutex);
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation timeout");
			return (GFARM_ERR_OPERATION_TIMED_OUT);
		}
		if (!host_is_up(host)) {
			pthread_mutex_unlock(&host->remover_mutex);
			gflog_debug(GFARM_MSG_UNFIXED,
				"operation is not permitted");
			return (GFARM_ERR_OPERATION_NOT_PERMITTED);
		}
	}
	r = host->to_be_removed;
	host->to_be_removed = host->to_be_removed->next;
	pthread_mutex_unlock(&host->remover_mutex);

	e = gfs_client_fhremove(host_peer(host), r->inum, r->igen);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000262,
		    "host_remove_replica(%" GFARM_PRId64
			    "): %s", r->inum, gfarm_error_string(e));
		if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY)
			/* already removed by some reason */
			free(r);
		else
			host_remove_replica_enq_copy(host, r);
	} else
		free(r);
	return (e);
}

static gfarm_error_t
host_remove_replica_dump(struct host *host)
{
	gfarm_error_t e;
	struct dead_file_copy *r, *nr;

	pthread_mutex_lock(&host->remover_mutex);
	r = host->to_be_removed;
	while (r != NULL) {
		e = db_deadfilecopy_add(r->inum, r->igen, host_name(host));
		if (e != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000263,
			    "db_deadfilecopy_add(%" GFARM_PRId64
				    ", %s): %s", r->inum, host_name(host),
				    gfarm_error_string(e));
		else if (debug_mode)
			gflog_debug(GFARM_MSG_1000264,
			    "db_deadfilecopy_add(%" GFARM_PRId64
				    ", %s): added", r->inum, host_name(host));
		nr = r->next;
		free(r);
		r = nr;
	}
	host->to_be_removed = NULL;
	pthread_mutex_unlock(&host->remover_mutex);
	return (GFARM_ERR_NO_ERROR);
}

void
host_remove_replica_dump_all(void)
{
	struct gfarm_hash_iterator it;
	struct host *h;
	gfarm_error_t e;

	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_active(h)) {
			e = host_remove_replica_dump(h);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_warning(GFARM_MSG_1000265,
				    "host_remove_replica_dump_all: %s",
				    gfarm_error_string(e));
		}
	}
}

void
host_status_update(struct host *host, struct host_status *status)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;

	mutex_lock(&host->status_mutex, "host", "status_update");

	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = host->status.disk_used;
		saved_avail = host->status.disk_avail;
	}

	host->last_report = time(NULL);
	host->report_flags =
		GFM_PROTO_SCHED_FLAG_HOST_AVAIL |
		GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL;
	host->status = *status;

	mutex_unlock(&host->status_mutex, "host", "status_update");

	pthread_mutex_lock(&total_disk_mutex);
	total_disk_used += status->disk_used - saved_used;
	total_disk_avail += status->disk_avail - saved_avail;
	pthread_mutex_unlock(&total_disk_mutex);
}

void
host_status_disable(struct host *host)
{
	gfarm_uint64_t saved_used = 0, saved_avail = 0;

	mutex_lock(&host->status_mutex, "host", "status_disable");

	if (host->report_flags & GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL) {
		saved_used = host->status.disk_used;
		saved_avail = host->status.disk_avail;
	}

	host->report_flags = 0;

	mutex_unlock(&host->status_mutex, "host", "status_disable");

	pthread_mutex_lock(&total_disk_mutex);
	total_disk_used -= saved_used;
	total_disk_avail -= saved_avail;
	pthread_mutex_unlock(&total_disk_mutex);
}

gfarm_error_t
host_update_status(struct host *host)
{
	gfarm_error_t e;
	struct host_status status;

	e = gfs_client_status(host_peer(host),
		&status.loadavg_1min, &status.loadavg_5min,
		&status.loadavg_15min,
		&status.disk_used, &status.disk_avail);
	if (e == GFARM_ERR_NO_ERROR)
		host_status_update(host, &status);
	else
		host_status_disable(host);
	return (e);
}

/*
 * save all to text file
 */

static FILE *host_fp;

gfarm_error_t
host_info_open_for_seq_write(void)
{
	host_fp = fopen("host", "w");
	if (host_fp == NULL)
		return (gfarm_errno_to_error(errno));
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
host_info_close_for_seq_write(void)
{
	fclose(host_fp);
	return (GFARM_ERR_NO_ERROR);
}

/* The memory owner of `*hi' is changed to host.c */
void
host_add_one(void *closure, struct gfarm_host_info *hi)
{
	gfarm_error_t e = host_enter(hi, NULL);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000266,
		    "host_add_one: %s", gfarm_error_string(e));
}

void
host_init(void)
{
	gfarm_error_t e;

	host_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (host_hashtab == NULL)
		gflog_fatal(GFARM_MSG_1000267, "no memory for host hashtab");
	hostalias_hashtab =
	    gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		gfarm_hash_casefold_strptr,
		gfarm_hash_key_equal_casefold_strptr);
	if (hostalias_hashtab == NULL) {
		gfarm_hash_table_free(host_hashtab);
		gflog_fatal(GFARM_MSG_1000268,
		    "no memory for hostalias hashtab");
	}

	e = db_host_load(NULL, host_add_one);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1000269,
		    "loading hosts: %s", gfarm_error_string(e));
}

#ifndef TEST
/*
 * protocol handler
 */

/* this interface is exported for a use from a private extension */
gfarm_error_t
host_info_send(struct gfp_xdr *client, struct host *h)
{
	struct gfarm_host_info *hi = &h->hi;

	return (gfp_xdr_send(client, "ssiiii",
	    hi->hostname, hi->architecture,
	    hi->ncpu, hi->port, hi->flags, hi->nhostaliases));
}

gfarm_error_t
gfm_server_host_info_get_all(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	struct gfarm_hash_iterator it;
	struct host *h;
	gfarm_int32_t nhosts;
	const char msg[] = "protocol HOST_INFO_GET_ALL";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* XXX FIXME too long giant lock */
	giant_lock();

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_active(h))
			++nhosts;
	}
	e = gfm_server_put_reply(peer, msg,
	    GFARM_ERR_NO_ERROR, "i", nhosts);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_server_put_reply(host_info_get_all) failed: %s",
			gfarm_error_string(e));
		giant_unlock();
		return (e);
	}
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_active(h)) {
			e = host_info_send(client, h);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"host_info_send() failed: %s",
					gfarm_error_string(e));
				giant_unlock();
				return (e);
			}
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
	struct gfarm_hash_iterator it;
	struct host *h;
	const char msg[] = "protocol HOST_INFO_GET_BY_ARCHITECTURE";

	e = gfm_server_get_request(peer, msg,
	    "s", &architecture);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"host_info_get_by_architecture request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();

	nhosts = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_active(h) &&
		    strcmp(h->hi.architecture, architecture) == 0)
			++nhosts;
	}
	if (nhosts == 0) {
		e = gfm_server_put_reply(peer, msg,
		    GFARM_ERR_NO_SUCH_OBJECT, "");
	} else {
		e = gfm_server_put_reply(peer, msg,
		    GFARM_ERR_NO_ERROR, "i", nhosts);
	}
	if (e != GFARM_ERR_NO_ERROR || nhosts == 0) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_server_put_reply(host_info_get_all) failed: %s",
			gfarm_error_string(e));
		free(architecture);
		giant_unlock();
		return (e);
	}
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (host_is_active(h) &&
		    strcmp(h->hi.architecture, architecture) == 0) {
			e = host_info_send(client, h);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"host_info_send() failed: %s",
					gfarm_error_string(e));
				break;
			}
		}
	}
	free(architecture);
	giant_unlock();
	return (e);
}

gfarm_error_t
gfm_server_host_info_get_by_names_common(struct peer *peer,
	int from_client, int skip,
	struct host *(*lookup)(const char *), char *diag)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e;
	gfarm_int32_t nhosts;
	char *host, **hosts;
	int i, j, eof, no_memory = 0;
	struct host *h;

	e = gfm_server_get_request(peer, diag, "i", &nhosts);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_server_get_request() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL)
		no_memory = 1;
	for (i = 0; i < nhosts; i++) {
		e = gfp_xdr_recv(client, 0, &eof, "s", &host);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfp_xdr_recv(host) failed: %s",
				gfarm_error_string(e));
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
	if (no_memory)
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_MEMORY, "");
	else
		e = gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "");
	if (no_memory || e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfp_xdr_recv(host) failed: %s",
			gfarm_error_string(e));
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
		h = (*lookup)(hosts[i]);
		if (h == NULL || host_is_invalidated(h)) {
			if (debug_mode)
				gflog_info(GFARM_MSG_1000270,
				    "host lookup <%s>: failed",
				    hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_UNKNOWN_HOST, "");
		} else {
			if (debug_mode)
				gflog_info(GFARM_MSG_1000271,
				    "host lookup <%s>: ok", hosts[i]);
			e = gfm_server_put_reply(peer, diag,
			    GFARM_ERR_NO_ERROR, "");
			if (e == GFARM_ERR_NO_ERROR)
				e = host_info_send(client, h);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"error occurred during process: %s",
				gfarm_error_string(e));
			break;
		}
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
	return (gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, host_lookup, "host_info_get_by_names"));
}

gfarm_error_t
gfm_server_host_info_get_by_namealiases(struct peer *peer,
	int from_client, int skip)
{
	return (gfm_server_host_info_get_by_names_common(
	    peer, from_client, skip, host_namealiases_lookup,
	    "host_info_get_by_namealiases"));
}

gfarm_error_t
gfm_server_host_info_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	struct user *user = peer_get_user(peer);
	char *hostname, *architecture;
	gfarm_int32_t ncpu, port, flags;
	struct gfarm_host_info hi;
	const char msg[] = "protocol HOST_INFO_SET";

	e = gfm_server_get_request(peer, msg, "ssiii",
	    &hostname, &architecture, &ncpu, &port, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"host_info_set request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hostname);
		free(architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (host_is_active(host_lookup(hostname))) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"host already exists");
		e = GFARM_ERR_ALREADY_EXISTS;
	} else {
		hi.hostname = hostname;
		hi.port = port;
		/* XXX FIXME missing hostaliases */
		hi.nhostaliases = 0;
		hi.hostaliases = NULL;
		hi.architecture = architecture;
		hi.ncpu = ncpu;
		hi.flags = flags;
		e = host_enter(&hi, NULL);
		if (e == GFARM_ERR_NO_ERROR) {
			e = db_host_add(&hi);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"db_host_add() failed: %s",
					gfarm_error_string(e));
				host_remove(hostname);
				hostname = architecture = NULL;
			}
		}
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
		if (hostname != NULL)
			free(hostname);
		if (architecture != NULL)
			free(architecture);
	}
	giant_unlock();
	return (gfm_server_put_reply(peer, msg, e, ""));
}

gfarm_error_t
gfm_server_host_info_modify(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	struct gfarm_host_info hi;
	struct host *h;
	int needs_free = 0;
	const char msg[] = "protocol HOST_INFO_MODIFY";

	e = gfm_server_get_request(peer, msg, "ssiii",
	    &hi.hostname, &hi.architecture, &hi.ncpu, &hi.port, &hi.flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"host_info_modify request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hi.hostname);
		free(hi.architecture);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX should we disconnect a back channel to the host? */
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		needs_free = 1;
	} else if ((h = host_lookup(hi.hostname)) == NULL ||
		   host_is_invalidated(h)) {
		gflog_debug(GFARM_MSG_UNFIXED, "host does not exists");
		e = GFARM_ERR_NO_SUCH_OBJECT;
		needs_free = 1;
	} else if ((e = db_host_modify(&hi,
	    DB_HOST_MOD_ARCHITECTURE|DB_HOST_MOD_NCPU|DB_HOST_MOD_FLAGS,
	    /* XXX */ 0, NULL, 0, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"db_host_modify failed: %s",
			gfarm_error_string(e));
		needs_free = 1;
	} else {
		free(h->hi.architecture);
		h->hi.architecture = hi.architecture;
		h->hi.ncpu = hi.ncpu;
		h->hi.port = hi.port;
		h->hi.flags = hi.flags;
		free(hi.hostname);
	}
	if (needs_free) {
		free(hi.hostname);
		free(hi.architecture);
	}
	giant_unlock();

	return (gfm_server_put_reply(peer, msg, e, ""));
}

/* this interface is exported for a use from a private extension */
gfarm_error_t
host_info_remove_default(const char *hostname, const char *diag)
{
	gfarm_error_t e, e2;
	struct host *host;

	if ((host = host_lookup(hostname)) == NULL ||
	    host_is_invalidated(host))
		return (GFARM_ERR_NO_SUCH_OBJECT);

	/* disconnect the back channel */
	host_peer_disconnect(host);

	if ((e = host_remove(hostname)) == GFARM_ERR_NO_ERROR) {
		e2 = db_host_remove(hostname);
		if (e2 != GFARM_ERR_NO_ERROR)
			gflog_error(GFARM_MSG_1000272,
			    "%s: db_host_remove: %s",
			    diag, gfarm_error_string(e2));
	}
	return (e);
}

/* this interface is made as a hook for a private extension */
gfarm_error_t (*host_info_remove)(const char *, const char *) =
	host_info_remove_default;

gfarm_error_t
gfm_server_host_info_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct user *user = peer_get_user(peer);
	char *hostname;
	const char msg[] = "protocol HOST_INFO_REMOVE";

	e = gfm_server_get_request(peer, msg, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"host_info_remove request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}
	/*
	 * XXX should we remove all file copy entries stored on the
	 * specified host?
	 */
	giant_lock();
	if (!from_client || user == NULL || !user_is_admin(user)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		e = host_info_remove(hostname, msg);
	free(hostname);
	giant_unlock();

	return (gfm_server_put_reply(peer, msg, e, ""));
}

/* called from inode.c:inode_schedule_file_reply() */

gfarm_error_t
host_schedule_reply_n(struct peer *peer, gfarm_int32_t n, const char *diag)
{
	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, "i", n));
}

gfarm_error_t
host_schedule_reply(struct host *h, struct peer *peer, const char *diag)
{
	struct host_status status;
	gfarm_time_t last_report;
	gfarm_int32_t report_flags;

	mutex_lock(&h->status_mutex, "host", "schedule_reply");
	status = h->status;
	last_report = h->last_report;
	report_flags = h->report_flags;
	mutex_unlock(&h->status_mutex, "host", "schedule_reply");
	return (gfp_xdr_send(peer_get_conn(peer), "siiillllii",
	    h->hi.hostname, h->hi.port, h->hi.ncpu,
	    (gfarm_int32_t)(status.loadavg_1min * GFM_PROTO_LOADAVG_FSCALE),
	    last_report,
	    status.disk_used, status.disk_avail,
	    (gfarm_int64_t)0 /* rtt_cache_time */,
	    (gfarm_int32_t)0 /* rtt_usec */,
	    report_flags));
}

/* XXX does not care about hostaliases and architecture */
static gfarm_error_t
host_copy(struct host **dstp, const struct host *src)
{
	struct host *dst;

	GFARM_MALLOC(dst);
	if (dst == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of host failed");
		return (GFARM_ERR_NO_MEMORY);
	}

	*dst = *src;
	if ((dst->hi.hostname = strdup(dst->hi.hostname)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of hostname failed");
		free(dst);
		return (GFARM_ERR_NO_MEMORY);
	}
	*dstp = dst;
	return (GFARM_ERR_NO_ERROR);
}

static void
host_free(struct host *h)
{
	if (h == NULL)
		return;
	if (h->hi.hostname != NULL)
		free(h->hi.hostname);
	free(h);
	return;
}

static void
host_free_all(int n, struct host **h)
{
	int i;

	for (i = 0; i < n; ++i)
		host_free(h[i]);
	free(h);
}

gfarm_error_t
host_active_hosts(int (*filter)(struct host *, void *), void *arg,
	int *nhostsp, struct host ***hostsp)
{
	struct gfarm_hash_iterator it;
	struct host **hosts, *h;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, n;

	n = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		pthread_mutex_lock(&h->remover_mutex);
		if (host_is_up(h) && filter(h, arg))
			++n;
	}
	GFARM_MALLOC_ARRAY(hosts, n);
	if (hosts == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of hosts failed");
		e = GFARM_ERR_NO_MEMORY;
	}

	i = 0;
	FOR_ALL_HOSTS(&it) {
		h = host_iterator_access(&it);
		if (hosts != NULL && host_is_up(h) && filter(h, arg)) {
			e = host_copy(&hosts[i], h);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"host_copy() failed: %s",
					gfarm_error_string(e));
				host_free_all(i, hosts);
				hosts = NULL;
				/* skip all the rest except unlock */
			}
			++i;
		}
		pthread_mutex_unlock(&h->remover_mutex);
	}
	if (i == n) {
		*nhostsp = n;
		*hostsp = hosts;
	}
	return (e);
}

static int
null_filter(struct host *host, void *arg)
{
	return (1);
}

gfarm_error_t
host_schedule_reply_all(struct peer *peer, const char *diag,
	int (*filter)(struct host *, void *), void *arg)
{
	gfarm_error_t e, e_save;
	struct host **hosts;
	int i, n;

	e = host_active_hosts(filter, arg, &n, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		n = 0;

	e_save = host_schedule_reply_n(peer, n, diag);
	for (i = 0; i < n; ++i)
		e = host_schedule_reply(hosts[i], peer, diag); {
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	host_free_all(n, hosts);
	return (e_save);
}

gfarm_error_t
host_schedule_reply_one_or_all(struct peer *peer, const char *diag)
{
	gfarm_error_t e, e_save;
	struct host *h = peer_get_host(peer);

	/* give the top priority to the local host if it has enough space */
	if (host_is_up(h) &&
	    h->status.disk_avail > gfarm_get_minimum_free_disk_space()) {
		e_save = host_schedule_reply_n(peer, 1, diag);
		e = host_schedule_reply(h, peer, diag);
		return (e_save != GFARM_ERR_NO_ERROR ? e_save : e);
	} else
		return (host_schedule_reply_all(
				peer, diag, null_filter, NULL));
}

gfarm_error_t
gfm_server_hostname_set(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	char *hostname;
	const char msg[] = "protocol HOSTNAME_SET";

	e = gfm_server_get_request(peer, msg, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_server_get_request() failure");
		return (e);
	}
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}

	giant_lock();
	if (from_client) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted for from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else
		e = peer_set_host(peer, hostname);
	giant_unlock();
	free(hostname);

	return (gfm_server_put_reply(peer, msg, e, ""));
}

static int
domain_filter(struct host *h, void *d)
{
	const char *domain = d;

	return (gfarm_host_is_in_domain(host_name(h), domain));
}

gfarm_error_t
gfm_server_schedule_host_domain(struct peer *peer, int from_client, int skip)
{
	gfarm_int32_t e;
	char *domain;
	const char msg[] = "protocol SCHEDULE_HOST_DOMAIN";

	e = gfm_server_get_request(peer, msg, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"schedule_host_domain request failure: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}

	/* XXX FIXME too long giant lock */
	giant_lock();
	e = host_schedule_reply_all(peer, msg, domain_filter, domain);
	giant_unlock();
	free(domain);

	return (e);
}

gfarm_error_t
gfm_server_statfs(struct peer *peer, int from_client, int skip)
{
	gfarm_uint64_t used, avail, files;
	const char msg[] = "protocol STATFS";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	files = inode_total_num();
	pthread_mutex_lock(&total_disk_mutex);
	used = total_disk_used;
	avail = total_disk_avail;
	pthread_mutex_unlock(&total_disk_mutex);

	return (gfm_server_put_reply(peer, msg, GFARM_ERR_NO_ERROR, "lll",
		    used, avail, files));
}

#endif /* TEST */

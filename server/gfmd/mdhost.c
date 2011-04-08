/*
 * $Id$
 */

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "thrsubr.h"

#include "auth.h"
#include "gfp_xdr.h"
#include "config.h"

#include "gfm_client.h"

#include "host.h"
#include "user.h"
#include "peer.h"
#include "abstract_host.h"
#include "filesystem.h"
#include "metadb_server.h"
#include "mdhost.h"
#include "db_journal.h"
#include "journal_file.h"

/* in-core gfarm_metadb_server */
struct mdhost {
	struct abstract_host ah; /* must be the first member of this struct */

	struct mdhost *next;
	struct gfarm_metadb_server *ms;
	struct gfm_connection *conn;
#ifdef ENABLE_METADATA_REPLICATION
	int is_recieved_seqnum, is_in_first_sync;
	struct journal_file_reader *jreader;
	gfarm_uint64_t last_fetch_seqnum;
#endif
};

struct mdhost mdhost_list = { {}, &mdhost_list };
static int localhost_is_readonly = 0;

#define FOREACH_MDHOST(m) \
	for (m = mdhost_list.next; m != &mdhost_list; m = m->next)

static const char BACK_CHANNEL_DIAG[] = "gfmdc_channel";

int
mdhost_is_master(struct mdhost *m)
{
	return (gfarm_metadb_server_is_master(m->ms));
}

int
mdhost_is_self(struct mdhost *m)
{
	return (gfarm_metadb_server_is_self(m->ms));
}

const char *
mdhost_get_name(struct mdhost *m)
{
	return (gfarm_metadb_server_get_name(m->ms));
}

struct abstract_host *
mdhost_to_abstract_host(struct mdhost *m)
{
	return (&m->ah);
}

static struct host *
mdhost_downcast_to_host(struct abstract_host *h)
{
	gflog_error(GFARM_MSG_UNFIXED, "downcasting mdhost %p to host", h);
	abort();
	return (NULL);
}

static struct mdhost *
mdhost_downcast_to_mdhost(struct abstract_host *h)
{
	return ((struct mdhost *)h);
}

static const char *
mdhost_name0(struct abstract_host *h)
{
	return (mdhost_get_name(abstract_host_to_mdhost(h)));
}

int
mdhost_get_port(struct mdhost *m)
{
	return (gfarm_metadb_server_get_port(m->ms));
}

static int
mdhost_port0(struct abstract_host *h)
{
	return (mdhost_get_port(abstract_host_to_mdhost(h)));
}

struct peer *
mdhost_get_peer(struct mdhost *m)
{
	return (abstract_host_get_peer(mdhost_to_abstract_host(m),
	    BACK_CHANNEL_DIAG));
}

int
mdhost_is_up(struct mdhost *m)
{
	return (abstract_host_is_up(mdhost_to_abstract_host(m),
	    BACK_CHANNEL_DIAG));
}

void
mdhost_activate(struct mdhost *m, const char *back_channel_diag)
{
	abstract_host_activate(mdhost_to_abstract_host(m), back_channel_diag);
}

static void
mdhost_validate(struct mdhost *m)
{
	abstract_host_validate(mdhost_to_abstract_host(m));
}

void
mdhost_set_peer(struct mdhost *m, struct peer *peer, int version)
{
	abstract_host_set_peer(mdhost_to_abstract_host(m), peer, version);
}

struct gfm_connection *
mdhost_get_connection(struct mdhost *m)
{
	return (m->conn);
}

void
mdhost_set_connection(struct mdhost *m, struct gfm_connection *conn)
{
	m->conn = conn;
}

void
mdhost_foreach(int (*func)(struct mdhost *, void *), void *closure)
{
	struct mdhost *m;

	FOREACH_MDHOST(m) {
		if (func(m, closure) == 0)
			break;
	}
}

#ifdef ENABLE_METADATA_REPLICATION
static void
mdhost_channel_mutex_lock(struct mdhost *m, const char *diag)
{
	abstract_host_channel_mutex_lock(mdhost_to_abstract_host(m), diag,
	    BACK_CHANNEL_DIAG);
}

static void
mdhost_channel_mutex_unlock(struct mdhost *m, const char *diag)
{
	abstract_host_channel_mutex_unlock(mdhost_to_abstract_host(m), diag,
	    BACK_CHANNEL_DIAG);
}

struct journal_file_reader *
mdhost_get_journal_file_reader(struct mdhost *m)
{
	struct journal_file_reader *reader;
	static const char *diag = "mdhost_get_journal_file_reader";

	mdhost_channel_mutex_lock(m, diag);
	reader = m->jreader;
	mdhost_channel_mutex_unlock(m, diag);
	return (reader);
}

void
mdhost_set_journal_file_reader(struct mdhost *m,
	struct journal_file_reader *reader)
{
	static const char *diag = "mdhost_set_journal_file_reader";

	mdhost_channel_mutex_lock(m, diag);
	m->jreader = reader;
	mdhost_channel_mutex_unlock(m, diag);
}

gfarm_uint64_t
mdhost_get_last_fetch_seqnum(struct mdhost *m)
{
	gfarm_uint64_t r;
	static const char *diag = "mdhost_get_last_fetch_seqnum";

	mdhost_channel_mutex_lock(m, diag);
	r = m->last_fetch_seqnum;
	mdhost_channel_mutex_unlock(m, diag);
	return (r);
}

void
mdhost_set_last_fetch_seqnum(struct mdhost *m, gfarm_uint64_t seqnum)
{
	static const char *diag = "mdhost_set_last_fetch_seqnum";

	mdhost_channel_mutex_lock(m, diag);
	m->last_fetch_seqnum = seqnum;
	mdhost_channel_mutex_unlock(m, diag);
}

int
mdhost_is_recieved_seqnum(struct mdhost *m)
{
	int r;
	static const char *diag = "mdhost_is_recieved_seqnum";

	mdhost_channel_mutex_lock(m, diag);
	r = m->is_recieved_seqnum;
	mdhost_channel_mutex_unlock(m, diag);
	return (r);
}

void
mdhost_set_is_recieved_seqnum(struct mdhost *m, int flag)
{
	static const char *diag = "mdhost_set_is_recieved_seqnum";

	mdhost_channel_mutex_lock(m, diag);
	m->is_recieved_seqnum = flag;
	mdhost_channel_mutex_unlock(m, diag);
}

int
mdhost_is_in_first_sync(struct mdhost *m)
{
	int r;
	static const char *diag = "mdhost_is_in_first_sync";

	mdhost_channel_mutex_lock(m, diag);
	r = m->is_in_first_sync;
	mdhost_channel_mutex_unlock(m, diag);
	return (r);
}

void
mdhost_set_is_in_first_sync(struct mdhost *m, int flag)
{
	static const char *diag = "mdhost_set_is_in_first_sync";

	mdhost_channel_mutex_lock(m, diag);
	m->is_in_first_sync = flag;
	mdhost_channel_mutex_unlock(m, diag);
}

static void
mdhost_self_change_to_readonly(void)
{
	localhost_is_readonly = 1;
	gflog_warning(GFARM_MSG_UNFIXED,
	    "changed to read-only mode");
}
#endif

static void
mdhost_set_peer_locked(struct abstract_host *h, struct peer *peer)
{
}

static void
mdhost_set_peer_unlocked(struct abstract_host *h, struct peer *peer)
{
}

static void
mdhost_unset_peer(struct abstract_host *h, struct peer *peer)
{
}

static gfarm_error_t
mdhost_disable(struct abstract_host *h, void **closurep)
{
	return (GFARM_ERR_NO_ERROR);
}

static void
mdhost_disabled(struct abstract_host *h, struct peer *peer, void *closure)
{
	struct mdhost *m = abstract_host_to_mdhost(h);

	if (m->conn) {
		gfm_client_connection_unset_conn(m->conn);
		gfm_client_connection_free(m->conn);
		m->conn = NULL;
		peer_invoked(peer);
	}
#ifdef ENABLE_METADATA_REPLICATION
	m->is_recieved_seqnum = 0;
	if (m->jreader)
		journal_file_reader_close(m->jreader);
#endif
}

struct abstract_host_ops mdhost_ops = {
	mdhost_downcast_to_host,
	mdhost_downcast_to_mdhost,
	mdhost_name0,
	mdhost_port0,
	mdhost_set_peer_locked,
	mdhost_set_peer_unlocked,
	mdhost_unset_peer,
	mdhost_disable,
	mdhost_disabled,
};

static struct mdhost *
mdhost_new(struct gfarm_metadb_server *ms)
{
	struct mdhost *m;
	static const char *diag = "mdhost_new";

	if ((m = malloc(sizeof(struct mdhost))) == NULL)
		return (NULL);
	abstract_host_init(&m->ah, &mdhost_ops, diag);
	m->ms = ms;
	m->conn = NULL;
#ifdef ENABLE_METADATA_REPLICATION
	m->jreader = NULL;
	m->last_fetch_seqnum = 0;
	m->is_recieved_seqnum = 0;
	m->is_in_first_sync = 0;
#endif
	return (m);
}

struct mdhost *
mdhost_lookup(const char *hostname)
{
	struct mdhost *m;

	FOREACH_MDHOST(m)
		if (strcmp(mdhost_get_name(m), hostname) == 0)
			return (m);
	return (NULL);
}

struct mdhost *
mdhost_lookup_master(void)
{
	struct mdhost *m;

	FOREACH_MDHOST(m)
		if (mdhost_is_master(m))
			return (m);
	abort();
	return (NULL);
}


struct mdhost *
mdhost_lookup_self(void)
{
	struct mdhost *m;
	static struct mdhost *self = NULL;

	if (self)
		return (self);
	FOREACH_MDHOST(m)
		if (mdhost_is_self(m)) {
			self = m;
			return (m);
		}
	abort();
	return (NULL);
}

int
mdhost_self_is_master(void)
{
	struct mdhost *m = mdhost_lookup_self();

	return (mdhost_is_master(m));
}

int
mdhost_self_is_readonly(void)
{
	return (localhost_is_readonly);
}

/* giant_lock should be held before calling this */
void
mdhost_disconnect(struct mdhost *m, struct peer *peer)
{
	if (abstract_host_get_peer_unlocked(mdhost_to_abstract_host(m)) != NULL)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "disconnect gfmd %s", mdhost_get_name(m));
	return (abstract_host_disconnect(&m->ah, peer, BACK_CHANNEL_DIAG));
}

void
mdhost_set_self_as_master(void)
{
	struct mdhost *m, *s = mdhost_lookup_self();

	FOREACH_MDHOST(m) {
		if (mdhost_is_master(m))
			mdhost_disconnect(m, NULL);
		gfarm_metadb_server_set_is_master(m->ms, m == s);
	}
	localhost_is_readonly = 0;
}

void
mdhost_init()
{
	int i, n;
	struct gfarm_metadb_server **msl;
	struct mdhost *m, *m0, *self = NULL;
	struct gfarm_filesystem *fs;

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	m = NULL;
#endif
	fs = gfarm_filesystem_get_default();
	msl = gfarm_filesystem_get_metadb_server_list(fs, &n);
	if (msl == NULL)
		return;
	assert(n > 0);
	m0 = &mdhost_list;
	for (i = 0; i < n; ++i) {
		m = mdhost_new(msl[i]);
		if (m == NULL)
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "%s", gfarm_error_string(GFARM_ERR_NO_MEMORY));
		m0->next = m;
		m0 = m;
		if (strcmp(mdhost_get_name(m), gfarm_metadb_server_name) == 0) {
			self = m;
			gfarm_metadb_server_set_is_self(m->ms, 1);
#ifndef ENABLE_METADATA_REPLICATION
			gfarm_metadb_server_set_is_master(m->ms, 1);
#endif
		}
#ifndef ENABLE_METADATA_REPLICATION
		else
			gfarm_metadb_server_set_is_master(m->ms, 0);
#endif
	}
	if (self == NULL) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "hostname which is set to metadb_server_name is "
		    "not found in metadb_server_list");
	}
	m->next = &mdhost_list;
	mdhost_validate(self);
	mdhost_activate(self, BACK_CHANNEL_DIAG);
	if (!mdhost_is_master(self))
		localhost_is_readonly = 1;
#ifdef ENABLE_METADATA_REPLICATION
	db_journal_set_fail_store_op(mdhost_self_change_to_readonly);
#endif
}

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
	int is_readonly;
#ifdef ENABLE_JOURNAL
	int is_recieved_seqnum;
	struct journal_file_reader *jreader;
	gfarm_uint64_t last_fetch_seqnum;
#endif
};

struct mdhost mdhost_list = { {}, &mdhost_list };

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
mdhost_activate(struct mdhost *m)
{
	abstract_host_activate(mdhost_to_abstract_host(m));
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

static int
mdhost_is_readonly(struct mdhost *m)
{
	return (m->is_readonly);
}

static void
mdhost_set_is_readonly(struct mdhost *m, int flag)
{
	m->is_readonly = flag;
}

#ifdef ENABLE_JOURNAL
struct journal_file_reader *
mdhost_get_journal_file_reader(struct mdhost *m)
{
	return (m->jreader);
}

gfarm_uint64_t
mdhost_get_last_fetch_seqnum(struct mdhost *m)
{
	return (m->last_fetch_seqnum);
}

void
mdhost_set_last_fetch_seqnum(struct mdhost *m, gfarm_uint64_t seqnum)
{
	m->last_fetch_seqnum = seqnum;
}

int
mdhost_is_recieved_seqnum(struct mdhost *m)
{
	return (m->is_recieved_seqnum);
}

void
mdhost_set_is_recieved_seqnum(struct mdhost *m, int flag)
{
	m->is_recieved_seqnum = flag;
}

static void
mdhost_self_change_to_readonly(void)
{
	mdhost_set_is_readonly(mdhost_lookup_self(), 1);
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
mdhost_disable(struct abstract_host *h, struct peer *peer, void **closurep)
{
	struct mdhost *m = abstract_host_to_mdhost(h);

	if (m->conn) {
		gfm_client_connection_free(m->conn);
		m->conn = NULL;
		peer_unset_connection(peer);
		peer_invoked(peer);
	}
#ifdef ENABLE_JOURNAL
	m->is_recieved_seqnum = 0;
	if (m->jreader && journal_file_reader_is_invalid(m->jreader))
		journal_file_reader_close(m->jreader);
#endif
	return (GFARM_ERR_NO_ERROR);
}

static void
mdhost_disabled(struct abstract_host *h, void *closure)
{
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
	m->is_readonly = 0;
#ifdef ENABLE_JOURNAL
	m->jreader = NULL;
	m->last_fetch_seqnum = 0;
	m->is_recieved_seqnum = 0;
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
	struct mdhost *m = mdhost_lookup_self();

	return (mdhost_is_readonly(m));
}

/* giant_lock should be held before calling this */
void
mdhost_disconnect(struct mdhost *m, struct peer *peer)
{
	return (abstract_host_disconnect(&m->ah, peer, BACK_CHANNEL_DIAG));
}

void
mdhost_init()
{
	int i, n;
	struct gfarm_metadb_server **msl;
	struct mdhost *m, *m0, *self = NULL;
#ifdef ENABLE_JOURNAL
	gfarm_error_t e;
	struct journal_file_reader *reader;
#endif

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	m = NULL;
#endif
	msl = gfarm_get_metadb_server_list(&n);
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
		}
	}
	if (self == NULL) {
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "hostname which is set to metadb_server_name is "
		    "not found in metadb_server_list");
	}
	m->next = &mdhost_list;
	if (!mdhost_is_master(self))
		mdhost_set_is_readonly(self, 1);
#ifdef ENABLE_JOURNAL
	if (mdhost_is_master(self)) {
		FOREACH_MDHOST(m) {
			if (m != self) {
				if ((e = db_journal_add_initial_reader(&reader))
				    != GFARM_ERR_NO_ERROR) {
					gflog_fatal(GFARM_MSG_UNFIXED,
					    "%s", gfarm_error_string(e));
				} else
					m->jreader = reader;
			}
		}
	}
	db_journal_set_fail_store_op(mdhost_self_change_to_readonly);
#endif
}

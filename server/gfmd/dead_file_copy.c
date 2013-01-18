#include <pthread.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "thrsubr.h"

#include "context.h"
#include "config.h"
#include "gfm_proto.h"
#include "gfs_proto.h"
#include "gfp_xdr.h" /* gfmd.h needs this */

#include "subr.h"
#include "callout.h"
#include "db_access.h"
#include "inode.h"
#include "abstract_host.h"
#include "host.h"
#include "netsendq.h"
#include "netsendq_impl.h"
#include "dead_file_copy.h"
#include "back_channel.h"
#include "gfmd.h"	/* sync_protocol_get_thrpool() */

struct dead_file_copy {
	struct netsendq_entry qentry; /* must be first member */

	gfarm_ino_t inum;
	gfarm_uint64_t igen;

	int is_kept; /* on dfc_keptq?: protected by giant lock */

	GFARM_HCIRCLEQ_ENTRY(dead_file_copy) same_inode_copies;
};

struct dead_file_copy_list {
	GFARM_HCIRCLEQ_HEAD(dead_file_copy) list;
};

struct dfc_keptq {
	pthread_mutex_t mutex;

	GFARM_HCIRCLEQ_HEAD(netsendq_entry) q; /* dead_file_copy, actually */
};

/* IMPORTANT NOTE: functions should not sleep while holding dfc_keptq.mutex */
static struct dfc_keptq dfc_keptq;

/*
 * PREREQUISITE: giant_lock
 */
void
dead_file_copy_schedule_removal(struct dead_file_copy *dfc)
{
	struct netsendq *qhost =
	    abstract_host_get_sendq(dfc->qentry.abhost);
	static const char diag[] = "dead_file_copy_schedule_removal";

	if (dfc->is_kept) {
		gfarm_mutex_lock(&dfc_keptq.mutex, diag, "lock");
		dfc->is_kept = 0;
		GFARM_HCIRCLEQ_REMOVE(&dfc->qentry, workq_entries);
		gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "unlock");
	}
	/* should success always, because of NETSENDQ_FLAG_QUEUEABLE_IF_DOWN */
	(void)netsendq_add_entry(qhost, &dfc->qentry, 0);
}

static void dead_file_copy_free(struct dead_file_copy *);

/*
 * PREREQUISITE: nothing
 * LOCKS: giant_lock
 *  -> (dbq.mutex, dfc_keptq.mutex, host_busyq.mutex, host::back_channel_mutex)
 * SLEEPS: yes (giant_lock, dbq.mutex)
 *	but dfc_keptq.mutex, host_busyq.mutex and host::back_channel_mutex
 *	won't be blocked while sleeping.
 */
static void
handle_removal_result(struct netsendq_entry *qentryp)
{
	struct dead_file_copy *dfc = (struct dead_file_copy *)qentryp;
	const char diag[] = "handle_removal_result";

	if (dfc->qentry.result == GFARM_ERR_NO_ERROR ||
	    dfc->qentry.result == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY ||
	    !abstract_host_is_valid(dfc->qentry.abhost, diag)) {
		giant_lock(); /* necessary for dead_file_copy_free() */
		dead_file_copy_free(dfc); /* sleeps to wait for dbq.mutex */
		giant_unlock();
	} else {
		if (abstract_host_is_up(dfc->qentry.abhost) ||
		    !IS_CONNECTION_ERROR(dfc->qentry.result)) {
			/* unexpected error */
			gflog_error(GFARM_MSG_1002223,
			    "retrying removal of (%lld, %lld, %s): %s",
			    (long long)dfc->inum, (long long)dfc->igen,
			    abstract_host_get_name(dfc->qentry.abhost),
			    gfarm_error_string(dfc->qentry.result));
		}
		/* try again later to avoid busy loop */
		giant_lock();
		dead_file_copy_schedule_removal(dfc);
		giant_unlock();
	}
}

/*
 * FUNCTION:
 * check the policy whether it's ok to remove this obsolete replica or not.
 *
 * PREREQUISITE: giant_lock (at least for inode access)
 * LOCKS: maybe dfc_keptq.mutex
 * SLEEPS: maybe, but no for now
 *	but dfc_keptq.mutex won't be blocked while sleeping.
 *
 * the caller should check dfc->host is `valid' and `up' and `not busy'
 * before calling this function.
 * the caller should allow this function to sleep.
 *
 * this interface is exported for a use from a private extension
 */
int
dead_file_copy_is_removable_default(struct dead_file_copy *dfc)
{
	int desired = inode_desired_dead_file_copy(dfc->inum);

	if (desired == 0)
		return (1);

	/*
	 * XXX FIXME: check number of obsolete replicas,
	 * and allow removal of older ones.
	 * that will lock dfc_keptq.mutex, but shouldn't sleep while locking
	 * dfc_keptq.mutex.
	 */

	return (0);
}

/* this interface is made as a hook for a private extension */
int (*dead_file_copy_is_removable)(struct dead_file_copy *) =
	dead_file_copy_is_removable_default;

/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: XXX
 */
void
dead_file_copy_host_becomes_up(struct host *host)
{
	struct netsendq_entry *qe, *tmp;
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_host_becomes_up";

	/*
	 * in this case, we have to check dead_file_copy data for *all* hosts,
	 * not only the host which becomes up.
	 */

	/*
	 * for hosts except the host which becomes up:
	 *	the host which becomes up may have some replicas which
	 *	only exists on the host.  in that case, other hosts may
	 *	have pending dead_file_copy entries which processing is
	 *	deferred because only owner of the newest replica is down.
	 */
	gfarm_mutex_lock(&dfc_keptq.mutex, diag, "lock");

	/* giant_lock prevents dfc from being freed */
	GFARM_HCIRCLEQ_FOREACH_SAFE(qe, dfc_keptq.q, workq_entries, tmp) {
		dfc = (struct dead_file_copy *)qe;
		assert(dfc->is_kept);
		if (dead_file_copy_is_removable(dfc)) {
			dfc->is_kept = 0;
			GFARM_HCIRCLEQ_REMOVE(qe, workq_entries);
			dead_file_copy_schedule_removal(dfc);
		}
	}

	gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "unlock");

	/*
	 * for the host which becomes up:
	 *	we should start to send pending dead_file_copy entries.
	 *
	 * this is done by: netsendq_host_becomes_up();
	 */
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_keptq.mutex, removal_pendingq.mutex, removal_finishedq.mutex,
 *	host_busyq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex),
 *	but dfc_keptq.mutex, removal_pendingq.mutex, removal_pendingq.mutex
 *	and host_busyq.mutex won't be blocked while sleeping.
 */
void
dead_file_copy_host_removed(struct host *host)
{
	struct netsendq_entry *qe, *tmp;
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_host_removed";

	gfarm_mutex_lock(&dfc_keptq.mutex, diag, "lock");

	/* giant_lock prevents dfc from being freed */
	GFARM_HCIRCLEQ_FOREACH_SAFE(qe, dfc_keptq.q, workq_entries, tmp) {
		dfc = (struct dead_file_copy *)qe;
		if (abstract_host_to_host(dfc->qentry.abhost) != host)
			continue;
		assert(dfc->is_kept);
		dfc->is_kept = 0;
		GFARM_HCIRCLEQ_REMOVE(qe, workq_entries);
		/*
		 * to prevent functions which acquire dfc_kept.mutex
		 * from sleeping
		 */
		gfarm_mutex_unlock(&dfc_keptq.mutex, diag,
		    "unlock before sleeping");

		dead_file_copy_free(dfc);

		gfarm_mutex_lock(&dfc_keptq.mutex, diag,
		    "lock after sleeping");
	}

	gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "unlock");

	/* leave the host_busyq as is, because it will be handled shortly. */
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: XXX
 */
void
dead_file_copy_inode_status_changed(struct dead_file_copy_list *same_inode_list)
{
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_inode_status_changed";

	GFARM_HCIRCLEQ_FOREACH(dfc, same_inode_list->list, same_inode_copies) {
		/* kept && !kept_hard && removable ? */
		if (dfc->is_kept && dead_file_copy_is_removable(dfc)) {
			gfarm_mutex_lock(&dfc_keptq.mutex, diag, "lock");
			dfc->is_kept = 0;
			GFARM_HCIRCLEQ_REMOVE(&dfc->qentry, workq_entries);
			gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "unlock");
			dead_file_copy_schedule_removal(dfc);
		}
	}
}

/* prevent dfc from moved to removal_pendingq */
/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: XXX
 */
void
dead_file_copy_mark_kept(struct dead_file_copy *dfc)
{
	static const char diag[] = "dead_file_copy_mark_kept";

	/* sanity check */
	if (dfc->is_kept) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "dead copy %lld:%lld host %s: already kept",
		    (long long)dfc->inum, (long long)dfc->igen,
		    abstract_host_get_name(dfc->qentry.abhost));
		return;
	}

	gfarm_mutex_lock(&dfc_keptq.mutex, diag, "lock");

	GFARM_HCIRCLEQ_INSERT_TAIL(dfc_keptq.q, &dfc->qentry, workq_entries);
	dfc->is_kept = 1;

	gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "unlock");
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: XXX
 * SLEEPS: no
 */
int
dead_file_copy_count_by_inode(struct dead_file_copy_list *same_inode_list,
	gfarm_uint64_t igen, int up_only)
{
	int n = 0;
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_count_by_inode";

	if (same_inode_list == NULL)
		return (0);
	GFARM_HCIRCLEQ_FOREACH(dfc, same_inode_list->list, same_inode_copies) {
		if (dfc->igen == igen) /* handled by an invalid file_copy */
			continue;
		if (up_only ?
		    abstract_host_is_up(dfc->qentry.abhost) :
		    abstract_host_is_valid(dfc->qentry.abhost, diag))
			n++;
	}
	return (n);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: XXX
 * SLEEPS: no
 */
gfarm_error_t
dead_file_copy_info_by_inode(struct dead_file_copy_list *same_inode_list,
	gfarm_uint64_t igen, int up_only,
	int *np, char **hosts, gfarm_int64_t *gens, gfarm_int32_t *flags)
{
	int i = 0, n = *np;
	struct dead_file_copy *dfc;
	gfarm_int32_t flag;
	char *name;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "dead_file_copy_info_by_inode";

	if (same_inode_list == NULL) {
		*np = 0;
		return (GFARM_ERR_NO_ERROR);
	}
	GFARM_HCIRCLEQ_FOREACH(dfc, same_inode_list->list, same_inode_copies) {
		if (i >= n) /* this happens if host becomes up */
			break;

		if (dfc->igen == igen) /* handled by an invalid file_copy */
			continue;
		if (up_only) {
			if (!abstract_host_is_up(dfc->qentry.abhost))
				continue;
			flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY;
		} else {
			if (!abstract_host_is_valid(dfc->qentry.abhost, diag))
				continue;
			if (abstract_host_is_up(dfc->qentry.abhost))
				flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY;
			else
				flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY |
				       GFM_PROTO_REPLICA_FLAG_DEAD_HOST;
		}

		name = strdup_log(abstract_host_get_name(dfc->qentry.abhost),
		    diag);
		if (name == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		hosts[i] = name;
		gens[i] = dfc->igen;
		flags[i] = flag;
		i++;
	}


	if (e != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			free(hosts[i]);
	} else {
		*np = i;
	}
	return (e);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: XXX
 * SLEEPS: no
 */
int
dead_file_copy_existing(struct dead_file_copy_list *same_inode_list,
	gfarm_uint64_t igen, struct host *host)
{
	struct dead_file_copy *dfc;

	if (same_inode_list == NULL)
		return (0);

	GFARM_HCIRCLEQ_FOREACH(dfc, same_inode_list->list, same_inode_copies) {
		if (dfc->igen == igen &&
		    dfc->qentry.abhost == host_to_abstract_host(host))
			return (1);
	}
	return (0);
}

gfarm_ino_t
dead_file_copy_get_ino(struct dead_file_copy *dfc)
{
	return (dfc->inum);
}

gfarm_uint64_t
dead_file_copy_get_gen(struct dead_file_copy *dfc)
{
	return (dfc->igen);
}

struct host *
dead_file_copy_get_host(struct dead_file_copy *dfc)
{
	return (abstract_host_to_host(dfc->qentry.abhost));
}

static void
removal_finishedq_enqueue(struct dead_file_copy *dfc, gfarm_error_t e)
{
	netsendq_remove_entry(abstract_host_get_sendq(dfc->qentry.abhost),
	    &dfc->qentry, e);
}

static gfarm_int32_t
gfs_client_fhremove_result(void *p, void *arg, size_t size)
{
	struct peer *peer = p;
	struct dead_file_copy *dfc = arg;
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_recv_result(peer, host, size, diag, "");
	removal_finishedq_enqueue(dfc, e);
	return (e);
}

/* both giant_lock and peer_table_lock are held before calling this function */
static void
gfs_client_fhremove_free(void *p, void *arg)
{
#if 0
	struct peer *peer = p;
#endif
	struct dead_file_copy *dfc = arg;

	removal_finishedq_enqueue(dfc, GFARM_ERR_CONNECTION_ABORTED);
}

static void *
gfs_client_fhremove_request(void *closure)
{
	struct dead_file_copy *dfc = closure;
	gfarm_ino_t ino = dead_file_copy_get_ino(dfc);
	gfarm_int64_t gen = dead_file_copy_get_gen(dfc);
	struct host *host = dead_file_copy_get_host(dfc);
	gfarm_error_t e;
	static const char diag[] = "GFS_PROTO_FHREMOVE";

	e = gfs_client_send_request(host, NULL, diag,
	    gfs_client_fhremove_result, gfs_client_fhremove_free, dfc,
	    GFS_PROTO_FHREMOVE, "ll", ino, gen);
	netsendq_entry_was_sent(abstract_host_get_sendq(dfc->qentry.abhost),
	    &dfc->qentry);

	if (e != GFARM_ERR_NO_ERROR) {
		/* accessing dfc is only allowed if e != GFARM_ERR_NO_ERROR */
		if (e == GFARM_ERR_DEVICE_BUSY) {
			gflog_info(GFARM_MSG_1002284,
			    "%s(%lld, %lld, %s): "
			    "busy, shouldn't happen", diag,
			    (long long)dead_file_copy_get_ino(dfc),
			    (long long)dead_file_copy_get_gen(dfc),
			    host_name(dead_file_copy_get_host(dfc)));
		}
		removal_finishedq_enqueue(dfc, e);
	}

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

struct netsendq_type gfs_proto_fhremove_queue = {
	gfs_client_fhremove_request,
	handle_removal_result,
	0, /* will be initialized by gfs_proto_fhremove_request_window */
	NETSENDQ_FLAG_QUEUEABLE_IF_DOWN,
	NETSENDQ_TYPE_GFS_PROTO_FHREMOVE
};

/*
 * PREREQUISITE: nothing
 * SLEEPS: no
 */
static struct dead_file_copy *
dead_file_copy_alloc(gfarm_ino_t inum, gfarm_uint64_t igen, struct host *host)
{
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_alloc";

	GFARM_MALLOC(dfc);
	if (dfc == NULL) {
		gflog_debug(GFARM_MSG_1002228,
		    "%s(%lld, %lld, %s): no memory", diag,
		    (unsigned long long)inum, (unsigned long long)igen,
		    host_name(host));
		return (NULL);
	}
	netsendq_entry_init(&dfc->qentry, &gfs_proto_fhremove_queue);

	dfc->qentry.abhost = host_to_abstract_host(host);
	dfc->inum = inum;
	dfc->igen = igen;
	dfc->is_kept = 0;

	return (dfc);
}

struct dead_file_copy_list *
dead_file_copy_list_alloc(void)
{
	struct dead_file_copy_list *same_inode_list;

	GFARM_MALLOC(same_inode_list);
	if (same_inode_list == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "cannot allocate dead_file_copy_list");
		return (NULL);
	}
	GFARM_HCIRCLEQ_INIT(same_inode_list->list, same_inode_copies);
	return (same_inode_list);
}

/* will be called back from inode_dead_file_copy_added() */
void
dead_file_copy_list_add(struct dead_file_copy_list **dead_copiesp,
	struct dead_file_copy *dfc)
{
	struct dead_file_copy_list *same_inode_list = *dead_copiesp;

	if (same_inode_list == NULL) {
		same_inode_list = dead_file_copy_list_alloc();
		if (same_inode_list == NULL)
			return;
		*dead_copiesp = same_inode_list;
	}
	GFARM_HCIRCLEQ_INSERT_TAIL(same_inode_list->list,
	    dfc, same_inode_copies);
}

/* will be called back from inode_remove_replica_completed() */
int
dead_file_copy_list_free_check(struct dead_file_copy_list *same_inode_list)
{
	if (GFARM_HCIRCLEQ_EMPTY(same_inode_list->list, same_inode_copies)) {
		free(same_inode_list);
		return (1);
	}
	return (0);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_keptq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex)
 */
struct dead_file_copy *
dead_file_copy_new(gfarm_ino_t inum, gfarm_uint64_t igen, struct host *host,
	struct dead_file_copy_list **dead_copiesp)
{
	gfarm_error_t e;
	struct dead_file_copy *dfc;
	struct dead_file_copy_list *same_inode_list = *dead_copiesp;
	int same_inode_list_alloced = 0;

	if (same_inode_list == NULL) {
		same_inode_list = dead_file_copy_list_alloc();
		if (same_inode_list == NULL)
			return (NULL);
		same_inode_list_alloced = 1;
	}
	if ((dfc = dead_file_copy_alloc(inum, igen, host)) == NULL) {
		if (same_inode_list_alloced)
			free(same_inode_list);
		return (NULL);
	}

	e = db_deadfilecopy_add(inum, igen, host_name(host));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002229,
		    "db_deadfilecopy_add(%lld, %lld, %s): %s",
		    (unsigned long long)inum, (unsigned long long)igen,
		    host_name(host), gfarm_error_string(e));
#if 0
	else if (debug_mode)
		gflog_debug(GFARM_MSG_1002230,
		    "db_deadfilecopy_add(%lld, %lld, %s): added",
		    (unsigned long long)inum, (unsigned long long)igen,
		    host_name(host));
#endif

	GFARM_HCIRCLEQ_INSERT_TAIL(same_inode_list->list,
	    dfc, same_inode_copies);
	*dead_copiesp = same_inode_list;

	return (dfc);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_keptq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex)
 *	but dfc_keptq.mutex won't be blocked while sleeping.
 *
 * giant_lock is necessary, because dead_file_copy_host_becomes_up() or
 * dead_file_copy_host_removed() may be accessing this dfc.
 */
static void
dead_file_copy_free(struct dead_file_copy *dfc)
{
	gfarm_error_t e;
	static const char diag[] = "dead_file_copy_free";

	gfarm_mutex_lock(&dfc_keptq.mutex, diag, "keptq lock");
	if (dfc->is_kept) {
		dfc->is_kept = 0;
		GFARM_HCIRCLEQ_REMOVE(&dfc->qentry, workq_entries);
	}
	gfarm_mutex_unlock(&dfc_keptq.mutex, diag, "keptq unlock");

	e = db_deadfilecopy_remove(dfc->inum, dfc->igen,
	    abstract_host_get_name(dfc->qentry.abhost));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002232,
		    "db_deadfilecopy_remove(%lld, %lld, %s): %s",
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    abstract_host_get_name(dfc->qentry.abhost),
		    gfarm_error_string(e));

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR)
		gflog_trace(GFARM_MSG_1003279,
		    "%lld/////DELREPLICA/%s/%d/%s/%lld/%lld///////",
		    (unsigned long long)trace_log_get_sequence_number(),
		    gfarm_host_get_self_name(), gfmd_port,
		    abstract_host_get_name(dfc->qentry.abhost),
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen);

	GFARM_HCIRCLEQ_REMOVE(dfc, same_inode_copies);
	inode_remove_replica_completed(dfc->inum, dfc->igen,
	    abstract_host_to_host(dfc->qentry.abhost));

	netsendq_entry_destroy(&dfc->qentry);
	free(dfc);
}

/* The memory owner of `hostname' is changed to dead_file_copy.c */
void
dead_file_copy_add_one(void *closure,
	gfarm_ino_t inum, gfarm_uint64_t igen, char *hostname)
{
	struct host *host = host_lookup(hostname);
	struct dead_file_copy *dfc = NULL;
	static const char diag[] = "dead_file_copy_add_one";

	/* XXX FIXME: do not remove, if latest one is inaccessible */

	if (host == NULL) {
		gflog_error(GFARM_MSG_1002233,
		    "%s: inode %lld:%lld: no host %s",
		    diag, (long long)inum, (long long)igen, hostname);
	} else if ((dfc = dead_file_copy_alloc(inum, igen, host)) == NULL) {
		gflog_error(GFARM_MSG_1002234,
		    "%s: dead replica(%lld, %lld, %s): no memory",
		    diag, (long long)inum, (long long)igen, hostname);
	}
	free(hostname);

	if (dfc != NULL) {
		inode_dead_file_copy_added(inum, igen, host, dfc);
		if (dead_file_copy_is_removable(dfc))
			dead_file_copy_schedule_removal(dfc);
		else
			dead_file_copy_mark_kept(dfc);
	}

}

void
dead_file_copy_init_load(void)
{
	gfarm_error_t e;

	e = db_deadfilecopy_load(NULL, dead_file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		gflog_error(GFARM_MSG_1000362,
		    "loading deadfilecopy: %s", gfarm_error_string(e));
}

void
dead_file_copy_init(int is_master)
{
	static const char diag[] = "dead_file_copy_init";

	gfs_proto_fhremove_queue.window_size =
	    gfs_proto_fhremove_request_window;

	gfarm_mutex_init(&dfc_keptq.mutex, diag, "dfc_keptq");
	GFARM_HCIRCLEQ_INIT(dfc_keptq.q, workq_entries);

	if (is_master)
		dead_file_copy_init_load();
}

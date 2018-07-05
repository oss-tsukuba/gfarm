#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "thrsubr.h"
#include "nanosec.h"

#include "timespec.h"
#include "context.h"
#include "config.h"
#include "gfm_proto.h"

#include "subr.h"
#include "callout.h"
#include "db_access.h"
#include "host.h"
#include "inode.h"
#include "gfmd.h"	/* sync_protocol_get_thrpool() */

struct dead_file_copy {
	struct dead_file_copy *allq_next, *allq_prev;
	struct dead_file_copy *workq_next, *workq_prev;

	gfarm_ino_t inum;
	gfarm_uint64_t igen;
	struct host *host;

	enum dead_file_copy_state {
		dfcstate_deferred,	/* not on any dfc_workq */
		dfcstate_kept,		/* do not move to removal_pendingq */
		dfcstate_lost,		/* not on any dfc_workq */
		dfcstate_pending,	/* on removal_pendingq */
		dfcstate_in_flight,	/* during protocol process */
		dfcstate_finished,	/* on removal_finishedq */
		dfcstate_finalizing,	/* during finalize */
		dfcstate_busy		/* on host_busyq */
	} state;

	gfarm_int32_t result;	/* available if state == dfsstate_finsihed */

	pthread_mutex_t mutex; /* currently only used to protect dfc->state */
};

/*
 * state transitions:
 *
 *	(created)
 *		-> dfcstate_deferred
 *
 *	dfcstate_deferred
 *		-> dfcstate_kept
 *		-> dfcstate_lost
 *		-> dfcstate_pending
 *		-> dfcstate_finished
 *		-> (freed) ... needs giant_lock()
 *
 *	dfcstate_kept
 *		-> dfcstate_deferred
 *		-> dfcstate_lost
 *		-> dfcstate_pending
 *
 *	dfcstate_lost
 *		-> dfcstate_finished
 *
 *	dfcstate_pending
 *		-> dfcstate_deferred
 *		-> dfcstate_in_flight
 *
 *	dfcstate_in_flight
 *		-> dfcstate_deferred
 *		-> dfcstate_busy
 *		-> dfcstate_finished
 *
 *	dfcstate_finished
 *		-> dfcstate_finalizing
 *
 *	dfcstate_finalizing
 *		-> dfcstate_busy
 *		-> (freed) ... needs giant_lock()
 *
 *	dfcstate_busy
 *		-> dfcstate_deferred
 *		-> dfcstate_pending
 *
 */

struct dfc_allq {
	pthread_mutex_t mutex;

	struct dead_file_copy q; /* dummy head of doubly linked circular list */
};

/* IMPORTANT NOTE: functions should not sleep while holding dfc_allq.mutex */
static struct dfc_allq dfc_allq = {
	PTHREAD_MUTEX_INITIALIZER,
	{ &dfc_allq.q, &dfc_allq.q, NULL, NULL }
};

struct dfc_workq {
	pthread_mutex_t mutex;
	pthread_cond_t not_empty;

	struct dead_file_copy q; /* dummy head of doubly linked circular list */
};

/*
 * IMPORTANT NOTE:
 * functions should not sleep while holding removal_pendingq.mutex
 */
static struct dfc_workq removal_pendingq = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	{ NULL, NULL, &removal_pendingq.q, &removal_pendingq.q }
};

/*
 * IMPORTANT NOTE:
 * functions should not sleep while holding removal_pendingq.mutex
 */
static struct dfc_workq removal_finishedq = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	{ NULL, NULL, &removal_finishedq.q, &removal_finishedq.q }
};

/*
 * IMPORTANT NOTE:
 * functions should not sleep while holding host_busyq.mutex
 */
static struct dfc_workq host_busyq = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	{ NULL, NULL, &host_busyq.q, &host_busyq.q }
};

static void
dfc_workq_enqueue(struct dfc_workq *q, struct dead_file_copy *dfc,
	enum dead_file_copy_state new_state, const char *diag)
{
	gfarm_mutex_lock(&q->mutex, diag, "lock");

	dfc->workq_next = &q->q;
	dfc->workq_prev = q->q.workq_prev;
	q->q.workq_prev->workq_next = dfc;
	q->q.workq_prev = dfc;

	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	dfc->state = new_state;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

	gfarm_cond_signal(&q->not_empty, diag, "not empty");
	gfarm_mutex_unlock(&q->mutex, diag, "unlock");
}

static struct dead_file_copy *
dfc_workq_dequeue(struct dfc_workq *q, enum dead_file_copy_state new_state,
	const char *diag)
{
	struct dead_file_copy *dfc;

	gfarm_mutex_lock(&q->mutex, diag, "lock");

	while (q->q.workq_next == &q->q) {
		gfarm_cond_wait(&q->not_empty, &q->mutex, diag, "not empty");
	}

	dfc = q->q.workq_next;

	/* i.e. dfc_workq_remove(q->q.workq_next) */
	q->q.workq_next = dfc->workq_next;
	dfc->workq_next->workq_prev = &q->q;

	dfc->workq_next = dfc->workq_prev = NULL; /* to be sure */

	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	dfc->state = new_state;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

	gfarm_mutex_unlock(&q->mutex, diag, "unlock");
	return (dfc);
}

static void
dfc_workq_remove(struct dead_file_copy *dfc)
{
	dfc->workq_next->workq_prev = dfc->workq_prev;
	dfc->workq_prev->workq_next = dfc->workq_next;
}

/*
 * PREREQUISITE: nothing
 * LOCKS: dfc_workq::mutex
 * SLEEPS: no
 */
static void
dfc_workq_host_remove(struct dfc_workq *q, struct host *host, const char *diag)
{
	struct dead_file_copy *dfc, *next;

	gfarm_mutex_lock(&q->mutex, diag, "lock");

	/* dfc_allq.mutex prevents dfc from being freed */
	for (dfc = q->q.workq_next; dfc != &q->q; dfc = next) {
		next = dfc->workq_next;

		if (dfc->host != host)
			continue;

		dfc_workq_remove(dfc);

		gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
		dfc->state = dfcstate_deferred;
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

		dfc->workq_next = dfc->workq_prev = NULL; /* to be sure */
	}

	gfarm_mutex_unlock(&q->mutex, diag, "unlock");
}

void
removal_pendingq_enqueue(struct dead_file_copy *dfc)
{
	enum dead_file_copy_state state;
	static const char diag[] = "removal_pendingq_enqueue";

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	state = dfc->state;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
	if (state != dfcstate_deferred &&
	    state != dfcstate_kept &&
	    state != dfcstate_busy) {
		gflog_fatal(GFARM_MSG_1002221, "%s(%lld, %lld, %s): "
		    "insane state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), state);
		return;
	}

	dfc_workq_enqueue(&removal_pendingq, dfc, dfcstate_pending, diag);
}

struct dead_file_copy *
removal_pendingq_dequeue(void)
{
	static const char diag[] = "removal_pendingq_dequeue";

	return (dfc_workq_dequeue(&removal_pendingq, dfcstate_in_flight,
	    diag));
}

void
removal_finishedq_enqueue(struct dead_file_copy *dfc, gfarm_int32_t result)
{
	static const char diag[] = "removal_finishedq_enqueue";

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	if (dfc->state != dfcstate_deferred &&
	    dfc->state != dfcstate_lost &&
	    dfc->state != dfcstate_in_flight) {
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		gflog_fatal(GFARM_MSG_1002222, "%s(%lld, %lld, %s): "
		    "insane state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), dfc->state);
		return;
	}
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

	dfc->result = result;
	dfc_workq_enqueue(&removal_finishedq, dfc, dfcstate_finished, diag);
}

static struct dead_file_copy *
removal_finishedq_dequeue(void)
{
	static const char diag[] = "removal_finishedq_dequeue";

	return (dfc_workq_dequeue(&removal_finishedq, dfcstate_finalizing,
	    diag));
}

static void dead_file_copy_free(struct dead_file_copy *);
void host_busyq_enqueue(struct dead_file_copy *);

/*
 * PREREQUISITE: nothing
 * LOCKS: giant_lock
 *  -> (dbq.mutex, dfc_allq.mutex, host_busyq.mutex, host::back_channel_mutex)
 * SLEEPS: yes (giant_lock, dbq.mutex)
 *	but dfc_allq.mutex, host_busyq.mutex and host::back_channel_mutex
 *	won't be blocked while sleeping.
 */
static void
handle_removal_result(struct dead_file_copy *dfc)
{
	static const char diag[] = "handle_removal_result";

	/* giant_lock is necessary before calling dead_file_copy_free() */
	giant_lock();

	if (dfc->result == GFARM_ERR_NO_ERROR ||
	    dfc->result == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		dead_file_copy_free(dfc); /* sleeps to wait for dbq.mutex */
	} else if (host_is_up(dfc->host)) {
		/* unexpected error, try again later to avoid busy loop */
		gflog_notice(GFARM_MSG_1002223,
		    "waiting removal of (%lld, %lld, %s): %s",
		    (long long)dfc->inum, (long long)dfc->igen,
		    host_name(dfc->host), gfarm_error_string(dfc->result));
		host_busyq_enqueue(dfc);
	} else if (!host_is_valid(dfc->host)) {
		dead_file_copy_free(dfc); /* sleeps to wait for dbq.mutex */
	} else {
		gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
		dfc->state = dfcstate_deferred;
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
	}

	giant_unlock();
}

static void *
removal_finalizer(void *arg)
{
	gfarm_error_t e;
	static const char diag[] = "removal_finalizer";

	e = gfarm_pthread_set_priority_minimum(diag);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1005035,
		    "%s: set_priority_minimum: %s",
		    diag, gfarm_error_string(e));

	for (;;) {
		handle_removal_result(removal_finishedq_dequeue());
	}

	/*NOTREACHED*/
	return (NULL);
}

/*
 * FUNCTION:
 * check the policy whether it's ok to remove this obsolete replica or not.
 *
 * PREREQUISITE: giant_lock (at least for inode access)
 * LOCKS: maybe dfc_allq.mutex
 * SLEEPS: maybe, but no for now
 *	but dfc_allq.mutex won't be blocked while sleeping.
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
	 * that will lock dfc_allq.mutex, but shouldn't sleep while locking
	 * dfc_allq.mutex.
	 */

	return (0);
}

/* this interface is made as a hook for a private extension */
int (*dead_file_copy_is_removable)(struct dead_file_copy *) =
	dead_file_copy_is_removable_default;

void
host_busyq_enqueue(struct dead_file_copy *dfc)
{
	static const char diag[] = "host_busyq_enqueue";

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	if (dfc->state != dfcstate_in_flight &&
	    dfc->state != dfcstate_finalizing) {
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		gflog_fatal(GFARM_MSG_1002224, "%s(%lld, %lld, %s): "
		    "insane state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), dfc->state);
		return;
	}
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

	dfc_workq_enqueue(&host_busyq, dfc, dfcstate_busy, diag);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: giant_lock -> host_busyq.mutex -> removal_pendingq.mutex
 * SLEEPS: depends on whether dead_file_copy_is_removable() sleeps or not,
 *	but host_busyq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 *
 * dead_file_copy_is_removable() needs giant_lock, and may sleep.
 *
 * this function is called via callout.
 */
static void
dead_file_copy_host_busyq_scan(void)
{
	struct dead_file_copy *dfc, *next;
	time_t now;
	int busy, up;
	static const char diag[] = "dead_file_copy_host_busyq_scan";

	giant_lock();
	gfarm_mutex_lock(&host_busyq.mutex, diag, "host_busyq lock");

	now = time(NULL);

	/* dfc_allq.mutex prevents dfc from being freed */
	for (dfc = host_busyq.q.workq_next; dfc != &host_busyq.q; dfc = next) {
		next = dfc->workq_next;

		/* this may make the host down */
		busy = host_check_busy(dfc->host, now);

		/* so, the following should be called after host_check_busy */
		up = host_is_up(dfc->host);

		if (!up) {
			dfc_workq_remove(dfc);
			gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
			dfc->state = dfcstate_deferred;
			gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

			/* to be sure */
			dfc->workq_next = dfc->workq_prev = NULL;

		} else if (!busy) {
			dfc_workq_remove(dfc);

			if (dead_file_copy_is_removable(dfc)) {
				removal_pendingq_enqueue(dfc);
			} else {
				/* leave it as dfcstate_deferred */
				gfarm_mutex_lock(&dfc->mutex,
				    diag, "dfc state");
				dfc->state = dfcstate_deferred;
				gfarm_mutex_unlock(&dfc->mutex,
				    diag, "dfc state");

				/* to be sure */
				dfc->workq_next = dfc->workq_prev = NULL;
			}
		} /* otherwise it's still busy, so, leave it as is. */
	}

	gfarm_mutex_unlock(&host_busyq.mutex, diag, "host_busyq unlock");
	giant_unlock();
}

static void host_busyq_scanner_schedule(void);

/* this function is called via callout */
static void *
host_busyq_scanner(void *arg)
{
	/*
	 * XXX FIXME
	 * we should periodically check possibility of automatic replicaton too.
	 */
	dead_file_copy_host_busyq_scan();

	host_busyq_scanner_schedule();

	/* this return value won't be used, because this thread is detached */
	return (NULL);
}

static struct callout *host_busyq_scanner_callout;

static void
host_busyq_scanner_schedule(void)
{
	/*
	 * Q: why we use gfarm_metadb_heartbeat_interval here?
	 * A: see the comment in callout_schedule_common().
	 *
	 * Q: why we use sync_protocol_get_thrpool() here?
	 * A: because this thread needs giant_lock, and
	 *    delay of the invocation is nearly harmless
	 *    at least from deadlock point of view.
	 */
	callout_reset(host_busyq_scanner_callout,
	    gfarm_metadb_heartbeat_interval * 1000000,
	    sync_protocol_get_thrpool(), host_busyq_scanner, NULL);
}

static void
host_busyq_scanner_init(void)
{
	host_busyq_scanner_callout = callout_new();
	host_busyq_scanner_schedule();
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex
 * SLEEPS: no
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
static struct dead_file_copy *
dead_file_copy_lookup(gfarm_ino_t inum, gfarm_uint64_t igen, struct host *host,
	const char *diag)
{
	struct dead_file_copy *dfc;
	int found = 0;

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");

	/* giant_lock prevents dfc from being freed */
	for (dfc = dfc_allq.q.allq_next; dfc != &dfc_allq.q;
	     dfc = dfc->allq_next) {
		/*
		 * dfc->mutex lock is not necessary,
		 * because dfc->inum, dfc->igen and dfc->host are constant
		 */
		if (dfc->inum == inum &&
		    dfc->igen == igen &&
		    dfc->host == host) {
			found = 1;
			break;
		}
	}

	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");
	return (found ? dfc : NULL);
}

static struct gfarm_timespec remove_scan_log_time = { 0, 0 };
static struct gfarm_timespec remove_scan_max_time = { 0, 0 };
static struct gfarm_timespec remove_scan_total_time = { 0, 0 };
static unsigned long long remove_scan_max_process_count = 0;
static unsigned long long remove_scan_max_scan_count = 0;
static unsigned long long remove_scan_total_process_count = 0;
static unsigned long long remove_scan_total_scan_count = 0;
static unsigned long long remove_scan_total_calls = 0;

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe,
 *	but dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 *
 * dead_file_copy_is_removable() needs giant_lock, and may sleep.
 */
static long long
dead_file_copy_scan_deferred(gfarm_ino_t inum, struct host *host,
	int (*filter)(struct dead_file_copy *, gfarm_ino_t, struct host *),
	const char *diag)
{
	struct dead_file_copy *dfc;
	time_t now = time(NULL);
	int busy, already_removed;
	enum dead_file_copy_state state;
	struct timespec t1, t2;
	struct gfarm_timespec gt1, gt2, elapsed, next_log_time;
	unsigned long long process_count = 0, scan_count = 0;

	gfarm_gettime(&t1);

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");

	/* giant_lock prevents dfc from being freed */
	for (dfc = dfc_allq.q.allq_next; dfc != &dfc_allq.q;
	     dfc = dfc->allq_next) {

		++scan_count;
		gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
		state = dfc->state;
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		if (state != dfcstate_deferred && state != dfcstate_lost)
			continue;

		if (state == dfcstate_lost) {
			already_removed = 1;
		} else if (host_is_valid(dfc->host)) {
			if (!(*filter)(dfc, inum, host))
				continue;
			already_removed = 0;
		} else { /* if host is invalid. remove this dfc anyway */
			already_removed = 1;
		}

		/*
		 * to prevent functions which acquire dfc_all.mutex
		 * from sleeping
		 */
		gfarm_mutex_unlock(&dfc_allq.mutex,
		    diag, "unlock before sleeping");

		++process_count;
		if (already_removed) {
			removal_finishedq_enqueue(dfc, GFARM_ERR_NO_ERROR);
		} else {
			busy = host_check_busy(dfc->host, now);
			if (!busy && dead_file_copy_is_removable(dfc))
				removal_pendingq_enqueue(dfc);
		}

		gfarm_mutex_lock(&dfc_allq.mutex,
		    diag, "lock after sleeping");
	}

	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");

	/* the static variables below are protected by giant_lock */

	gfarm_gettime(&t2);
	gt1.tv_sec = t1.tv_sec; gt1.tv_nsec = t1.tv_nsec;
	gt2.tv_sec = t2.tv_sec; gt2.tv_nsec = t2.tv_nsec;

	elapsed = gt2;
	gfarm_timespec_sub(&elapsed, &gt1);

	if (gfarm_timespec_cmp(&remove_scan_max_time, &elapsed) < 0) {
		remove_scan_max_time = elapsed;
		remove_scan_max_process_count = process_count;
		remove_scan_max_scan_count = scan_count;
	}

	gfarm_timespec_add(&remove_scan_total_time, &elapsed);
	remove_scan_total_process_count += process_count;
	remove_scan_total_scan_count += scan_count;
	remove_scan_total_calls++;

	/*
	 * calculate next_log_time here to immediately reflect the change to
	 * gfarm_metadb_remove_scan_log_interval by "gfstatus -Mm"
	 */
	next_log_time = remove_scan_log_time;
	next_log_time.tv_sec += gfarm_metadb_remove_scan_log_interval;
	if (gfarm_timespec_cmp(&gt2, &next_log_time) >= 0) {
		gflog_info(GFARM_MSG_1005036,
		    "dead_file_copy scan: "
		    "max: %g seconds (%llu scans %llu processed), "
		    "%llu times average: %g seconds (%g scans %g processed)",
		    remove_scan_max_time.tv_sec +
		    (double)remove_scan_max_time.tv_nsec
		    / GFARM_SECOND_BY_NANOSEC,
		    remove_scan_max_scan_count,
		    remove_scan_max_process_count,
		    remove_scan_total_calls,
		    (remove_scan_total_time.tv_sec +
		     (double)remove_scan_total_time.tv_nsec
		     / GFARM_SECOND_BY_NANOSEC) / remove_scan_total_calls,
		    (double)remove_scan_total_scan_count
		    / remove_scan_total_calls,
		    (double)remove_scan_total_process_count
		    / remove_scan_total_calls);
		remove_scan_log_time = gt2;
		remove_scan_max_time.tv_sec = 0;
		remove_scan_max_time.tv_nsec = 0;
		remove_scan_max_process_count = 0;
		remove_scan_max_scan_count = 0;
		remove_scan_total_time.tv_sec = 0;
		remove_scan_total_time.tv_nsec = 0;
		remove_scan_total_process_count = 0;
		remove_scan_total_scan_count = 0;
		remove_scan_total_calls = 0;
	}
	return ((long long)(t2.tv_sec - t1.tv_sec) * GFARM_SECOND_BY_NANOSEC
	    + (t2.tv_nsec - t1.tv_nsec));
}

static int
transparent_filter(struct dead_file_copy *dfc,
	gfarm_ino_t inum, struct host *host)
{
	return (1);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe,
 *	but dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 */
static long long
dead_file_copy_scan_deferred_all_run(void)
{
	static const char diag[] = "dead_file_copy_scan_deferred_all_run";

	return (dead_file_copy_scan_deferred(
	    0, NULL, transparent_filter, diag));

	/* leave the host_busyq as is, because it will be handled shortly. */
}

/*
 * PREREQUISITE: nothing
 * LOCKS: removal_pendingq.mutex, host_busyq.mutex
 * SLEEPS: no
 */
void
dead_file_copy_host_becomes_down(struct host *host)
{
	dfc_workq_host_remove(&removal_pendingq, host,
	    "host down: removal_pendingq");

#if 0
	/*
	 * DO NOT CALL THIS, because this may cause dead lock.
	 * see https://sourceforge.net/apps/trac/gfarm/ticket/420
	 *	gfmd dead lock at gfsd back_channel disconnection
	 */
	dfc_workq_host_remove(&host_busyq, host,
	    "host down: host_busyq");
#endif
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe,
 *	but dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 */
static long long
dead_file_copy_host_becomes_up_run(struct host *host)
{
	static const char diag[] = "dead_file_copy_host_becomes_up_run";

	/*
	 * in this case, we have to check dead_file_copy data for *all* hosts,
	 * not only the host which becomes up.
	 *
	 * for the host which becomes up:
	 *	we should start to send pending dead_file_copy entries.
	 * for hosts except the host which becomes up:
	 *	the host which becomes up may have some replicas which
	 *	only exists on the host.  in that case, other hosts may
	 *	have pending dead_file_copy entries which processing is
	 *	deferred because only owner of the newest replica is down.
	 */
	return (dead_file_copy_scan_deferred(
	    0, NULL, transparent_filter, diag));

	/* leave the host_busyq as is, because it will be handled shortly. */
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex, removal_finishedq.mutex,
 *	host_busyq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex),
 *	but dfc_allq.mutex, removal_pendingq.mutex, removal_pendingq.mutex
 *	and host_busyq.mutex won't be blocked while sleeping.
 */
void
dead_file_copy_host_removed(struct host *host)
{
	struct dead_file_copy *dfc, *next;
	enum dead_file_copy_state state;
	static const char diag[] = "dead_file_copy_host_removed";

	dead_file_copy_host_becomes_down(host);

	dfc_workq_host_remove(&removal_finishedq, host,
	    "host removed: removal_finishedq");

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");

	/* giant_lock prevents dfc from being freed */
	for (dfc = dfc_allq.q.allq_next; dfc != &dfc_allq.q; dfc = next) {
		next = dfc->allq_next;

		if (dfc->host != host)
			continue;

		gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
		state = dfc->state;
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		if (state != dfcstate_deferred) {
			gflog_debug(GFARM_MSG_1002225,
			    "%s: defer removal of (%lld, %lld, %s): %d", diag,
			    (unsigned long long)dfc->inum,
			    (unsigned long long)dfc->igen,
			    host_name(dfc->host), state);
			continue;
		}

		/*
		 * to prevent functions which acquire dfc_all.mutex
		 * from sleeping
		 */
		gfarm_mutex_unlock(&dfc_allq.mutex, diag,
		    "unlock before sleeping");

		dead_file_copy_free(dfc);

		gfarm_mutex_lock(&dfc_allq.mutex, diag,
		    "lock after sleeping");
	}

	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");

	/* leave the host_busyq as is, because it will be handled shortly. */
}

static int
inode_and_host_filter(struct dead_file_copy *dfc,
	gfarm_ino_t inum, struct host *host)
{
	return (dfc->inum == inum && dfc->host == host);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe,
 *	but dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
static long long
dead_file_copy_replica_status_changed_run(gfarm_ino_t inum, struct host *host)
{
	static const char diag[] = "dead_file_copy_replica_status_changed_run";

	return (dead_file_copy_scan_deferred(
	    inum, host, inode_and_host_filter, diag));
}

static int
inode_filter(struct dead_file_copy *dfc,
	gfarm_ino_t inum, struct host *host)
{
	return (dfc->inum == inum);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, removal_pendingq.mutex
 * SLEEPS: maybe,
 *	but dfc_allq.mutex and removal_pendingq.mutex
 *	won't be blocked while sleeping.
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
static long long
dead_file_copy_inode_status_changed_run(gfarm_ino_t inum)
{
	static const char diag[] = "dead_file_copy_inode_status_changed_run";

	return (dead_file_copy_scan_deferred(
	    inum, NULL, inode_filter, diag));
}

static struct {
	pthread_mutex_t mutex;
	pthread_cond_t requested;
	int need_to_run;
	gfarm_ino_t inum;
	struct host *host;
} dead_file_copy_scan = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	0,
	0,
	NULL
};

static const char dead_file_copy_scan_diag[] = "dead_file_copy_scan";
static const char dead_file_copy_scan_requested_diag[] =
	"dead_file_copy_scan_requested";

void
dead_file_copy_scan_deferred_all(void)
{
	static const char diag[] = "dead_file_copy_scan_deferred_all";

	gfarm_mutex_lock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);

	/* scan all */
	dead_file_copy_scan.inum = 0;
	dead_file_copy_scan.host = NULL;

	dead_file_copy_scan.need_to_run = 1;
	gfarm_cond_signal(
	    &dead_file_copy_scan.requested, diag,
	    dead_file_copy_scan_requested_diag);

	gfarm_mutex_unlock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);
}

void
dead_file_copy_host_becomes_up(struct host *host)
{
	static const char diag[] = "dead_file_copy_host_becomes_up";

	gfarm_mutex_lock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);

	if (!dead_file_copy_scan.need_to_run ||
	    dead_file_copy_scan.host == host) {
		/* only this host */
		dead_file_copy_scan.inum = 0;
		dead_file_copy_scan.host = host;
	} else { /* otherwise scan all */
		dead_file_copy_scan.inum = 0;
		dead_file_copy_scan.host = NULL;
	}

	dead_file_copy_scan.need_to_run = 1;
	gfarm_cond_signal(
	    &dead_file_copy_scan.requested, diag,
	    dead_file_copy_scan_requested_diag);

	gfarm_mutex_unlock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);
}

void
dead_file_copy_replica_status_changed(gfarm_ino_t inum, struct host *host)
{
	static const char diag[] = "dead_file_copy_replica_status_changed";

	gfarm_mutex_lock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);

	if (!dead_file_copy_scan.need_to_run ||
	    (dead_file_copy_scan.inum == inum &&
	     dead_file_copy_scan.host == host)) {
		/* only this replica */
		dead_file_copy_scan.inum = inum;
		dead_file_copy_scan.host = host;
	} else if (dead_file_copy_scan.inum == inum) {
		/* only this inode */
		dead_file_copy_scan.host = NULL;
	} else { /* otherwise scan all */
		dead_file_copy_scan.inum = 0;
		dead_file_copy_scan.host = NULL;
	}

	dead_file_copy_scan.need_to_run = 1;
	gfarm_cond_signal(
	    &dead_file_copy_scan.requested, diag,
	    dead_file_copy_scan_requested_diag);

	gfarm_mutex_unlock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);
}

void
dead_file_copy_inode_status_changed(gfarm_ino_t inum)
{
	static const char diag[] = "dead_file_copy_inode_status_changed";

	gfarm_mutex_lock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);

	if (!dead_file_copy_scan.need_to_run ||
	    dead_file_copy_scan.inum == inum) {
		/* only this inode */
		dead_file_copy_scan.inum = inum;
		dead_file_copy_scan.host = NULL;
	} else { /* otherwise scan all */
		dead_file_copy_scan.inum = 0;
		dead_file_copy_scan.host = NULL;
	}

	dead_file_copy_scan.need_to_run = 1;
	gfarm_cond_signal(
	    &dead_file_copy_scan.requested, diag,
	    dead_file_copy_scan_requested_diag);

	gfarm_mutex_unlock(&dead_file_copy_scan.mutex,
	    diag, dead_file_copy_scan_diag);
}

static void *
dead_file_copy_scanner(void *arg)
{
	gfarm_error_t e;
	static const char diag[] = "dead_file_copy_scanner";
	gfarm_ino_t inum;
	struct host *host;
	long long t;
	int interval_factor;

	e = gfarm_pthread_set_priority_minimum(diag);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_info(GFARM_MSG_1005037,
		    "%s: set_priority_minimum: %s",
		    diag, gfarm_error_string(e));

	for (;;) {
		gfarm_mutex_lock(&dead_file_copy_scan.mutex,
		    diag, dead_file_copy_scan_diag);

		while (!dead_file_copy_scan.need_to_run)
			gfarm_cond_wait(
			    &dead_file_copy_scan.requested,
			    &dead_file_copy_scan.mutex, diag,
			    dead_file_copy_scan_requested_diag);
		inum = dead_file_copy_scan.inum;
		host = dead_file_copy_scan.host;

		dead_file_copy_scan.need_to_run = 0;
		dead_file_copy_scan.inum = 0;
		dead_file_copy_scan.host = NULL;

		gfarm_mutex_unlock(&dead_file_copy_scan.mutex,
		    diag, dead_file_copy_scan_diag);

		giant_lock();

		if (inum != 0 && host != NULL) {
			t = dead_file_copy_replica_status_changed_run(
			    inum, host);
		} else if (inum != 0) {
			t = dead_file_copy_inode_status_changed_run(inum);
		} else if (host != NULL) {
			t = dead_file_copy_host_becomes_up_run(host);
		} else {
			t = dead_file_copy_scan_deferred_all_run();
		}

		/* giant_lock is needed for GFM_PROTO_CONFIG_SET */
		interval_factor = gfarm_metadb_remove_scan_interval_factor;

		giant_unlock();

		if (t <= 0)
			t = 1;
		gfarm_nanosleep(t * interval_factor);
	}

	/*NOTREACHED*/
	return (NULL);
}


/* used for deferred -> kept: prevent dfc from moved to removal_pendingq */
void
dead_file_copy_mark_kept(struct dead_file_copy *dfc)
{
	static const char diag[] = "dead_file_copy_mark_kept";

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	if (dfc->state != dfcstate_deferred) {
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		gflog_fatal(GFARM_MSG_1002226, "%s(%lld, %lld, %s): "
		    "insane state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), dfc->state);
		return;
	}
	dfc->state = dfcstate_kept;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
}

/* used for kept -> deferred: make dfc movable to removal_pendingq */
void
dead_file_copy_mark_deferred(struct dead_file_copy *dfc)
{
	static const char diag[] = "dead_file_copy_mark_deferred";

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	if (dfc->state != dfcstate_kept) {
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		gflog_fatal(GFARM_MSG_1002227, "%s(%lld, %lld, %s): "
		    "insane state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), dfc->state);
		return;
	}
	dfc->state = dfcstate_deferred;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
}

/* used for kept/deferred -> lost: move dfc to removal_finishedq */
gfarm_error_t
dead_file_copy_mark_lost(
	gfarm_ino_t inum, gfarm_uint64_t igen, struct host *host)
{
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_mark_lost";

	dfc = dead_file_copy_lookup(inum, igen, host, diag);
	if (dfc == NULL) {
		gflog_notice(GFARM_MSG_1004319, "%s(%lld, %lld, %s): "
		    "not found", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host));
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	/* sanity check */
	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	if (dfc->state != dfcstate_kept && dfc->state != dfcstate_deferred) {
		gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
		gflog_notice(GFARM_MSG_1004320, "%s(%lld, %lld, %s): "
		    "cannot remove due to unexpected state %d", diag,
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), dfc->state);
		return (GFARM_ERR_FILE_BUSY);
	}
	dfc->state = dfcstate_lost;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");
	return (GFARM_ERR_NO_ERROR);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: dfc_allq.mutex, host::back_channel_mutex
 * SLEEPS: no
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
int
dead_file_copy_count_by_inode(gfarm_ino_t inum, gfarm_uint64_t igen,
	int up_only)
{
	int n = 0;
	struct dead_file_copy *dfc;
	static const char diag[] = "dead_file_copy_count_by_inode";

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");

	/* dfc_allq.mutex prevents dfc from being freed */
	for (dfc = dfc_allq.q.allq_next; dfc != &dfc_allq.q;
	     dfc = dfc->allq_next) {

		/* dfc->igen == igen case is handled by an invalid file_copy */
		if (dfc->inum == inum && dfc->igen != igen &&
		    (up_only ?
		    host_is_up(dfc->host) : host_is_valid(dfc->host)))
			n++;
	}

	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");
	return (n);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: dfc_allq.mutex, host::back_channel_mutex
 * SLEEPS: no
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
gfarm_error_t
dead_file_copy_info_by_inode(gfarm_ino_t inum, gfarm_uint64_t igen, int up_only,
	int *np, char **hosts, gfarm_int64_t *gens, gfarm_int32_t *flags)
{
	int i = 0, n = *np;
	struct dead_file_copy *dfc;
	gfarm_int32_t flag;
	char *name;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "dead_file_copy_info_by_inode";

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");

	/* dfc_allq.mutex prevents dfc from being freed */
	for (dfc = dfc_allq.q.allq_next; dfc != &dfc_allq.q;
	     dfc = dfc->allq_next) {
		if (i >= n)
			break;

		if (dfc->inum != inum)
			continue;
		if (dfc->igen == igen) /* handled by an invalid file_copy */
			continue;
		if (up_only) {
			if (!host_is_up(dfc->host))
				continue;
			flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY;
		} else {
			if (!host_is_valid(dfc->host))
				continue;
			if (host_is_up(dfc->host))
				flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY;
			else
				flag = GFM_PROTO_REPLICA_FLAG_DEAD_COPY |
				       GFM_PROTO_REPLICA_FLAG_DEAD_HOST;
		}

		name = strdup_log(host_name(dfc->host), diag);
		if (name == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		hosts[i] = name;
		gens[i] = dfc->igen;
		flags[i] = flag;
		i++;
	}

	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");

	if (e != GFARM_ERR_NO_ERROR) {
		while (--i >= 0)
			free(hosts[i]);
	} else {
		*np = i;
	}
	return (e);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex
 * SLEEPS: no
 *
 * XXX
 * this assumes that the number of dead file copies is small enough,
 * otherwise this is too slow.
 */
int
dead_file_copy_existing(gfarm_ino_t inum, gfarm_uint64_t igen,
	struct host *host)
{
	static const char diag[] = "dead_file_copy_existing";

	return (dead_file_copy_lookup(inum, igen, host, diag) != NULL);
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
	return (dfc->host);
}

/*
 * PREREQUISITE: nothing
 * LOCKS: dfc_allq.mutex
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
	dfc->inum = inum;
	dfc->igen = igen;
	dfc->host = host;

	dfc->state = dfcstate_deferred;
	gfarm_mutex_init(&dfc->mutex, diag, "dfc state");

	/* to be sure */
	dfc->workq_next = dfc->workq_prev = NULL;

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "lock");
	dfc->allq_next = &dfc_allq.q;
	dfc->allq_prev = dfc_allq.q.allq_prev;
	dfc_allq.q.allq_prev->allq_next = dfc;
	dfc_allq.q.allq_prev = dfc;
	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "unlock");

	return (dfc);
}

/*
 * PREREQUISITE: nothing (XXX host_name()?)
 * LOCKS: dfc_allq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex)
 */
struct dead_file_copy *
dead_file_copy_new(gfarm_ino_t inum, gfarm_uint64_t igen, struct host *host)
{
	gfarm_error_t e;
	struct dead_file_copy *dfc;

	if ((dfc = dead_file_copy_alloc(inum, igen, host)) == NULL)
		return (NULL);

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

	return (dfc);
}

/*
 * PREREQUISITE: giant_lock
 * LOCKS: dfc_allq.mutex, dbq.mutex
 * SLEEPS: yes (dbq.mutex)
 *	but dfc_allq.mutex won't be blocked while sleeping.
 *
 * giant_lock is necessary, because dead_file_copy_scan_deferred() or
 * dead_file_copy_host_removed() may be accessing this dfc.
 */
static void
dead_file_copy_free(struct dead_file_copy *dfc)
{
	gfarm_error_t e;
	enum dead_file_copy_state state;
	static const char diag[] = "dead_file_copy_free";

	gfarm_mutex_lock(&dfc_allq.mutex, diag, "allq lock");

	gfarm_mutex_lock(&dfc->mutex, diag, "dfc state");
	state = dfc->state;
	gfarm_mutex_unlock(&dfc->mutex, diag, "dfc state");

	if (state != dfcstate_deferred &&
	    state != dfcstate_finalizing) {
		gfarm_mutex_unlock(&dfc_allq.mutex,
		    diag, "allq unlock at error");
		gflog_error(GFARM_MSG_1002231,
		    "dead_file_copy_free(%lld, %lld, %s): "
		    "state %d is not allowed, this shouldn't happen, ignored",
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), state);
		return;
	}

	dfc->allq_next->allq_prev = dfc->allq_prev;
	dfc->allq_prev->allq_next = dfc->allq_next;
	gfarm_mutex_unlock(&dfc_allq.mutex, diag, "allq unlock");

	e = db_deadfilecopy_remove(dfc->inum, dfc->igen, host_name(dfc->host));
	if (e != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1002232,
		    "db_deadfilecopy_remove(%lld, %lld, %s): %s",
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen,
		    host_name(dfc->host), gfarm_error_string(e));

	gfarm_mutex_destroy(&dfc->mutex, diag, "dfc state");

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR)
		gflog_trace(GFARM_MSG_1003279,
		    "%lld/////DELREPLICA/%s/%d/%s/%lld/%lld///////",
		    (unsigned long long)trace_log_get_sequence_number(),
		    gfarm_host_get_self_name(), gfmd_port,
		    host_name(dfc->host),
		    (unsigned long long)dfc->inum,
		    (unsigned long long)dfc->igen);

	inode_remove_replica_completed(dfc->inum, dfc->igen, dfc->host);

	free(dfc);
}

/* The memory owner of `hostname' is changed to dead_file_copy.c */
void
dead_file_copy_add_one(void *closure,
	gfarm_ino_t inum, gfarm_uint64_t igen, char *hostname)
{
	struct host *host = host_lookup(hostname);
	struct dead_file_copy *dfc;
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
#if 0 /* this is unnecessary, since all hosts are down in this point */
	} else if (dead_file_copy_is_removable(dfc)) {
		removal_pendingq_enqueue(dfc);
#endif
	} /* otherwise leave it as dfcstate_deferred */

	free(hostname);

	inode_dead_file_copy_added(inum, igen, host);
}

void
dead_file_copy_init_load(void)
{
	gfarm_error_t e;

	e = db_deadfilecopy_load(NULL, dead_file_copy_add_one);
	if (e != GFARM_ERR_NO_ERROR && e != GFARM_ERR_NO_SUCH_OBJECT)
		gflog_error(GFARM_MSG_1000362,
		    "loading deadfilecopy: %s", gfarm_error_string(e));

	host_busyq_scanner_init();
}

void
dead_file_copy_init(int is_master)
{
	gfarm_error_t e;

	if (is_master)
		dead_file_copy_init_load();

	e = create_detached_thread(removal_finalizer, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1002235,
		    "create_detached_thread(removal_finalizer): %s",
		    gfarm_error_string(e));

	e = create_detached_thread(dead_file_copy_scanner, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1005038,
		    "create_detached_thread(dead_file_copy_scanner): %s",
		    gfarm_error_string(e));
}

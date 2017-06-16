#include <pthread.h>
#include <assert.h>
#include <stdarg.h> /* XXX gfp_xdr.h needs this */
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "queue.h"
#include "gfutil.h"
#include "thrsubr.h"

#include "gfp_xdr.h" /* XXX abstract_host.h result_callback_t needs this */

#include "subr.h"
#include "thrpool.h"
#include "abstract_host.h"
#include "netsendq.h"
#include "netsendq_impl.h"

struct netsendq_workq {
	pthread_mutex_t mutex;
	pthread_cond_t dequeueable;

	GFARM_HCIRCLEQ_HEAD(netsendq_entry) q;
					    
	struct netsendq_entry *next; /* next data to move to readyq */

	int inflight_number;
};

struct netsendq_manager;

/* per abstract_host structure */
struct netsendq {
	GFARM_HCIRCLEQ_ENTRY(netsendq) hostq_entries;

	/* an array. array length == NETSENDQ_TYPE_GF?_PROTO_NUM_TYPES */
	struct netsendq_workq *workqs;

	pthread_mutex_t readyq_mutex;
	GFARM_STAILQ_HEAD(readyq_head, netsendq_entry) readyq;


	pthread_mutex_t sending_mutex;
	int sending;

	struct abstract_host *abhost; /* this pointer is immutable */
	struct netsendq_manager *manager; /* this pointer is immutable */
};

/* mark that this `netsendq' is not linked to hostq */
#define MARK_NETSENDQ_NOT_LINKED_TO_HOSTQ(qhost) \
	GFARM_HCIRCLEQ_INIT(*qhost, hostq_entries)
#define IS_NETSENDQ_LINKED_TO_HOSTQ(qhost) \
	(!GFARM_HCIRCLEQ_EMPTY(*qhost, hostq_entries))

struct netsendq_manager {
	pthread_mutex_t hostq_mutex;
	pthread_cond_t sendable;
	GFARM_HCIRCLEQ_HEAD(netsendq) hostq;

	pthread_mutex_t finalizeq_mutex;
	pthread_cond_t finalizeq_is_not_empty;
	GFARM_HCIRCLEQ_HEAD(netsendq_entry) finalizeq;

	int num_types;
	const struct netsendq_type *const *types;

	struct thread_pool *send_thrpool;
};

/* constructor of struct netsendq_entry */
void
netsendq_entry_init(struct netsendq_entry *entry, struct netsendq_type *type)
{
	entry->sendq_type = type;
	/* entry->workq_entries is not initialized */
	/* entry->readyq_entries is not initialized */
	/* entry->abhost is not initialized */

	gfarm_mutex_init(&entry->entry_mutex, "netsendq_entry_init", "init");
	entry->sending = 0;
	entry->finalize_pending = 0;

	/* entry->result is not initialized */
}

/* denstructor of struct netsendq_entry */
void
netsendq_entry_destroy(struct netsendq_entry *entry)
{
	gfarm_mutex_destroy(&entry->entry_mutex,
	    "netsendq_entry_destroy", "destroy");
}

static void
netsendq_entry_start_send(struct netsendq_entry *entry)
{
	static const char diag[] = "netsendq_entry_start_send";

	gfarm_mutex_lock(&entry->entry_mutex, diag, "entry_mutex");
	assert(!entry->sending);
	entry->sending = 1;
	gfarm_mutex_unlock(&entry->entry_mutex, diag, "entry_mutex");
}

static int
netsendq_entry_try_finalize(struct netsendq_entry *entry, gfarm_error_t result)
{
	int sending;
	static const char diag[] = "netsendq_entry_is_sending";

	gfarm_mutex_lock(&entry->entry_mutex, diag, "entry_mutex");
	sending = entry->sending;
	if (sending) {
		entry->finalize_pending = 1;
		entry->result = result;
		/* netsendq_entry_was_sent() will move this to finalizeq */
	}
	gfarm_mutex_unlock(&entry->entry_mutex, diag, "entry_mutex");

	return (!sending);
}

static void
netsendq_readyq_add(struct netsendq *qhost, struct netsendq_entry *entry,
	const char *diag)
{
	struct netsendq_manager *manager;

	gfarm_mutex_lock(&qhost->readyq_mutex, diag, "readyq");
	if ((entry->sendq_type->flags & NETSENDQ_FLAG_PRIOR_ONE_SHOT) != 0)
		GFARM_STAILQ_INSERT_HEAD(&qhost->readyq, entry, readyq_entries);
	else
		GFARM_STAILQ_INSERT_TAIL(&qhost->readyq, entry, readyq_entries);
	gfarm_mutex_unlock(&qhost->readyq_mutex, diag, "readyq");

	/*
	 * we cannot use the GFARM_STAILQ_EMPTY(&qhost->readyq) condition
	 * just before above insertion for the decision to add this qhost
	 * to the manager->hostq below.
	 * because there is race condition between adding an entry to readyq
	 * and the time when the host becomes down.
	 * i.e. qhost which is not linked to manager->hostq may have
	 * some readyq entries.
	 */

	manager = qhost->manager;
	gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
	if (!IS_NETSENDQ_LINKED_TO_HOSTQ(qhost)) {
		GFARM_HCIRCLEQ_INSERT_TAIL(manager->hostq,
		    qhost, hostq_entries);
		gfarm_cond_signal(&manager->sendable, diag, "sendable");
	}
	gfarm_mutex_unlock(&manager->hostq_mutex, diag, "hostq_mutex");
}

/*
 * PREREQUISITE: netsendq_workq::mutex
 * LOCKS: netsendq::readq_mutex, netsendq_manager:hostq_mutex
 */
static void
netsendq_workq_to_readyq(struct netsendq *qhost,
	struct netsendq_workq *workq, const char *diag)
{
	struct netsendq_entry *entry;

	while ((entry = workq->next) != NULL &&
	    workq->inflight_number < entry->sendq_type->window_size) {
		workq->next = GFARM_HCIRCLEQ_NEXT(entry, workq_entries);
		if (GFARM_HCIRCLEQ_IS_END(workq->q, workq->next))
			workq->next = NULL;
		netsendq_readyq_add(qhost, entry, diag);
		workq->inflight_number++;
	}
}

static void
netsendq_finalizeq_add(struct netsendq_manager *manager,
	struct netsendq_entry *entry, gfarm_error_t result, const char *diag)
{
	gfarm_mutex_lock(&manager->finalizeq_mutex, diag, "finalizeq");
	entry->result = result;
	GFARM_HCIRCLEQ_INSERT_TAIL(manager->finalizeq, entry, workq_entries);
	gfarm_cond_signal(&manager->finalizeq_is_not_empty, diag, "finalizeq");
	gfarm_mutex_unlock(&manager->finalizeq_mutex, diag, "finalizeq");
}

int
netsendq_window_is_full(struct netsendq *qhost, struct netsendq_type *type)
{
	int is_full;
	struct netsendq_workq *workq;
	static const char diag[] = "netsendq_window_is_full";

	workq = &qhost->workqs[type->type_index];
	gfarm_mutex_lock(&workq->mutex, diag, "workq");
	is_full = workq->inflight_number >= type->window_size;
	gfarm_mutex_unlock(&workq->mutex, diag, "workq");
	return (is_full);
}

gfarm_error_t
netsendq_add_entry(struct netsendq *qhost, struct netsendq_entry *entry,
	int flags)
{
	struct netsendq_workq *workq;
	int abhost_is_up = abstract_host_is_up(entry->abhost);
	static const char diag[] = "netsendq_add_entry";

	if ((entry->sendq_type->flags & NETSENDQ_FLAG_QUEUEABLE_IF_DOWN) == 0
	    && !abhost_is_up) {
		if ((flags & NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING) != 0) {
			netsendq_finalizeq_add(qhost->manager, entry,
			    GFARM_ERR_NO_ROUTE_TO_HOST, diag);
		}
		return (GFARM_ERR_NO_ROUTE_TO_HOST);
	}

	workq = &qhost->workqs[entry->sendq_type->type_index];
	gfarm_mutex_lock(&workq->mutex, diag, "workq");
	if ((entry->sendq_type->flags & NETSENDQ_FLAG_PRIOR_ONE_SHOT) != 0 &&
	    workq->inflight_number >= entry->sendq_type->window_size) {
		gfarm_mutex_unlock(&workq->mutex, diag, "workq");
		if ((flags & NETSENDQ_ADD_FLAG_DETACH_ERROR_HANDLING) != 0) {
			netsendq_finalizeq_add(qhost->manager, entry,
			    GFARM_ERR_DEVICE_BUSY, diag);
		}
		return (GFARM_ERR_DEVICE_BUSY);
	}
	GFARM_HCIRCLEQ_INSERT_TAIL(workq->q, entry, workq_entries);
	if (workq->next == NULL)
		workq->next = entry;
	if (workq->inflight_number < entry->sendq_type->window_size &&
	    abhost_is_up)
		netsendq_workq_to_readyq(qhost, workq, diag);
	gfarm_mutex_unlock(&workq->mutex, diag, "workq");
	return (GFARM_ERR_NO_ERROR);
}

/*
 * This routine only can be called
 * after  (*entry->sendq_type->send)() is called, and
 * before (*entry->sendq_type->finalize)() is called.
 * Thus, this routine doesn't have to maintain workq->next.
 */
void
netsendq_remove_entry(struct netsendq *qhost,
	struct netsendq_entry *entry, gfarm_error_t result)
{
	struct netsendq_workq *workq;
	static const char diag[] = "netsendq_remove_entry";

	workq = &qhost->workqs[entry->sendq_type->type_index];
	gfarm_mutex_lock(&workq->mutex, diag, "workq");
	GFARM_HCIRCLEQ_REMOVE(entry, workq_entries);
	--workq->inflight_number;

	/* if (*entry->sendq_type->send)() isn't called, abort */
	assert(entry != workq->next);

	if (workq->inflight_number < entry->sendq_type->window_size &&
	    abstract_host_is_up(qhost->abhost))
		netsendq_workq_to_readyq(qhost, workq, diag);
	gfarm_mutex_unlock(&workq->mutex, diag, "workq");

	if (netsendq_entry_try_finalize(entry, result))
		netsendq_finalizeq_add(qhost->manager, entry, result, diag);
}

static void
netsendq_host_is_down_at_entry(struct netsendq *qhost,
	struct netsendq_entry *entry)
{
	struct netsendq_workq *workq;
	static const char diag[] = "netsendq_host_is_down_at_entry";

	workq = &qhost->workqs[entry->sendq_type->type_index];
	gfarm_mutex_lock(&workq->mutex, diag, "workq");
	GFARM_HCIRCLEQ_REMOVE(entry, workq_entries);
	--workq->inflight_number;
	assert(entry != workq->next);
	gfarm_mutex_unlock(&workq->mutex, diag, "workq");

	netsendq_finalizeq_add(qhost->manager, entry,
	    GFARM_ERR_NO_ROUTE_TO_HOST, diag);
}

static int
netsendq_readyq_remove(struct netsendq *qhost, struct netsendq_entry **entryp)
{
	int became_empty;
	static const char diag[] = "netsendq_readyq_remove";

	gfarm_mutex_lock(&qhost->readyq_mutex, diag, "readyq");
	if (GFARM_STAILQ_EMPTY(&qhost->readyq)) {
		*entryp = NULL;
		became_empty = 1;
	} else {
		*entryp = GFARM_STAILQ_FIRST(&qhost->readyq);
		GFARM_STAILQ_REMOVE_HEAD(&qhost->readyq, readyq_entries);
		became_empty = GFARM_STAILQ_EMPTY(&qhost->readyq);
	}
	gfarm_mutex_unlock(&qhost->readyq_mutex, diag, "readyq");
	return (became_empty);
}

static void
netsendq_workq_init(struct netsendq_workq *workq, const char *diag)
{
	gfarm_mutex_init(&workq->mutex, diag, "workq");
	gfarm_cond_init(&workq->dequeueable, diag, "dequeueable");
	GFARM_HCIRCLEQ_INIT(workq->q, workq_entries);
	workq->next = NULL;
	workq->inflight_number = 0;
}

gfarm_error_t
netsendq_new(struct netsendq_manager *manager, struct abstract_host *abhost,
	struct netsendq **qhostp)
{
	struct netsendq *qhost;
	int i;
	static const char diag[] = "netsendq_new";

	GFARM_MALLOC(qhost);
	if (qhost == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no memory for %d bytes",
		    diag, (int)sizeof(*qhost));
		return (GFARM_ERR_NO_MEMORY);
	}
	GFARM_MALLOC_ARRAY(qhost->workqs, manager->num_types);
	if (qhost->workqs == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no memory for %d*%d bytes",
		    diag, manager->num_types, (int)sizeof(*qhost->workqs));
		return (GFARM_ERR_NO_MEMORY);
	}

	MARK_NETSENDQ_NOT_LINKED_TO_HOSTQ(qhost);

	for (i = 0; i < manager->num_types; i++)
		netsendq_workq_init(&qhost->workqs[i], diag);

	gfarm_mutex_init(&qhost->readyq_mutex, diag, "readyq");
	GFARM_STAILQ_INIT(&qhost->readyq);

	gfarm_mutex_init(&qhost->sending_mutex, diag, "sending");
	qhost->sending = 0;

	qhost->abhost = abhost;
	qhost->manager = manager;

	*qhostp = qhost;
	return (GFARM_ERR_NO_ERROR);
}

struct abstract_host *
netsendq_get_abstract_host(struct netsendq *qhost)
{
	return (qhost->abhost);
}

static int
netsendq_try_to_send_to_host(struct netsendq *qhost)
{
	int sendable;
	static const char diag[] = "netsendq_is_sending_to_host";

	gfarm_mutex_lock(&qhost->sending_mutex, diag, "sending_mutex");
	if (qhost->sending) {
		sendable = 0;
	} else {
		sendable = 1;
		qhost->sending = 1;
	}
	gfarm_mutex_unlock(&qhost->sending_mutex, diag, "sending_mutex");
	return (sendable);
}

static void
netsendq_was_sent_to_host(struct netsendq *qhost)
{
	struct netsendq_manager *manager = qhost->manager;
	static const char diag[] = "netsendq_was_sent_to_host";

	gfarm_mutex_lock(&qhost->sending_mutex, diag, "sending_mutex");
	qhost->sending = 0;
	gfarm_mutex_unlock(&qhost->sending_mutex, diag, "sending_mutex");

	gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
	if (IS_NETSENDQ_LINKED_TO_HOSTQ(qhost))
		gfarm_cond_signal(&manager->sendable, diag, "sendable");
	gfarm_mutex_unlock(&manager->hostq_mutex, diag, "qhost_mutex");
}

static int
netsendq_is_sending(struct netsendq *qhost)
{
	int sending;
	static const char diag[] = "netsendq_is_sending";

	gfarm_mutex_lock(&qhost->sending_mutex, diag, "sending_mutex");
	sending = qhost->sending;
	gfarm_mutex_unlock(&qhost->sending_mutex, diag, "sending_mutex");
	return (sending);
}

void
netsendq_entry_was_sent(struct netsendq *qhost, struct netsendq_entry *entry)
{
	int finalize_pending;
	static const char diag[] = "netsendq_entry_was_sent";

	gfarm_mutex_lock(&entry->entry_mutex, diag, "entry_mutex");
	assert(entry->sending);
	entry->sending = 0;
	finalize_pending = entry->finalize_pending;
	entry->finalize_pending = 0;
	gfarm_mutex_unlock(&entry->entry_mutex, diag, "entry_mutex");

	netsendq_was_sent_to_host(qhost);

	if (finalize_pending)
		netsendq_finalizeq_add(qhost->manager, entry, entry->result,
		    diag);
}

static void *
netsendq_send_manager(void *arg)
{
	struct netsendq_manager *manager = arg;
	struct netsendq *current, *send_to, *to_remove, *c;
	struct netsendq_entry *entry;
	int all_busy = 1;
	static const char diag[] = "netsendq_send_manager";

	gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
	current = GFARM_HCIRCLEQ_FIRST(manager->hostq, hostq_entries);
	all_busy = 1;
	for (;;) {
		if (GFARM_HCIRCLEQ_IS_END(manager->hostq, current)) {
			if (all_busy) {
				/*
				 * check it again, because some may become
				 * unbusy, while releasing hostq_mutex.
				 */
				for (c = GFARM_HCIRCLEQ_FIRST(manager->hostq,
				    hostq_entries);
				    !GFARM_HCIRCLEQ_IS_END(manager->hostq, c);
				    c = GFARM_HCIRCLEQ_NEXT(c, hostq_entries)) {
					if (!netsendq_is_sending(c))
						all_busy = 0;
				}
				if (all_busy) {
					gfarm_cond_wait(&manager->sendable,
					    &manager->hostq_mutex,
					    diag, "all_busy->sendable");
				}
			}
			while (GFARM_HCIRCLEQ_EMPTY(manager->hostq,
			    hostq_entries)) {
				gfarm_cond_wait(&manager->sendable,
				    &manager->hostq_mutex,
				    diag, "empty->sendable");
			}
			current = GFARM_HCIRCLEQ_FIRST(manager->hostq,
			    hostq_entries);
			all_busy = 1;
		}

		/*
		 * XXX
		 * eventually, we'd like to use abstract_host_sender_trylock()
		 * to see whether current->abhost is sendable or not,
		 * but we use netsendq_is_sending_to_host() here for now.
		 * (because the amount of time to rewrite things is much.)
		 */
		if (netsendq_try_to_send_to_host(current)) {
			send_to = current; /* NOTE: send_to->sending is set */
			all_busy = 0;
		} else {
			send_to = NULL;
		}
		current = GFARM_HCIRCLEQ_NEXT(current, hostq_entries);
		if (send_to == NULL)
			continue;
		gfarm_mutex_unlock(&manager->hostq_mutex, diag, "hostq_mutex");

		if (netsendq_readyq_remove(send_to, &entry)) {
			to_remove = send_to;
		} else {
			to_remove = NULL;
		}
		if (entry == NULL) {
			/* unset send_to->sending */
			netsendq_was_sent_to_host(send_to);
		} else if (abstract_host_is_up(send_to->abhost)) {
			netsendq_entry_start_send(entry);
			thrpool_add_job(manager->send_thrpool,
			    entry->sendq_type->send, entry);
		} else {
			netsendq_host_is_down_at_entry(send_to, entry);
			/* unset send_to->sending */
			netsendq_was_sent_to_host(send_to);
		}

		gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
		/*
		 * `to_remove' and `current' may be unlinked from hostq
		 * by netsendq_host_becomes_down(),
		 * while hostq_mutex is unlocked.
		 */
		if (!IS_NETSENDQ_LINKED_TO_HOSTQ(current)) {
			current = GFARM_HCIRCLEQ_FIRST(manager->hostq,
			    hostq_entries);
		}
		if (to_remove != NULL &&
		    IS_NETSENDQ_LINKED_TO_HOSTQ(to_remove)) {
			GFARM_HCIRCLEQ_REMOVE(to_remove, hostq_entries);
			if (to_remove == current)
				current = GFARM_HCIRCLEQ_NEXT(
				    current, hostq_entries);

			MARK_NETSENDQ_NOT_LINKED_TO_HOSTQ(to_remove);
		}
	}
	return (NULL);
}

/*
 * LOCKS: netsendq_workq::mutex ->
 *	netsendq::readq_mutex, netsendq_manager:hostq_mutex
 */
void
netsendq_host_remove_internal(struct netsendq *qhost, int hold_queue)
{
	struct netsendq_manager *manager = qhost->manager;
	struct netsendq_workq *workq;
	struct netsendq_entry *entry, *n;
	int i;
	static const char diag[] = "netsendq_host_becomes_down";

	/*
	 * locking order is:
	 * netsendq_workq::mutex
	 *	-> netsendq::readq_mutex, netsendq_manager:hostq_mutex
	 */

	/* lock all netsendq_workq::mutex */
	for (i = 0; i < manager->num_types; i++) {
		workq = &qhost->workqs[i];
		gfarm_mutex_lock(&workq->mutex, diag, "workq::mutex");
	}

	gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
	if (IS_NETSENDQ_LINKED_TO_HOSTQ(qhost)) {
		GFARM_HCIRCLEQ_REMOVE(qhost, hostq_entries);
		MARK_NETSENDQ_NOT_LINKED_TO_HOSTQ(qhost);
	}
	gfarm_mutex_unlock(&manager->hostq_mutex, diag, "hostq_mutex");

	gfarm_mutex_lock(&qhost->readyq_mutex, diag, "readyq_mutex");
	GFARM_STAILQ_FOREACH_SAFE(entry, &qhost->readyq, readyq_entries, n) {
		/* NOTE: finalize even if NETSENDQ_FLAG_QUEUEABLE_IF_DOWN */
		GFARM_HCIRCLEQ_REMOVE(entry, workq_entries);
		workq = &qhost->workqs[entry->sendq_type->type_index];
		--workq->inflight_number;

		netsendq_finalizeq_add(manager, entry,
		    GFARM_ERR_CONNECTION_ABORTED, diag);
	}
	GFARM_STAILQ_INIT(&qhost->readyq);
	gfarm_mutex_unlock(&qhost->readyq_mutex, diag, "readyq_mutex");

	for (i = 0; i < manager->num_types; i++) {
		if (hold_queue && (manager->types[i]->flags &
		    NETSENDQ_FLAG_QUEUEABLE_IF_DOWN) != 0) {
			/* if it hasn't added to qhost->readyq, hold it */
			continue;
		}
		workq = &qhost->workqs[i];
		if (workq->next != NULL) {
			/*
			 * entries in [FIRST, workq->next) are inflight,
			 * thus, we remove only [workq->next, LAST].
			 */
			for (entry = workq->next;
			    !GFARM_HCIRCLEQ_IS_END(workq->q, entry);
			    entry = n) {
				n = GFARM_HCIRCLEQ_NEXT(entry, workq_entries);
				GFARM_HCIRCLEQ_REMOVE(entry, workq_entries);

				netsendq_finalizeq_add(manager, entry,
				    GFARM_ERR_CONNECTION_ABORTED, diag);
			}
			workq->next = NULL;
		}
	}

	/* unlock all netsendq_workq::mutex */
	for (i = manager->num_types - 1; i >= 0; --i) {
		workq = &qhost->workqs[i];
		gfarm_mutex_unlock(&workq->mutex, diag, "workq::mutex");
	}
}

/*
 * LOCKS: netsendq_workq::mutex ->
 *	netsendq::readq_mutex, netsendq_manager:hostq_mutex
 */
void
netsendq_host_remove(struct netsendq *qhost)
{
	netsendq_host_remove_internal(qhost, 0);
}

/*
 * LOCKS: netsendq_workq::mutex ->
 *	netsendq::readq_mutex, netsendq_manager:hostq_mutex
 */
void
netsendq_host_becomes_down(struct netsendq *qhost)
{
	netsendq_host_remove_internal(qhost, 1);
}

/*
 * LOCKS: netsendq_workq::mutex ->
 *	netsendq::readq_mutex, netsendq_manager:hostq_mutex
 */
void
netsendq_host_becomes_up(struct netsendq *qhost)
{
	struct netsendq_manager *manager = qhost->manager;
	struct netsendq_workq *workq;
	int i;
	static const char diag[] = "netsendq_host_becomes_up";

	/*
	 * qhost which was down may be belongs to manager->hostq,
	 * due to the following race condition:
	 * 1. thread A checks that the qhost is up.
	 * 2. thread B makes the host down.
	 * 3. thread A adds a qentry for the qhost to readyq->readyq,
	 *    thus the qhost is added to manager->hostq.
	 */
	gfarm_mutex_lock(&manager->hostq_mutex, diag, "hostq_mutex");
	if (IS_NETSENDQ_LINKED_TO_HOSTQ(qhost))
		gfarm_cond_signal(&manager->sendable, diag, "sendable");
	gfarm_mutex_unlock(&manager->hostq_mutex, diag, "hostq_mutex");

	for (i = 0; i < manager->num_types; i++) {
		workq = &qhost->workqs[i];
		gfarm_mutex_lock(&workq->mutex, diag, "workq::mutex");
		netsendq_workq_to_readyq(qhost, workq, diag);
		gfarm_mutex_unlock(&workq->mutex, diag, "workq::mutex");
	}
}

static void *
netsendq_finalizer(void *arg)
{
	struct netsendq_manager *manager = arg;
	struct netsendq_entry *entry;
	static const char diag[] = "netsendq_finalizer";

	for (;;) {
		gfarm_mutex_lock(&manager->finalizeq_mutex, diag, "finalizeq");

		while (GFARM_HCIRCLEQ_EMPTY(manager->finalizeq,
		    workq_entries)) {
			gfarm_cond_wait(&manager->finalizeq_is_not_empty,
			    &manager->finalizeq_mutex, diag, "not empty");
		}
		entry = GFARM_HCIRCLEQ_FIRST(manager->finalizeq,
		    workq_entries);
		GFARM_HCIRCLEQ_REMOVE_HEAD(manager->finalizeq, workq_entries);

		gfarm_mutex_unlock(&manager->finalizeq_mutex, diag,
		    "finalizeq");

		entry->sendq_type->finalize(entry);
	}
	return (NULL);
}

struct netsendq_manager *
netsendq_manager_new(int num_types, const struct netsendq_type *const *types,
	int thread_pool_size, int thread_pool_queue_len, const char *diag)
{
	gfarm_error_t e;
	struct netsendq_manager *manager;
	struct thread_pool *thrpool;
	int i;
	
	GFARM_MALLOC(manager);
	if (manager == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: no memory for struct netsendq_manager", diag);

	gfarm_mutex_init(&manager->hostq_mutex, diag, "hostq_mutex");
	gfarm_cond_init(&manager->sendable, diag, "sendable");
	GFARM_HCIRCLEQ_INIT(manager->hostq, hostq_entries);

	gfarm_mutex_init(&manager->finalizeq_mutex, diag, "finalizeq");
	gfarm_cond_init(&manager->finalizeq_is_not_empty, diag, "finalizeq");
	GFARM_HCIRCLEQ_INIT(manager->finalizeq, workq_entries);

	for (i = 0; i < num_types; i++) {
		if (types[i]->type_index != i)
			gflog_fatal(GFARM_MSG_UNFIXED,
			    "%s: netsendq_type is not correctly initialized, "
			    "types[%d]->type_index = %d, but must be %d",
			    diag, i, types[i]->type_index, i);
	}
	manager->num_types = num_types;
	manager->types = types;

	thrpool = thrpool_new(thread_pool_size, thread_pool_queue_len, diag);
	if (thrpool == NULL)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: filesystem node thread pool size:%d, "
		    "queue length:%d: no memory",
		    diag, thread_pool_size, thread_pool_queue_len);
	manager->send_thrpool = thrpool;

	e = create_detached_thread(netsendq_send_manager, manager);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: create_detached_thread(netsendq_send_manager): %s",
		    diag, gfarm_error_string(e));

	e = create_detached_thread(netsendq_finalizer, manager);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: create_detached_thread(netsendq_finalizer): %s",
		    diag, gfarm_error_string(e));

	return (manager);
}

struct thread_pool *
netsendq_manager_get_thrpool(struct netsendq_manager *manager)
{
	return (manager->send_thrpool);
}


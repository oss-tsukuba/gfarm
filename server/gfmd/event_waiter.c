#include <gfarm/gfarm_config.h>

#include <pthread.h>
#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "subr.h"
#include "queue.h"
#include "thrsubr.h"

#include "event_waiter.h"
#include "callout.h"
#include "gfmd.h" /* sync_protocol_get_thrpool() */

struct event_waiter {
	struct event_waiter_link super; /* should be first member of struct */

	struct peer *peer;
	struct callout *callout;
	gfarm_error_t (*action)(struct peer *, void *, int *, gfarm_error_t);
	void *arg;
	gfarm_error_t result;
};

void
event_waiter_list_init(struct event_waiter_list *list)
{
	GFARM_HCIRCLEQ_INIT(list->head, event_link);
}

gfarm_error_t
event_waiter_alloc(struct peer *peer,
	gfarm_error_t (*action)(struct peer *, void *, int *, gfarm_error_t),
	void *arg, struct event_waiter_list *list)
{
	struct event_waiter *waiter;

	GFARM_MALLOC(waiter);
	if (waiter == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/* XXX FIXME should call peer_add_ref(peer) */

	waiter->peer = peer;
	waiter->callout = NULL;
	waiter->action = action;
	waiter->arg = arg;
	waiter->result = GFARM_ERR_NO_ERROR;

	GFARM_HCIRCLEQ_INSERT_TAIL(list->head, &waiter->super, event_link);

	return (GFARM_ERR_NO_ERROR);
}

struct peer *
event_waiter_get_peer(struct event_waiter *waiter)
{
	return (waiter->peer);
}

gfarm_error_t
event_waiter_call_action(struct event_waiter *waiter, int *suspendedp)
{
	return ((*waiter->action)(waiter->peer, waiter->arg,
	    suspendedp, waiter->result));
}

void
event_waiter_free(struct event_waiter *waiter)
{
	/* XXX FIXME should call peer_del_ref(waiter->peer) */

	if (waiter->callout != NULL) {
		callout_halt(waiter->callout, NULL);
		callout_free(waiter->callout);
	}

	free(waiter);
}

struct resuming_queue {
	pthread_mutex_t mutex;
	pthread_cond_t nonempty;
	struct event_waiter_list queue;
} resuming_pendings = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	{
		GFARM_HCIRCLEQ_HEAD_ENTRY_INITIALIZER(
		    resuming_pendings.queue.head, event_link)
	}
};

void
resuming_enqueue(struct event_waiter *entry)
{
	struct resuming_queue *q = &resuming_pendings;
	static const char diag[] = "resuming_enqueue";

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	GFARM_HCIRCLEQ_INSERT_TAIL(q->queue.head, &entry->super, event_link);
	gfarm_cond_signal(&q->nonempty, diag, "nonempty");
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
}

struct event_waiter *
resuming_dequeue(const char *diag)
{
	struct resuming_queue *q = &resuming_pendings;
	struct event_waiter_link *link;
	struct event_waiter *entry;

	gfarm_mutex_lock(&q->mutex, diag, "mutex");
	while (GFARM_HCIRCLEQ_EMPTY(q->queue.head, event_link))
		gfarm_cond_wait(&q->nonempty, &q->mutex, diag, "nonempty");
	link = GFARM_HCIRCLEQ_FIRST(q->queue.head, event_link);
	entry = (struct event_waiter *)link; /* downcast */
	GFARM_HCIRCLEQ_REMOVE_HEAD(q->queue.head, event_link);
	gfarm_mutex_unlock(&q->mutex, diag, "mutex");
	return (entry);
}

static void
event_waiter_signal(struct event_waiter *waiter, gfarm_error_t result)
{
	waiter->result = result;
	resuming_enqueue(waiter);
}

/*
 * PREREQUISITE: giant_lock
 */
void
event_waiters_signal(struct event_waiter_list *list, gfarm_error_t result)
{
	struct event_waiter_link *t, *next;
	struct event_waiter *entry;

	GFARM_HCIRCLEQ_FOREACH_SAFE(t, list->head, event_link, next) {
		entry = (struct event_waiter *)t; /* downcast */
		event_waiter_signal(entry, result);
	}
	event_waiter_list_init(list);
}

static void *
event_waiter_timeout(void *closure)
{
	struct event_waiter *waiter = closure;
	struct event_waiter_link *link = closure;

	giant_lock();

	GFARM_HCIRCLEQ_REMOVE(link, event_link);

	/*
	 * we cannot use GFARM_ERR_OPERATION_TIMED_OUT for this error,
	 * because it matches with IS_CONNECTION_ERROR() and
	 * causes gfmd failover on the client.
	 */
	event_waiter_signal(waiter, GFARM_ERR_EXPIRED);

	giant_unlock();

	return (NULL);
}

gfarm_error_t
event_waiter_with_timeout_alloc(struct peer *peer, int timeout,
	gfarm_error_t (*action)(struct peer *, void *, int *, gfarm_error_t),
	void *arg, struct event_waiter_list *list)
{
	gfarm_error_t e = event_waiter_alloc(peer, action, arg, list);
	struct event_waiter_link *recently_allocated;
	struct event_waiter *waiter;

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	/*
	 * XXX layering violation, this assumes event_waiter_alloc() linked
	 * this waiter by GFARM_HCIRCLEQ_INSERT_TAIL()
	 */
	recently_allocated = GFARM_HCIRCLEQ_LAST(list->head, event_link);

	waiter = (struct event_waiter *)recently_allocated; /* downcast */
	waiter->callout = callout_new();
	if (waiter->callout == NULL) {
		GFARM_HCIRCLEQ_REMOVE(&waiter->super, event_link);
		event_waiter_free(waiter);
		return (GFARM_ERR_NO_MEMORY);
	}
	callout_reset(waiter->callout, timeout, sync_protocol_get_thrpool(),
	    event_waiter_timeout, waiter);

	return (GFARM_ERR_NO_ERROR);
}

#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>

#include "gfutil.h"
#include "gfevent.h"
#include "thrsubr.h"

#include "subr.h"
#include "thrpool.h"
#include "watcher.h"

/*
 * watcher_event
 */

enum watcher_event_type {
	WATCHER_READABLE_EVENT,
	WATCHER_WRITABLE_EVENT,
	WATCHER_CLOSING_EVENT
};

struct watcher_event {
	/* for watcher_request_queue::list */
	struct watcher_event *prev, *next;

	struct watcher_event *next_closing; /* watcher_event::closing_events */

	struct gfarm_event *gev;

	enum watcher_event_type type;
	struct thread_pool *thrpool; /* not used for WATCHER_CLOSING_EVENT */
	void *(*handler)(void *);
	void *closure;
#define WATCHER_EVENT_IN_QUEUE	1 /* in watcher_request_queue */
#define WATCHER_EVENT_WATCHING	2 /* in gfarm_eventqueue */
#define WATCHER_EVENT_INVOKING	4
	int flags;

	/* WATCHER_CLOSING_EVENT only */
	struct watcher_event *closing_events, **closing_tail;

	pthread_mutex_t mutex;
};

static const char module_name[] = "watcher_module";

static void
watcher_event_callback(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct watcher_event *wev = closure;
	struct thread_pool *p;
	void *(*h)(void *);
	void *c;

	gfarm_mutex_lock(&wev->mutex, module_name, "event callback");
	wev->flags &= ~WATCHER_EVENT_WATCHING;
	wev->flags |= WATCHER_EVENT_INVOKING;
	p = wev->thrpool; wev->thrpool = NULL;
	h = wev->handler; wev->handler = NULL;
	c = wev->closure; wev->closure = NULL;
	gfarm_mutex_unlock(&wev->mutex, module_name, "event callback");
	thrpool_add_job(p, h, c);
}

static gfarm_error_t
watcher_fd_event_alloc(int fd, enum watcher_event_type type,
	struct watcher_event **wevp)
{
	struct watcher_event *wev;
	
	GFARM_MALLOC(wev);
	if (wev == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (type == WATCHER_CLOSING_EVENT) {
		wev->gev = NULL;
	} else if ((wev->gev = gfarm_fd_event_alloc(
	    type == WATCHER_READABLE_EVENT ? GFARM_EVENT_READ :
	    type == WATCHER_WRITABLE_EVENT ? GFARM_EVENT_WRITE :
	    (assert(0), 0),
	    fd, watcher_event_callback, wev)) == NULL) {
		free(wev);
		return (GFARM_ERR_NO_MEMORY);
	}

	wev->prev = wev->next = NULL;
	wev->next_closing = NULL;
	wev->type = type;
	wev->flags = 0;

	wev->thrpool = NULL;
	wev->handler = NULL;
	wev->closure = NULL;

	wev->closing_events = NULL;
	wev->closing_tail = &wev->closing_events;

	gfarm_mutex_init(&wev->mutex, module_name, "watcher_event");
	*wevp = wev;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
watcher_fd_readable_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(fd, WATCHER_READABLE_EVENT, wevp));
}

gfarm_error_t
watcher_fd_writable_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(fd, WATCHER_WRITABLE_EVENT, wevp));
}

gfarm_error_t
watcher_fd_closing_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(fd, WATCHER_CLOSING_EVENT, wevp));
}

void
watcher_fd_closing_event_add_relevant_event(
	struct watcher_event *closing_event, struct watcher_event *wev)
{
	assert(closing_event->type == WATCHER_CLOSING_EVENT);

	wev->next_closing = NULL;
	*closing_event->closing_tail = wev;
	closing_event->closing_tail = &wev->next_closing;
}

int
watcher_event_is_active(struct watcher_event *wev)
{
	int active;

	gfarm_mutex_lock(&wev->mutex, module_name, "is_active lock");
	active = wev->flags != 0;
	gfarm_mutex_unlock(&wev->mutex, module_name, "is_active unlock");
	return (active);
}

void
watcher_event_ack(struct watcher_event *wev)
{
	gfarm_mutex_lock(&wev->mutex, module_name, "ack lock");
	wev->flags &= ~WATCHER_EVENT_INVOKING;
	gfarm_mutex_unlock(&wev->mutex, module_name, "ack unlock");
}


/*
 * watcher_request_queue
 */

struct watcher_request_queue {
	pthread_mutex_t mutex;

	struct watcher_event head;

	int enq_fd, deq_fd;
};

static gfarm_error_t
watcher_request_queue_init(struct watcher_request_queue *wrq)
{
	static const char diag[] = "watcher_request_queue_init";
	int fds[2];

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds) == -1)
		return (gfarm_errno_to_error(errno));

	gfarm_mutex_init(&wrq->mutex, diag, "mutex");
	wrq->head.prev = wrq->head.next = &wrq->head;
	wrq->enq_fd = fds[1];
	wrq->deq_fd = fds[0];
	return (GFARM_ERR_NO_ERROR);
}

static void
watcher_request_queue_destroy(struct watcher_request_queue *wrq)
{
	static const char diag[] = "watcher_request_queue_destroy";

	close(wrq->enq_fd);
	close(wrq->deq_fd);
	gfarm_mutex_destroy(&wrq->mutex, diag, "mutex");
}

static int
watcher_request_queue_get_fd(struct watcher_request_queue *wrq)
{
	return (wrq->deq_fd);
}

static void
watcher_request_enqueue(struct watcher_request_queue *wrq,
	struct watcher_event *wev,
	struct thread_pool *thrpool, void *(*handler)(void *), void *closure)
{
	int rv, was_empty;
	char event = 0;
	static const char diag[] = "watcher_request_enqueue";

	gfarm_mutex_lock(&wrq->mutex, diag, "mutex");

	gfarm_mutex_lock(&wev->mutex, module_name, "enqueue lock");

	assert(wev->flags == 0);
	wev->flags |= WATCHER_EVENT_IN_QUEUE;
	wev->next = &wrq->head;
	wev->prev = wrq->head.prev;

	assert(wev->type == WATCHER_CLOSING_EVENT ?
	    thrpool == NULL : thrpool != NULL);
	wev->thrpool = thrpool;
	wev->handler = handler;
	wev->closure = closure;

	gfarm_mutex_unlock(&wev->mutex, module_name, "enqueue unlock");

	was_empty = wrq->head.next == &wrq->head;

	wrq->head.prev->next = wev;
	wrq->head.prev = wev;

	if (was_empty) {
		rv = write(wrq->enq_fd, &event, sizeof event);
		if (rv != sizeof event)
			gflog_fatal(GFARM_MSG_1002738,
			    "watcher_request_queue_enqueue: "
			    "write: %d (%d)", rv, errno);
	}

	gfarm_mutex_unlock(&wrq->mutex, diag, "mutex");
}

static struct watcher_event *
watcher_request_queue_dequeue(struct watcher_request_queue *wrq)
{
	int rv;
	char event;
	struct watcher_event *list;
	static const char diag[] = "watcher_request_queue_enqueue";

	gfarm_mutex_lock(&wrq->mutex, diag, "mutex");
	if (wrq->head.next == &wrq->head)
		gflog_fatal(GFARM_MSG_1002739,
		    "watcher_request_queue_dequeue: queue is empty");
	rv = read(wrq->deq_fd, &event, sizeof event);
	if (rv != sizeof event)
		gflog_fatal(GFARM_MSG_1002740,
		    "watcher_request_queue_dequeue: "
		    "read: %d (%d)", rv, errno);

	/* list->mutex is not necessary, since it's currently idle */
	list = wrq->head.next;
	list->prev = wrq->head.prev;
	wrq->head.prev->next = list;

	wrq->head.prev = wrq->head.next = &wrq->head;

	gfarm_mutex_unlock(&wrq->mutex, diag, "mutex");

	return (list);
}


/*
 * watcher
 */

struct watcher {
	struct watcher_request_queue wrq;

	struct gfarm_eventqueue *q;

	struct gfarm_event *control_gev;
};

static void
watcher_control_callback(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct watcher *w = closure;
	struct watcher_event *list0, *list, *wev;
	int err;

	assert(events == GFARM_EVENT_READ);

	list0 = watcher_request_queue_dequeue(&w->wrq);
	assert(list0 != NULL);

	/* handle closing events first */
	list = list0;
	do {
		if (list->type == WATCHER_CLOSING_EVENT) {
			for (wev = list->closing_events;
			    wev != NULL; wev = wev->next_closing) {

				gfarm_mutex_lock(&wev->mutex, module_name,
				    "event removal");

				if ((wev->flags & WATCHER_EVENT_IN_QUEUE) != 0){
					wev->flags &= ~WATCHER_EVENT_IN_QUEUE;
					if (list0 == wev)
						list0 = wev->next;
					wev->next->prev = wev->prev;
					wev->prev->next = wev->next;
				}
				if ((wev->flags & WATCHER_EVENT_WATCHING) != 0){
					wev->flags &= ~WATCHER_EVENT_WATCHING;

					err = gfarm_eventqueue_delete_event(
					    w->q, wev->gev);
					if (err != 0) {
						gflog_error(GFARM_MSG_1002741,
						    "delete_event(type:%d, "
						    "handler:%p): %s",
						    wev->type, wev->handler,
						    strerror(err));
					}
					wev->thrpool = NULL;
					wev->handler = NULL;
					wev->closure = NULL;
				}
				gfarm_mutex_unlock(&wev->mutex, module_name,
				    "event removal");

			}

			/* calling the handler without using a thread pool */
			list->flags &= ~WATCHER_EVENT_INVOKING;
			(*wev->handler)(wev->closure);
		}
		list = list->next;
	} while (list != list0);

	list = list0;
	do {
		if (list->type != WATCHER_CLOSING_EVENT) {
			err = gfarm_eventqueue_add_event(w->q, list->gev, NULL);
			if (err == 0) {
				list->flags |= WATCHER_EVENT_WATCHING;
			} else {
				gflog_error(GFARM_MSG_1002742,
				    "add_event(type:%d, handler:%p): %s",
				    list->type, list->handler, strerror(err));
			}
		}
		/*
		 * the following flags must be cleared after adding
		 * the WATCHER_EVENT_WATCHING flag above.
		 * otherwise the race condition of SF.net #616 appears.
		 */
		list->flags &= ~(WATCHER_EVENT_IN_QUEUE|WATCHER_EVENT_INVOKING);

		list = list->next;
	} while (list != list0);

	err = gfarm_eventqueue_add_event(w->q, w->control_gev, NULL);
	if (err != 0) {
		gflog_error(GFARM_MSG_1002743,
		    "add_control_event: %s", strerror(err));
	}
}

static void *
watcher(void *arg)
{
	struct watcher *w = arg;
	int err;

	err = gfarm_eventqueue_loop(w->q, NULL);
	gflog_fatal(GFARM_MSG_1002744, "watcher: %s", strerror(err));
	return (NULL);
}


gfarm_error_t
watcher_alloc(int size, struct watcher **wp)
{
	gfarm_error_t e;
	struct watcher *w;
	int err;

	GFARM_MALLOC(w);
	if (w == NULL) {
		gflog_error(GFARM_MSG_1002745, "watcher_alloc: no memory");
		e = GFARM_ERR_NO_MEMORY;
	} else {
		if ((err = gfarm_eventqueue_alloc(size, &w->q)) != 0) {
			gflog_error(GFARM_MSG_1002746,
			    "gfarm_eventqueue_alloc: %s", strerror(err));
			e = gfarm_errno_to_error(err);
		} else {
			e = watcher_request_queue_init(&w->wrq);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1002747,
				    "watcher_request_queue_init: %s",
				    gfarm_error_string(e));
			} else {
				w->control_gev = gfarm_fd_event_alloc(
				    GFARM_EVENT_READ,
				    watcher_request_queue_get_fd(&w->wrq),
				    watcher_control_callback, w);
				if (w->control_gev == NULL) {
					gflog_error(GFARM_MSG_1002748,
					    "gfarm_fd_event_alloc: no memory");
					e = GFARM_ERR_NO_MEMORY;
				} else {
					err = gfarm_eventqueue_add_event(
					    w->q, w->control_gev, NULL);
					if (err != 0) {
						gflog_error(GFARM_MSG_1002749,
						    "add_control_event"
						    ": %s", strerror(err));
						e = gfarm_errno_to_error(err);
					} else {
						e = create_detached_thread(
							watcher, w);
						if (e != GFARM_ERR_NO_ERROR) {
							gflog_error(
							    GFARM_MSG_1002750,
							    "create_detached_"
							    "thread: %s",
							    gfarm_error_string(
							    e));
						} else {
							*wp = w;
							return (e);
						}
					}
					gfarm_event_free(w->control_gev);
				}
				watcher_request_queue_destroy(&w->wrq);
			}
			gfarm_eventqueue_free(w->q);
		}
		free(w);
	}

	return (e);
}

void
watcher_add_event(struct watcher *w, struct watcher_event *wev,
	struct thread_pool *thrpool, void *(*handler)(void *), void *closure)
{
	watcher_request_enqueue(&w->wrq, wev, thrpool, handler, closure);
}

#if 0
gfarm_error_t
watcher_add_event_with_timeout(struct watcher *w, struct watcher_event *wev,
	const struct timeval *timeout, void *(*timeout_handler)(void *))
{
}
#endif

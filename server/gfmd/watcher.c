#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
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

struct watcher_event {
	/* for watcher_request_queue::list */
	struct watcher_event *prev, *next;

	struct gfarm_event *gev;

	int gfevent_filter;
	struct thread_pool *thrpool;
	void *(*handler)(void *);
	void *closure;
#define WATCHER_EVENT_IN_QUEUE	1 /* in watcher_request_queue */
#define WATCHER_EVENT_WATCHING	2 /* in gfarm_eventqueue */
#define WATCHER_EVENT_INVOKING	4 /* in (*wev->handler)() */
	int flags;

	/* GFARM_EVENT_TIMEOUT case only */
	struct timeval timeout;

	int gfevent_happened;

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
	wev->gfevent_happened = events;
	wev->flags &= ~WATCHER_EVENT_WATCHING;
	wev->flags |= WATCHER_EVENT_INVOKING;
	p = wev->thrpool; wev->thrpool = NULL;
	h = wev->handler; wev->handler = NULL;
	c = wev->closure; wev->closure = NULL;
	gfarm_mutex_unlock(&wev->mutex, module_name, "event callback");
	thrpool_add_job(p, h, c);
}

static void
watcher_event_init(struct watcher_event *wev,
	int filter, struct gfarm_event *gev)
{
	wev->gev = gev;
	wev->prev = wev->next = NULL;
	wev->gfevent_filter = filter;
	wev->flags = 0;

	wev->thrpool = NULL;
	wev->handler = NULL;
	wev->closure = NULL;

	wev->timeout.tv_sec = wev->timeout.tv_usec = 0;

	gfarm_mutex_init(&wev->mutex, module_name, "watcher_event");
}

static gfarm_error_t
watcher_fd_event_alloc(int filter, int fd, struct watcher_event **wevp)
{
	struct watcher_event *wev;
	struct gfarm_event *gev;

	GFARM_MALLOC(wev);
	if (wev == NULL)
		return (GFARM_ERR_NO_MEMORY);
	gev = gfarm_fd_event_alloc(filter, fd, watcher_event_callback, wev);
	if (gev == NULL) {
		free(wev);
		return (GFARM_ERR_NO_MEMORY);
	}
	watcher_event_init(wev, filter, gev);
	*wevp = wev;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
watcher_fd_readable_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(GFARM_EVENT_READ, fd, wevp));
}

gfarm_error_t
watcher_fd_writable_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(GFARM_EVENT_WRITE, fd, wevp));
}

gfarm_error_t
watcher_fd_readable_or_timeout_event_alloc(int fd, struct watcher_event **wevp)
{
	return (watcher_fd_event_alloc(GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT,
	    fd, wevp));
}

void
watcher_event_free(struct watcher_event *wev)
{
	if (wev->gev != NULL)
		gfarm_event_free(wev->gev);

	gfarm_mutex_destroy(&wev->mutex, module_name, "watcher_event");

	free(wev);
}

int
watcher_event_is_readable(struct watcher_event *wev)
{
	return ((wev->gfevent_happened & GFARM_EVENT_READ) != 0);
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
	struct watcher_event *wev, const struct timeval *timeout,
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

	if (timeout != NULL)
		wev->timeout = *timeout;

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
	struct watcher_event *list0, *wev;
	int err;

	assert(events == GFARM_EVENT_READ);

	list0 = watcher_request_queue_dequeue(&w->wrq);
	assert(list0 != NULL); /* doubly linked circular list */

	wev = list0;
	do {
		err = gfarm_eventqueue_add_event(w->q, wev->gev,
		    (wev->gfevent_filter & GFARM_EVENT_TIMEOUT) ?
		    &wev->timeout : NULL);

		if (err == 0) {
			wev->flags |= WATCHER_EVENT_WATCHING;
		} else {
			gflog_error(GFARM_MSG_1002742,
			    "add_event(filter:0x%x, handler:%p): %s",
			    wev->gfevent_filter, wev->handler, strerror(err));
		}
		/*
		 * the following flags must be cleared after adding
		 * the WATCHER_EVENT_WATCHING flag above.
		 * otherwise the race condition of SF.net #616 appears.
		 */
		wev->flags &= ~(WATCHER_EVENT_IN_QUEUE|WATCHER_EVENT_INVOKING);
		wev = wev->next;
	} while (wev != list0);

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
	assert((wev->gfevent_filter & GFARM_EVENT_TIMEOUT) == 0);

	watcher_request_enqueue(&w->wrq, wev, NULL, thrpool, handler, closure);
}

void
watcher_add_event_with_timeout(struct watcher *w, struct watcher_event *wev,
	long timeout_millisec,
	struct thread_pool *thrpool, void *(*handler)(void *), void *closure)
{
	struct timeval tv;

	assert((wev->gfevent_filter & GFARM_EVENT_TIMEOUT) != 0);

	/*
	 * this is inaccurate, because there is delay until
	 * gfarm_eventqueue_add_event() is called.
	 *
	 * but it's not big deal to make it accurate at least for now.
	 */
	tv.tv_sec = tv.tv_usec = 0;
	gfarm_timeval_add_microsec(&tv,
	    timeout_millisec * GFARM_MILLISEC_BY_MICROSEC);

	watcher_request_enqueue(&w->wrq, wev, &tv, thrpool, handler, closure);
}

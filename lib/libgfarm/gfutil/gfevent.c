#include <stdio.h>
/* XXX implement, poll, /dev/poll, kqueue and epoll version */

#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <limits.h> /* CHAR_BIT */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gflog.h>

#include <gfarm/gfarm_config.h>
#ifdef __KERNEL__	/* HAVE_EPOLL :: not yet support */
#undef HAVE_EPOLL
#endif /* __KERNEL__ */

#ifdef HAVE_EPOLL

#include <sys/epoll.h>

#define SECOND_BY_MILLISEC	1000
#define MILLISEC_BY_MICROSEC	1000

#else /* !HAVE_EPOLL */

#include <sys/select.h>

#define MIN_FDS_SIZE	FD_SETSIZE
#ifdef __FDS_BITS /* for glibc, esp. Debian/kFreeBSD */
#define GF_FDS_BITS(set)	__FDS_BITS(set)
#else
#define GF_FDS_BITS(set)	((set)->fds_bits)
#endif

#endif /* !HAVE_EPOLL */

#include "gfutil.h"
#include "gfevent.h"

/* event */

struct gfarm_event {
	/* doubly linked circular list with a header */
	struct gfarm_event *next, *prev;

	int filter;
	void *closure;
	struct timeval timeout;
	int timeout_specified;

	enum { GFARM_FD_EVENT, GFARM_TIMER_EVENT
#ifdef __KERNEL__
		, GFARM_KERN_EVENT
#endif /* __KERNEL__ */
	} type;

	union {
		struct gfarm_fd_event {
			void (*callback)(int, int, void *,
				const struct timeval *);
			int fd;
		} fd;
		struct gfarm_timer_event {
			void (*callback)(void *, const struct timeval *);
		} timeout;
#ifdef __KERNEL__
		struct gfarm_kern_event {
			void (*callback)(int, int, void *, void *);
			void *kevp;
		} kern;
#endif /* __KERNEL__ */
	} u;
};

#ifndef HAVE_EPOLL
static int gfarm_eventqueue_alloc_fd_set(struct gfarm_eventqueue *q,
	int fd, fd_set **fd_setpp);
#endif

struct gfarm_event *
gfarm_fd_event_alloc(int filter, int fd,
	void (*callback)(int, int, void *, const struct timeval *),
	void *closure)
{
	struct gfarm_event *ev;

	GFARM_MALLOC(ev);
	if (ev == NULL)
		return (NULL);
	ev->next = ev->prev = NULL; /* to be sure */
	ev->type = GFARM_FD_EVENT;
	ev->filter = filter;
	ev->closure = closure;
	ev->u.fd.callback = callback;
	ev->u.fd.fd = fd;
	return (ev);
}

void
gfarm_fd_event_set_callback(struct gfarm_event *ev,
	void (*callback)(int, int, void *, const struct timeval *),
	void *closure)
{
	ev->closure = closure;
	ev->u.fd.callback = callback;
}

struct gfarm_event *
gfarm_timer_event_alloc(
	void (*callback)(void *, const struct timeval *), void *closure)
{
	struct gfarm_event *ev;

	GFARM_MALLOC(ev);
	if (ev == NULL) {
		gflog_debug(GFARM_MSG_1000767,
			"allocation of gfarm_event failed");
		return (NULL);
	}
	ev->next = ev->prev = NULL; /* to be sure */
	ev->type = GFARM_TIMER_EVENT;
	ev->filter = GFARM_EVENT_TIMEOUT;
	ev->closure = closure;
	ev->u.timeout.callback = callback;
	return (ev);
}

void
gfarm_timer_event_set_callback(struct gfarm_event *ev,
	void (*callback)(void *, const struct timeval *), void *closure)
{
	ev->closure = closure;
	ev->u.timeout.callback = callback;
}

#ifdef __KERNEL__
struct gfarm_event *
gfarm_kern_event_alloc(void *kevp,
	void (*callback)(int, int, void *, void *),
	void *closure)
{
	struct gfarm_event *ev;

	GFARM_MALLOC(ev);
	if (ev == NULL)
		return (NULL);
	ev->next = ev->prev = NULL; /* to be sure */
	ev->type = GFARM_KERN_EVENT;
	ev->filter = GFARM_EVENT_TIMEOUT;
	ev->closure = closure;
	ev->u.kern.callback = callback;
	ev->u.kern.kevp = kevp;
	return (ev);
}
#endif /* __KERNEL__ */

void
gfarm_event_free(struct gfarm_event *ev)
{
#ifdef __KERNEL__
	if (ev && ev->type == GFARM_KERN_EVENT) {
		if (ev->u.kern.kevp) {
			(*ev->u.kern.callback)(GFARM_EVENT_TIMEOUT, -EIO,
				ev->u.kern.kevp, ev->closure);
		}
	}
#endif /* __KERNEL__ */
	free(ev);
}

/* event queue */

struct gfarm_eventqueue {
	/* doubly linked circular list with a header */
	struct gfarm_event header;

#ifdef HAVE_EPOLL
	int size_epoll_events, n_epoll_events;
	struct epoll_event *epoll_events;
	int epoll_fd;
#else
	int fd_set_size, fd_set_bytes;
	fd_set *read_fd_set, *write_fd_set, *exception_fd_set;
#endif
#ifdef __KERNEL__
	int n_kern;
	int evfd;
#endif /* __KERNEL__ */
};

int
gfarm_eventqueue_alloc(int ndesc_hint, struct gfarm_eventqueue **qp)
{
	struct gfarm_eventqueue *q;

	GFARM_MALLOC(q);
	if (q == NULL) {
		gflog_debug(GFARM_MSG_1000768,
			"allocation of gfarm_eventqueue failed");
		return (ENOMEM);
	}

	/* make the queue empty */
	q->header.next = q->header.prev = &q->header;

#ifdef HAVE_EPOLL
	q->size_epoll_events = q->n_epoll_events = 0;
	q->epoll_events = NULL;
	q->epoll_fd = epoll_create(ndesc_hint);
	if (q->epoll_fd == -1) {
		free(q);
		return (errno);
	}
#else
	q->fd_set_size = q->fd_set_bytes = 0;
	q->read_fd_set = q->write_fd_set =
	    q->exception_fd_set = NULL;
#endif
#ifdef __KERNEL__
	q->n_kern = 0;

	if ((q->evfd = gfsk_evfd_create(0)) < 0) {
		errno = -q->evfd;
		free(q);
		return (errno);
	}
	if (!gfarm_eventqueue_alloc_fd_set(q, q->evfd, &q->read_fd_set)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'q->read_fd_set' failed");
		close(q->evfd);
		free(q);
		return (ENOMEM);
	}
#endif /* __KERNEL__ */
	*qp = q;
	return (0); /* no error */
}

void
gfarm_eventqueue_free(struct gfarm_eventqueue *q)
{
#ifdef HAVE_EPOLL
	free(q->epoll_events);
	close(q->epoll_fd);
#else
	free(q->read_fd_set);
	free(q->write_fd_set);
	free(q->exception_fd_set);
#endif

#ifdef __KERNEL__
	if (q->evfd >= 0) {
		close(q->evfd);
	}
#endif /* __KERNEL__ */
#if 0 /* this may not be true, if gfarm_eventqueue_loop() fails */
	/* assert that the queue is empty */
	assert(q->header.next == &q->header && q->header.prev == &q->header);
#endif

	free(q);
}

#ifdef __KERNEL__
int
gfarm_kern_eventqueue_getevfd(struct gfarm_eventqueue *q)
{
	return (q->evfd);
}
#endif /* __KERNEL__ */

#ifndef HAVE_EPOLL
/*
 * XXX This is not so portable,
 * but fixed-size fd_set cannot be used.
 */
static int
gfarm_eventqueue_realloc_fd_set(size_t old_bytes, size_t new_bytes,
	fd_set **fd_setpp)
{
	fd_set *fsp;

	if (*fd_setpp != NULL) {
		fsp = realloc(*fd_setpp, new_bytes);
		if (fsp == NULL) {
			gflog_debug(GFARM_MSG_1000769,
				"re-allocation of fd_set failed");
			return (0); /* failure */
		}
		*fd_setpp = fsp;
		/*
		 * We need to clear fd_set here, because
		 * gfarm_eventqueue_add_event() may be called
		 * from a callback in a loop of gfarm_eventqueue_turn().
		 */
		/* assumes always new_bytes > old_bytes */
		memset((char *)fsp + old_bytes, 0, new_bytes - old_bytes);
	}
	return (1); /* success */
}

static int
gfarm_eventqueue_alloc_fd_set(struct gfarm_eventqueue *q, int fd,
	fd_set **fd_setpp)
{
	fd_set *fsp;

	if (fd >= q->fd_set_size) {
		size_t fds_size, fds_array_length, fds_bytes, fd_set_size;
		int overflow = 0;

		fds_size = q->fd_set_size > 0 ? q->fd_set_size : MIN_FDS_SIZE;
		for (; fd >= fds_size; fds_size += fds_size)
			;
		/*
		 * This calculates:
		 *	howmany(fds_size, sizeof(fsp->fds_bits[0]) * CHAR_BIT);
		 * where howmany(x, y) == (((x) + ((y) - 1)) / (y))
		 */
		fds_array_length = gfarm_size_add(&overflow, fds_size,
		    (sizeof(GF_FDS_BITS(fsp)[0]) * CHAR_BIT) - 1) /
		    (sizeof(GF_FDS_BITS(fsp)[0]) * CHAR_BIT);
		fds_bytes = gfarm_size_mul(&overflow,
		    fds_array_length, sizeof(GF_FDS_BITS(fsp)[0]));
		fd_set_size = gfarm_size_mul(&overflow,
		    fds_bytes, CHAR_BIT);
		if (overflow) {
			gflog_debug(GFARM_MSG_1000770,
				"overflow in multiplication of (%zd) and (%u)",
				fds_bytes, CHAR_BIT);
			return (0); /* failure */
		}
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes, fds_bytes,
		    &q->read_fd_set)) {
			gflog_debug(GFARM_MSG_1000771,
				"re-allocation of 'q->read_fd_set' failed");
			return (0); /* failure */
		}
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes, fds_bytes,
		    &q->write_fd_set)) { /* XXX wastes q->read_fd_set_value */
			gflog_debug(GFARM_MSG_1000772,
				"re-allocation of 'q->write_fd_set' failed");
			return (0); /* failure */
		}
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes, fds_bytes,
		    &q->exception_fd_set)) {
			/*XXX wastes q->{r,w}*_fd_set_value*/
			gflog_debug(GFARM_MSG_1000773,
				"re-allocation of 'q->exception_fd_set' "
				"failed");
			return (0); /* failure */
		}

		q->fd_set_bytes = fds_bytes;
		q->fd_set_size = fd_set_size;
	}
	if (*fd_setpp == NULL) {
		/*
		 * XXX This is not so portable,
		 * but fixed-size fd_set cannot be used.
		 */
		*fd_setpp = malloc(q->fd_set_bytes);
		if (*fd_setpp == NULL) {
			gflog_debug(GFARM_MSG_1000774,
				"allocation of fd_set failed");
			return (0); /* failure */
		}
		/*
		 * We need to clear fd_set here, because
		 * gfarm_eventqueue_add_event() may be called
		 * from a callback in a loop of gfarm_eventqueue_turn().
		 */
		memset(*fd_setpp, 0, q->fd_set_bytes);
	}
	return (1); /* success */
}
#endif /* !HAVE_EPOLL */

int
gfarm_eventqueue_add_event(struct gfarm_eventqueue *q,
	struct gfarm_event *ev, const struct timeval *timeout)
{
#ifdef HAVE_EPOLL
	struct epoll_event epoll_ev;
#endif

	if (ev->next != NULL || ev->prev != NULL) /* shouldn't happen */
		return (EINVAL);

	if (timeout == NULL) {
		ev->timeout_specified = 0;
	} else if ((ev->filter & GFARM_EVENT_TIMEOUT) != 0) {
		ev->timeout_specified = 1;
		gettimeofday(&ev->timeout, NULL);
		gfarm_timeval_add(&ev->timeout, timeout);
	} else {
		/*
		 * if the event is not allocated with GFARM_EVENT_TIMEOUT,
		 * it's not allowed to specify a timeout.
		 */
		gflog_debug(GFARM_MSG_1000775,
			"Event is not allocated with GFARM_EVENT_TIMEOUT");
		return (EINVAL);
	}
	switch (ev->type) {
	case GFARM_FD_EVENT:
#ifdef HAVE_EPOLL
		memset(&epoll_ev, 0, sizeof(epoll_ev));
		epoll_ev.events = 0; /* We use the level triggered mode */
		if ((ev->filter & GFARM_EVENT_READ) != 0)
			epoll_ev.events |= EPOLLIN;
		if ((ev->filter & GFARM_EVENT_WRITE) != 0)
			epoll_ev.events |= EPOLLOUT;
		if ((ev->filter & GFARM_EVENT_EXCEPTION) != 0)
			epoll_ev.events |= EPOLLPRI;
		epoll_ev.data.ptr = ev;
		if (epoll_ctl(q->epoll_fd, EPOLL_CTL_ADD,
		    ev->u.fd.fd, &epoll_ev) == -1) {
			int save_errno = errno;
			gflog_debug(GFARM_MSG_1002519,
			    "epoll(%d, EPOLL_CTL_ADD, %d, %p): %s",
			    q->epoll_fd, ev->u.fd.fd, &epoll_ev,
			    strerror(errno));
			return (save_errno);
		}
		q->n_epoll_events++;
#else
		if ((ev->filter & GFARM_EVENT_READ) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->read_fd_set)) {
				gflog_debug(GFARM_MSG_1000776,
					"allocation of 'q->read_fd_set' "
					"failed");
				return (ENOMEM);
			}
		}
		if ((ev->filter & GFARM_EVENT_WRITE) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->write_fd_set)) {
				gflog_debug(GFARM_MSG_1000777,
					"allocation of 'q->write_fd_set' "
					"failed");
				return (ENOMEM);
			}
		}
		if ((ev->filter & GFARM_EVENT_EXCEPTION) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->exception_fd_set)) {
				gflog_debug(GFARM_MSG_1000778,
					"allocation of 'q->exception_fd_set' "
					"failed");
				return (ENOMEM);
			}
		}
#endif
		break;
	case GFARM_TIMER_EVENT:
		if (timeout == NULL) {
			gflog_debug(GFARM_MSG_1000779,
				"Event type is GFARM_TIMER_EVENT but "
				"timeout is NULL");
			return (EINVAL); /* not allowed */
		}
		break;
#ifdef __KERNEL__
	case GFARM_KERN_EVENT:
		q->n_kern++;
		break;
#endif /* __KERNEL__ */
	}

	/* enqueue - insert at the tail of the circular list */
	ev->next = &q->header;
	ev->prev = q->header.prev;
	q->header.prev->next = ev;
	q->header.prev = ev;
	return (0);
}

int
gfarm_eventqueue_delete_event(struct gfarm_eventqueue *q,
	struct gfarm_event *ev)
{
#ifdef HAVE_EPOLL
	/* dummy is for linux before 2.6.9 */
	struct epoll_event dummy;
#endif

	if (ev->next == NULL || ev->prev == NULL) { /* shouldn't happen */
		gflog_debug(GFARM_MSG_1000780,
			"Event queue link broken");
		return (EINVAL);
	}

	switch (ev->type) {

	case GFARM_FD_EVENT:
#ifdef HAVE_EPOLL
		memset(&dummy, 0, sizeof(dummy));
		if (epoll_ctl(q->epoll_fd, EPOLL_CTL_DEL, ev->u.fd.fd, &dummy)
		    == -1) {
			gflog_warning(GFARM_MSG_1002520,
			    "epoll_ctl(%d, EPOLL_CTL_DEL, %d, ): %s",
			     q->epoll_fd, ev->u.fd.fd, strerror(errno));
		}
		q->n_epoll_events--;
#endif
		break;
	case GFARM_TIMER_EVENT:
		/* nothing to do */
		break;
#ifdef __KERNEL__
	case GFARM_KERN_EVENT:
		q->n_kern--;
		break;
#endif /* __KERNEL__ */
	}

	/* dequeue */
	ev->next->prev = ev->prev;
	ev->prev->next = ev->next;
	ev->next = ev->prev = NULL; /* to be sure */
	return (0);
}

/*
 * run one turn of select(2) loop.
 * this function may return before the timeout.
 *
 * RETURN VALUE:
 *  0: All events are processed, and no event is remaining in the queue.
 *  EAGAIN: There are still pending events.
 *  EDEADLK: no watching file descriptor is requested, and any timer event
 *	isn't specified, either. This means infinite sleep.
 *  otherwise: select(2) failed, return value shows the `errno' of
 *	the select(2).
 */
int
gfarm_eventqueue_turn(struct gfarm_eventqueue *q,
	const struct timeval *timeo)
{
	int nfound;
	struct gfarm_event *ev, *n;
	struct timeval start_time, end_time, timeout_value, *timeout = NULL;
	int events;
#ifndef HAVE_EPOLL
	int max_fd = -1;
	fd_set *read_fd_set, *write_fd_set, *exception_fd_set;
#endif

	/* queue is empty? */
	if (q->header.next == &q->header)
		return (0); /* finished */

	/*
	 * prepare arguments for select(2)
	 */
	if (timeo != NULL) {
		timeout_value = *timeo;
		timeout = &timeout_value;
	}
#ifndef HAVE_EPOLL
	/*
	 * XXX This is not so portable,
	 * but usual implementation of FD_ZERO cannot be used.
	 */
	if (q->read_fd_set != NULL)
		memset(q->read_fd_set, 0, q->fd_set_bytes);
	if (q->write_fd_set != NULL)
		memset(q->write_fd_set, 0, q->fd_set_bytes);
	if (q->exception_fd_set != NULL)
		memset(q->exception_fd_set, 0, q->fd_set_bytes);
	read_fd_set = write_fd_set = exception_fd_set = NULL;
#endif
	for (ev = q->header.next; ev != &q->header; ev = ev->next) {
		if (ev->timeout_specified) {
			if (timeout == NULL) {
				timeout = &timeout_value;
				timeout_value = ev->timeout;
			} else if (gfarm_timeval_cmp(&ev->timeout,timeout) <0){
				timeout_value = ev->timeout;
			}
		}
		switch (ev->type) {
		case GFARM_FD_EVENT:
#ifndef HAVE_EPOLL
			if ((ev->filter & GFARM_EVENT_READ) != 0) {
				read_fd_set = q->read_fd_set;
				FD_SET(ev->u.fd.fd, read_fd_set);
			}
			if ((ev->filter & GFARM_EVENT_WRITE) != 0) {
				write_fd_set = q->write_fd_set;
				FD_SET(ev->u.fd.fd, write_fd_set);
			}
			if ((ev->filter & GFARM_EVENT_EXCEPTION) != 0) {
				exception_fd_set = q->exception_fd_set;
				FD_SET(ev->u.fd.fd, exception_fd_set);
			}
			if (ev->u.fd.fd > max_fd)
				max_fd = ev->u.fd.fd;
#endif
			break;
		case GFARM_TIMER_EVENT:
			break;
#ifdef __KERNEL__
		case GFARM_KERN_EVENT:
			break;
#endif /* __KERNEL__ */
		}
	}
#ifdef __KERNEL__
	if (q->n_kern) {
		read_fd_set = q->read_fd_set;
		FD_SET(q->evfd, read_fd_set);
		if (q->evfd > max_fd)
			max_fd = q->evfd;
	}
#endif /* __KERNEL__ */

	/*
	 * do wait
	 */
#ifndef HAVE_EPOLL
	if (max_fd < 0 && timeout == NULL)
		return (EDEADLK); /* infinite sleep without any watching fd */
#endif
	gettimeofday(&start_time, NULL);
	if (timeout != NULL) {
		gfarm_timeval_sub(&timeout_value, &start_time);
		if (timeout_value.tv_sec < 0)
			timeout_value.tv_sec = timeout_value.tv_usec = 0;
	}
#ifdef HAVE_EPOLL
	if (q->size_epoll_events <= q->n_epoll_events) {
		struct epoll_event *p;
		int sz;
		if (q->size_epoll_events * 2 >= q->n_epoll_events)
			sz = q->size_epoll_events * 2;
		else
			sz = q->n_epoll_events;
		GFARM_REALLOC_ARRAY(p, q->epoll_events, sz);
		if (p == NULL)
			return (ENOMEM);
		q->epoll_events = p;
		q->size_epoll_events = sz;
	}
	nfound = epoll_wait(q->epoll_fd, q->epoll_events, q->size_epoll_events,
	    timeout == NULL ? -1 :
	    timeout_value.tv_sec * SECOND_BY_MILLISEC +
	    timeout_value.tv_usec / MILLISEC_BY_MICROSEC);
#else
	nfound = select(max_fd + 1,
	    read_fd_set, write_fd_set, exception_fd_set, timeout);
#endif
	if (nfound == -1) {
		int save_errno = errno;

		if (save_errno != EINTR) {
#ifdef HAVE_EPOLL
			gflog_debug(GFARM_MSG_1000781,
			    "epoll_wait() failed: %s", strerror(save_errno));
#else
			gflog_debug(GFARM_MSG_1003564,
			    "select() failed: %s", strerror(save_errno));
#endif
		}
		return (save_errno);
	}
	gettimeofday(&end_time, NULL);

	/*
	 * call event callback routines
	 */
#ifdef HAVE_EPOLL
	if (nfound > 0) {
		int i;

		for (i = 0; i < nfound; i++) {
			ev = q->epoll_events[i].data.ptr;
			events = 0;
			if ((q->epoll_events[i].events & EPOLLIN) != 0)
				events |= GFARM_EVENT_READ;
			if ((q->epoll_events[i].events & EPOLLOUT) != 0)
				events |= GFARM_EVENT_WRITE;
			if ((q->epoll_events[i].events & EPOLLPRI) != 0)
				events |= GFARM_EVENT_EXCEPTION;
			if ((q->epoll_events[i].events & (EPOLLHUP|EPOLLERR))
			    != 0)
				events |= ev->filter & (GFARM_EVENT_READ|
				    GFARM_EVENT_WRITE|GFARM_EVENT_EXCEPTION);
			gfarm_eventqueue_delete_event(q, ev);
			(*ev->u.fd.callback)(events, ev->u.fd.fd, ev->closure,
			    &end_time);
		}
	} else {
		for (ev = q->header.next; ev != &q->header; ev = n) {
			n = ev->next;

			switch (ev->type) {
			case GFARM_FD_EVENT:
				if (ev->timeout_specified &&
				    gfarm_timeval_cmp(&end_time, &ev->timeout)
				    >= 0) {
					gfarm_eventqueue_delete_event(q, ev);
					(*ev->u.fd.callback)(
					    GFARM_EVENT_TIMEOUT, ev->u.fd.fd,
					    ev->closure, &end_time);
				}
				break;
			case GFARM_TIMER_EVENT:
				if (gfarm_timeval_cmp(&end_time, &ev->timeout)
				    >= 0){
					gfarm_eventqueue_delete_event(q, ev);
					(*ev->u.timeout.callback)(
					    ev->closure, &end_time);
				}
				break;
			default :
				break;
			}
		}
	}
#else /* !HAVE_EPOLL */
	for (ev = q->header.next; ev != &q->header; ev = n) {
		/*
		 * We shouldn't use "ev = ev->next" in the 3rd clause
		 * of above "for(;;)" statement, because ev may be
		 * deleted in this loop.
		 */
		n = ev->next;

		switch (ev->type) {
		case GFARM_FD_EVENT:
			events = 0;
			if ((ev->filter & GFARM_EVENT_READ) != 0 &&
			    FD_ISSET(ev->u.fd.fd, q->read_fd_set))
				events |= GFARM_EVENT_READ;
			if ((ev->filter & GFARM_EVENT_WRITE) != 0 &&
			    FD_ISSET(ev->u.fd.fd, q->write_fd_set))
				events |= GFARM_EVENT_WRITE;
			if ((ev->filter & GFARM_EVENT_EXCEPTION) != 0 &&
			    FD_ISSET(ev->u.fd.fd, q->exception_fd_set))
				events |= GFARM_EVENT_EXCEPTION;
			if (events != 0) {
				gfarm_eventqueue_delete_event(q, ev);
				/* here is a good breakpoint on a debugger */
				(*ev->u.fd.callback)(events, ev->u.fd.fd,
				    ev->closure, &end_time);
			} else if (ev->timeout_specified &&
			    gfarm_timeval_cmp(&end_time, &ev->timeout) >= 0) {
				gfarm_eventqueue_delete_event(q, ev);
				(*ev->u.fd.callback)(
				    GFARM_EVENT_TIMEOUT, ev->u.fd.fd,
				    ev->closure, &end_time);
			}
			break;
#ifdef __KERNEL__
		case GFARM_KERN_EVENT:
			if (FD_ISSET(q->evfd, q->read_fd_set)) {
				int fd;
				if (gfsk_req_check_fd(ev->u.kern.kevp, &fd)) {
					char buf[1];
					read(q->evfd, buf, 1);
					ev->u.kern.kevp = NULL;
					gfarm_eventqueue_delete_event(q, ev);
					(*ev->u.kern.callback)(GFARM_EVENT_READ,
						fd, NULL, ev->closure);
					break;
				}

			}
			if (ev->timeout_specified &&
			    gfarm_timeval_cmp(&end_time, &ev->timeout) >= 0) {
				void *kevp = ev->u.kern.kevp;
				ev->u.kern.kevp = NULL;
				gfarm_eventqueue_delete_event(q, ev);
				(*ev->u.kern.callback)(GFARM_EVENT_TIMEOUT,
					-ETIME, kevp, ev->closure);
			}
			break;
#endif /* __KERNEL__ */
		case GFARM_TIMER_EVENT:
			if (gfarm_timeval_cmp(&end_time, &ev->timeout) >= 0) {
				gfarm_eventqueue_delete_event(q, ev);
				(*ev->u.timeout.callback)(
				    ev->closure, &end_time);
			}
			break;
		}
	}
#endif /* !HAVE_EPOLL */

	/* queue is empty? */
	if (q->header.next == &q->header)
		return (0); /* finished */

	return (EAGAIN);
}

/*
 * run whole select(2) loop.
 *
 * RETURN VALUE:
 *  0: All events are processed, and no event is remaining in the queue.
 *  EDEADLK: no watching file descriptor is requested, and any timer event
 *	isn't specified, either. This means infinite sleep.
 *  ETIMEDOUT: timeout happened.
 *  otherwise: select(2) failed, return value shows the `errno' of
 *	the select(2).
 */
int
gfarm_eventqueue_loop(struct gfarm_eventqueue *q,
	const struct timeval *timeo)
{
	struct timeval limit, now, timeout_value, *timeout = NULL;
	int rv;

	if (timeo != NULL) {
		gettimeofday(&limit, NULL);
		gfarm_timeval_add(&limit, timeo);
		now = limit;
		timeout = &timeout_value;
	}
	for (;;) {
		if (timeout != NULL) {
			*timeout = limit;
			gfarm_timeval_sub(timeout, &now);
		}
		rv = gfarm_eventqueue_turn(q, timeout);
		if (rv == 0)
			return (0); /* completed */
		if (rv != EAGAIN && rv != EINTR) {
			gflog_debug(GFARM_MSG_1000782,
				"gfarm_eventqueue_turn() failed: %d",
				rv);
			return (rv); /* probably program logic is wrong */
		}
		if (timeout != NULL) {
			gettimeofday(&now, NULL);
			if (gfarm_timeval_cmp(&now, &limit) >= 0)
				return (ETIMEDOUT); /* timeout */
		}
	}
}

#if 0 /* sample usage */

#include <stdio.h>

struct proto1_state {
	struct gfarm_eventqueue *q;
	int sock;
	struct gfarm_event *writable, *readable;

	void (*continuation)(void *);
	void *closure;

	/* results */
	int error;
	unsigned char result;
};

void
proto1_receiving(int events, int fd, void *closure, const struct timeval *t)
{
	struct proto1_state *state = closure;
	int rv;

	rv = read(state->sock, &state->result, sizeof(state->result));
	if (rv == -1) {
		if (errno != EINTR && errno != EAGAIN)
			state->error = errno;
		else if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, NULL)) != 0) {
			state->error = rv;
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

void
proto1_sending(int events, int fd, void *closure, const struct timeval *t)
{
	struct proto1_state *state = closure;
	unsigned char request_code = 1;
	int rv;

	rv = write(state->sock, &request_code, sizeof(request_code));
	if (rv == -1) {
		if (errno != EINTR && errno != EAGAIN) {
			state->error = errno;
		} else if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) != 0) {
			state->error = rv;
		} else {
			return; /* go to proto1_sending() */
		}
	} else {
		if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->readable, NULL)) == 0) {
			return; /* go to proto1_receiving() */
		}
		state->error = rv;
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

int
proto1_request_multiplexed(struct gfarm_eventqueue *q, int peer_socket,
	void (*continuation)(void *), void *closure,
	struct proto1_state **statepp)
{
	struct proto1_state *state;
	int rv = ENOMEM;

	GFARM_MALLOC(state);
	if (state == NULL)
		return (ENOMEM);
	state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE, peer_socket,
	    proto1_sending, state);
	if (state->writable != NULL) {
		state->readable = gfarm_fd_event_alloc(
		    GFARM_EVENT_READ, peer_socket, proto1_receiving, state);
		if (state->readable != NULL) {
			state->q = q;
			state->sock = peer_socket;
			state->continuation = continuation;
			state->closure = closure;
			state->error = 0;
			rv = gfarm_eventqueue_add_event(q, state->writable,
			    NULL);
			if (rv == 0) {
				*statepp = state;
				return (0); /* go to proto1_sending() */
			}
			gfarm_event_free(state->readable);
		}
		gfarm_event_free(state->writable);
	}
	free(state);
	return (rv);
	
}

int
proto1_result_multiplexed(struct proto1_state *state, unsigned char *resultp)
{
	int error = state->error;

	if (error == 0)
		*resultp = state->result;
	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state);
	return (error);
}

struct p1_finalize_closure {
	char *name;
	struct proto1_state *state;
};

void
p1_finalize(void *closure)
{
	struct p1_finalize_closure *p1 = closure;
	unsigned char result;
	int error = proto1_result_multiplexed(p1->state, &result);

	if (error != 0)
		fprintf(stderr, "%s: error = %s\n", p1->name, strerror(error));
	else
		printf("%s: result=%c\n", p1->name, result);
}

void
run2(int host1_socket, int host2_socket)
{
	int error;
	struct gfarm_eventqueue *q;
	struct p1_finalize_closure fc1, fc2;

	/* initialize */

	if ((error = gfarm_eventqueue_alloc(2, &q)) != 0) {
		fprintf(stderr, "gfarm_eventqueue_alloc: %s\n",
		    strerror(error));
		return;
	}

	fc1.name = "host1"; fc1.state = NULL;
	error = proto1_request_multiplexed(q, host1_socket,
	    p1_finalize, &fc1,
	    &fc1.state);
	if (error != 0)
		fprintf(stderr, "host1: %s\n", strerror(error));

	fc2.name = "host2"; fc2.state = NULL;
	error = proto1_request_multiplexed(q, host2_socket,
	    p1_finalize, &fc2,
	    &fc2.state);
	if (error != 0)
		fprintf(stderr, "host2: %s\n", strerror(error));

	/* run */
	error = gfarm_eventqueue_loop(q, NULL);
	if (error != 0)
		fprintf(stderr, "host2: %s\n", strerror(error));

	/* terminate */
	gfarm_eventqueue_free(q);
}

#endif /* sample usage */

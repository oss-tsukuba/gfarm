#include <stdio.h>
/* XXX implement, poll, /dev/poll, kqueue and epoll version */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <assert.h>
#include <limits.h> /* CHAR_BIT */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/gfarm_misc.h>
#include "gfutil.h"
#include "gfevent.h"

#define MIN_FDS_SIZE	FD_SETSIZE

/* event */

struct gfarm_event {
	/* doubly linked circular list with a header */
	struct gfarm_event *next, *prev;

	int filter;
	void *closure;
	struct timeval timeout;
	int timeout_specified;

	enum { GFARM_FD_EVENT, GFARM_TIMER_EVENT } type;
	union {
		struct gfarm_fd_event {
			void (*callback)(int, int, void *,
				const struct timeval *);
			int fd;
		} fd;
		struct gfarm_timer_event {
			void (*callback)(void *, const struct timeval *);
		} timeout;
	} u;
};

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
gfarm_fd_event_set_callback(struct gfarm_event * ev,
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
	if (ev == NULL)
		return (NULL);
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

void
gfarm_event_free(struct gfarm_event *ev)
{
	free(ev);
}

/* event queue */

struct gfarm_eventqueue {
	/* doubly linked circular list with a header */
	struct gfarm_event header;

	int fd_set_size, fd_set_bytes;
	fd_set *read_fd_set, *write_fd_set, *exception_fd_set;
};

struct gfarm_eventqueue *
gfarm_eventqueue_alloc(void)
{
	struct gfarm_eventqueue *q;

	GFARM_MALLOC(q);
	if (q == NULL)
		return (NULL);

	/* make the queue empty */
	q->header.next = q->header.prev = &q->header;

	q->fd_set_size = q->fd_set_bytes = 0;
	q->read_fd_set = q->write_fd_set =
	    q->exception_fd_set = NULL;
	return (q);
}

void
gfarm_eventqueue_free(struct gfarm_eventqueue *q)
{
	if (q->read_fd_set != NULL)
		free(q->read_fd_set);
	if (q->write_fd_set != NULL)
		free(q->write_fd_set);
	if (q->exception_fd_set != NULL)
		free(q->exception_fd_set);

#if 0 /* this may not be true, if gfarm_eventqueue_loop() fails */
	/* assert that the queue is empty */
	assert(q->header.next == &q->header && q->header.prev == &q->header);
#endif

	free(q);
}

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
		if (fsp == NULL)
			return (0); /* failure */
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
		size_t fds_size, fds_array_length, fds_bytes;
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
		    (sizeof(fsp->fds_bits[0]) * CHAR_BIT) - 1) /
		    (sizeof(fsp->fds_bits[0]) * CHAR_BIT);
		fds_bytes = gfarm_size_mul(&overflow,
		    fds_array_length, sizeof(fsp->fds_bits[0]));
		if (overflow)
			return (0); /* failure */
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes,fds_bytes,
		    &q->read_fd_set))
			return (0); /* failure */
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes,fds_bytes,
		    &q->write_fd_set)) /* XXX wastes q->read_fd_set_value */
			return (0); /* failure */
		if (!gfarm_eventqueue_realloc_fd_set(q->fd_set_bytes,fds_bytes,
		    &q->exception_fd_set))/*XXX wastes q->{r,w}*_fd_set_value*/
			return (0); /* failure */

		q->fd_set_bytes = fds_bytes;
		q->fd_set_size = fds_bytes * CHAR_BIT;
	}
	if (*fd_setpp == NULL) {
		/*
		 * XXX This is not so portable,
		 * but fixed-size fd_set cannot be used.
		 */
		*fd_setpp = malloc(q->fd_set_bytes);
		if (*fd_setpp == NULL)
			return (0); /* failure */
		/*
		 * We need to clear fd_set here, because
		 * gfarm_eventqueue_add_event() may be called
		 * from a callback in a loop of gfarm_eventqueue_turn().
		 */
		memset(*fd_setpp, 0, q->fd_set_bytes);
	}
	return (1); /* success */
}

int
gfarm_eventqueue_add_event(struct gfarm_eventqueue *q,
	struct gfarm_event *ev, const struct timeval *timeout)
{
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
		return (EINVAL);
	}
	switch (ev->type) {
	case GFARM_FD_EVENT:
		if ((ev->filter & GFARM_EVENT_READ) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->read_fd_set))
				return (ENOMEM);
		}
		if ((ev->filter & GFARM_EVENT_WRITE) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->write_fd_set))
				return (ENOMEM);
		}
		if ((ev->filter & GFARM_EVENT_EXCEPTION) != 0) {
			if (!gfarm_eventqueue_alloc_fd_set(q, ev->u.fd.fd,
			    &q->exception_fd_set))
				return (ENOMEM);
		}
		break;
	case GFARM_TIMER_EVENT:
		if (timeout == NULL)
			return (EINVAL); /* not allowed */
		break;
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
	if (ev->next == NULL || ev->prev == NULL) /* shouldn't happen */
		return (EINVAL);

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
	int nfound, max_fd = -1;
	struct gfarm_event *ev, *n;
	fd_set *read_fd_set, *write_fd_set, *exception_fd_set;
	struct timeval start_time, end_time, timeout_value, *timeout = NULL;
	int events;

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
			break;
		case GFARM_TIMER_EVENT:
			break;
		}
	}

	/*
	 * do select(2)
	 */
	if (max_fd < 0 && timeout == NULL)
		return (EDEADLK); /* infinite sleep without any watching fd */
	gettimeofday(&start_time, NULL);
	if (timeout != NULL) {
		gfarm_timeval_sub(&timeout_value, &start_time);
		if (timeout_value.tv_sec < 0)
			timeout_value.tv_sec = timeout_value.tv_usec = 0;
	}
	nfound = select(max_fd + 1,
	    read_fd_set, write_fd_set, exception_fd_set, timeout);
	if (nfound == -1)
		return (errno);
	gettimeofday(&end_time, NULL);

	/*
	 * call event callback routines
	 */
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
		case GFARM_TIMER_EVENT:
			if (gfarm_timeval_cmp(&end_time, &ev->timeout) >= 0){
				gfarm_eventqueue_delete_event(q, ev);
				(*ev->u.timeout.callback)(
				    ev->closure, &end_time);
			}
			break;
		}
	}

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
		if (rv != EAGAIN && rv != EINTR)
			return (rv); /* probably program logic is wrong */
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

	if ((q = gfarm_eventqueue_alloc()) == NULL) {
		fprintf(stderr, "out of memory\n");
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

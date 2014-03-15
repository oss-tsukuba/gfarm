/*
 * This interface is a mimic of the libevent library by Niels Provos.
 *
 * XXX - make this library a wrapper to libevent, if the system has it.
 */

struct timeval;

/* event */

struct gfarm_event;

#define GFARM_EVENT_TIMEOUT	1
#define GFARM_EVENT_READ	2
#define GFARM_EVENT_WRITE	4
#define GFARM_EVENT_EXCEPTION	8

struct gfarm_event *gfarm_fd_event_alloc(int, int,
	void (*)(int, int, void *, const struct timeval *), void *);
void gfarm_fd_event_set_callback(struct gfarm_event *,
	void (*)(int, int, void *, const struct timeval *), void *);

/*
 * NOTE:
 * timer_event shouldn't be used for timeout handling of
 * read/write/exception processing, because it's possible that both a
 * timer_event handler and a read/write/exception handler are called
 * at once.  In other words, if timer_event is used for such timeout
 * handling, timer_event handler may be called even if timeout doesn't
 * actually happen.
 * If fd_event is used with GFARM_EVENT_TIMEOUT, it's guaranteed that
 * the TIMEOUT event and READ/WRITE/EXCEPTION event never happen at once.
 */
struct gfarm_event *gfarm_timer_event_alloc(
	void (*)(void *, const struct timeval *), void *);
void gfarm_timer_event_set_callback(struct gfarm_event *,
	void (*)(void *, const struct timeval *), void *);

void gfarm_event_free(struct gfarm_event *);

/* event queue */

struct gfarm_eventqueue;

int gfarm_eventqueue_alloc(int, struct gfarm_eventqueue **);
void gfarm_eventqueue_free(struct gfarm_eventqueue *);

int gfarm_eventqueue_add_event(struct gfarm_eventqueue *,
	struct gfarm_event *, const struct timeval *);
int gfarm_eventqueue_delete_event(struct gfarm_eventqueue *,
	struct gfarm_event *);

int gfarm_eventqueue_turn(struct gfarm_eventqueue *, const struct timeval *);
int gfarm_eventqueue_loop(struct gfarm_eventqueue *, const struct timeval *);

#ifdef __KERNEL__
struct gfarm_event *gfarm_kern_event_alloc(void *,
	void (*)(int, int, void *, void *), void *);
int gfarm_kern_eventqueue_getevfd(struct gfarm_eventqueue *);
#endif /* __KERNEL__ */

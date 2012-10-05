#include <pthread.h>
#include <sys/time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "nanosec.h"
#include "thrsubr.h"

#include "subr.h"
#include "thrpool.h"

struct callout {
	struct callout *prev, *next;

#define CALLOUT_PENDING		1
#define CALLOUT_FIRED		2
#define CALLOUT_INVOKING	4
	int state;

	struct timespec target_time;

	struct thread_pool *thrpool;
	void *(*func)(void *);
	void *closure;
};

struct callout_module {
	pthread_mutex_t mutex;
	pthread_cond_t have_things_to_run;

	/* dummy head of doubly linked circular list */
	struct callout pendings;
} callout_module;

static const char module_name[] = "callout_module";

int
timespec_cmp(const struct timespec *t1, const struct timespec *t2)
{
	if (t1->tv_sec > t2->tv_sec)
		return (1);
	if (t1->tv_sec < t2->tv_sec)
		return (-1);
	if (t1->tv_nsec > t2->tv_nsec)
		return (1);
	if (t1->tv_nsec < t2->tv_nsec)
		return (-1);
	return (0);
}

void *
callout_main(void *arg)
{
	struct callout_module *cm = arg;
	struct callout *c;
	struct thread_pool *thrpool;
	void *(*func)(void *);
	void *closure;
	int rv;
	struct timespec now;

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	thrpool = NULL;
	closure = NULL;
#endif
	for (;;) {
		gfarm_mutex_lock(&cm->mutex, module_name, "main lock");
		c = cm->pendings.next;
		if (c == &cm->pendings)
			rv = pthread_cond_wait(&cm->have_things_to_run,
			    &cm->mutex);
		else
			rv = pthread_cond_timedwait(&cm->have_things_to_run,
			    &cm->mutex, &c->target_time);
		if (rv != 0 && rv != ETIMEDOUT) {
			gflog_fatal(GFARM_MSG_1001490, "s: %s cond wait: %s",
			    module_name, strerror(rv));
		}

		func = NULL;

		/* cm->pendings may be changed while cond_wait */
		c = cm->pendings.next;
		if (c != &cm->pendings) {
			gfarm_gettime(&now);
			if (timespec_cmp(&c->target_time, &now) <= 0) {
				/* remove the head of the list */
				c->prev->next = c->next;
				c->next->prev = c->prev;
				/* clear the pointers to be sure */
				c->next = c;
				c->prev = c;
				c->state &= ~CALLOUT_PENDING;
				c->state |= (CALLOUT_FIRED | CALLOUT_INVOKING);
				thrpool = c->thrpool;
				func = c->func;
				closure = c->closure;
			}
		}
		gfarm_mutex_unlock(&cm->mutex, module_name, "main lock");

		if (func != NULL) {
			if (thrpool == NULL)
				(*func)(closure);
			else
				thrpool_add_job(thrpool, func, closure);
		}
	}
#ifdef __GNUC__ /* shut up stupid warning by gcc */
	/*NOTREACHED*/
	return (NULL);
#endif
}

/* This function is equivalent to callout_startup(9) */
void
callout_module_init(int nthreads)
{
	gfarm_error_t e;
	struct callout_module *cm = &callout_module;
	int i;

	gfarm_mutex_init(&cm->mutex, module_name, "init");
	gfarm_cond_init(&cm->have_things_to_run, module_name, "init");
	cm->pendings.prev = &cm->pendings;
	cm->pendings.next = &cm->pendings;

	for (i = 0; i < nthreads; i++) {
		e = create_detached_thread(callout_main, &callout_module);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_fatal(GFARM_MSG_1001491,
			    "callout_module_init: create_detached_thread: %s",
			    gfarm_error_string(e));
	}
}

/* This function is nearly equivalent to callout_init(9) */
struct callout *
callout_new(void)
{
	struct callout *c;

	GFARM_MALLOC(c);
	if (c == NULL)
		return (NULL);
	c->prev = c;
	c->next = c;
	c->state = 0;
	c->func = NULL;
	c->closure = NULL;
	return (c);
}

/*
 * This function is equivalent to callout_destroy(9)
 *
 * The callout must be stopped before calling this function.
 */
void
callout_free(struct callout *c)
{
	assert((c->state & CALLOUT_PENDING) == 0);
	free(c);
}

static void
callout_schedule_common(struct callout *n, int microseconds)
{
	struct callout_module *cm = &callout_module;
	struct callout *c;
	struct timeval now;

	/* callout_module.mutex must be already locked here */

	gettimeofday(&now, NULL);
	gfarm_timeval_add_microsec(&now, microseconds);
	n->target_time.tv_sec = now.tv_sec;
	n->target_time.tv_nsec = now.tv_usec * GFARM_MICROSEC_BY_NANOSEC;
	n->state &= ~(CALLOUT_FIRED | CALLOUT_INVOKING);

	if ((n->state & CALLOUT_PENDING) != 0) {
		/* remove from the pending list */
		n->prev->next = n->next;
		n->next->prev = n->prev;
	}

	/*
	 * Since currently all callouts are scheduled with same interval
	 * (i.e. gfarm_metadb_heartbeat_interval), searching from the tail
	 * is most efficient.
	 * XXX If we use another interval for something, this implementation
	 * should be changed to use buckets for efficiency.
	 */
	for (c = cm->pendings.prev; c != &cm->pendings;
	     c = c->prev) {
		if (timespec_cmp(&n->target_time, &c->target_time) >= 0)
			break;
	}
	/* insert n as the successor of c */
	n->prev = c;
	n->next = c->next;
	c->next->prev = n;
	c->next = n;
	n->state |= CALLOUT_PENDING;
	if (n->prev == &cm->pendings)
		gfarm_cond_signal(&cm->have_things_to_run, module_name,
		    "scheduling singal");
}

void
callout_schedule(struct callout *c, int microseconds)
{
	struct callout_module *cm = &callout_module;

	gfarm_mutex_lock(&cm->mutex, module_name, "schedule lock");
	callout_schedule_common(c, microseconds);
	gfarm_mutex_unlock(&cm->mutex, module_name, "schedule unlock");
}

/* if thrpool == NULL, use the callout_main thread to call the func. */
void
callout_reset(struct callout *c, int microseconds,
	struct thread_pool *thrpool, void *(*func)(void *), void *closure)
{
	struct callout_module *cm = &callout_module;

	gfarm_mutex_lock(&cm->mutex, module_name, "reset lock");
	c->thrpool = thrpool;
	c->func = func;
	c->closure = closure;
	callout_schedule_common(c, microseconds);
	gfarm_mutex_unlock(&cm->mutex, module_name, "reset unlock");
}

/* if thrpool == NULL, use the callout_main thread to call the func. */
void
callout_setfunc(struct callout *c,
	struct thread_pool *thrpool, void *(*func)(void *), void *closure)
{
	struct callout_module *cm = &callout_module;

	gfarm_mutex_lock(&cm->mutex, module_name, "reset lock");
	c->thrpool = thrpool;
	c->func = func;
	c->closure = closure;
	gfarm_mutex_unlock(&cm->mutex, module_name, "reset unlock");
}

int
callout_stop(struct callout *c)
{
	struct callout_module *cm = &callout_module;
	int expired;

	gfarm_mutex_lock(&cm->mutex, module_name, "stop lock");
	if ((c->state & CALLOUT_PENDING) != 0) {
		/* remove the head of the list */
		c->prev->next = c->next;
		c->next->prev = c->prev;
		/* clear the pointers to be sure */
		c->next = c;
		c->prev = c;
	}
	expired = (c->state & CALLOUT_FIRED) != 0;
	c->state &= ~(CALLOUT_PENDING | CALLOUT_FIRED);
	gfarm_mutex_unlock(&cm->mutex, module_name, "stop unlock");
	return (expired);
}

#ifdef NOT_USED
int
callout_invoking(struct callout *c)
{
	struct callout_module *cm = &callout_module;
	int invoking;

	gfarm_mutex_lock(&cm->mutex, module_name, "invoking lock");
	invoking = (c->state & CALLOUT_INVOKING) != 0;
	gfarm_mutex_unlock(&cm->mutex, module_name, "invoking unlock");
	return (invoking);
}

void
callout_ack(struct callout *c)
{
	struct callout_module *cm = &callout_module;

	gfarm_mutex_lock(&cm->mutex, module_name, "ack lock");
	c->state &= ~CALLOUT_INVOKING;
	gfarm_mutex_unlock(&cm->mutex, module_name, "ack unlock");
}
#endif /* NOT_USED */

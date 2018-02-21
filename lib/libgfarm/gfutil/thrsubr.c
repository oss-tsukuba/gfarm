#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "thrsubr.h"

/*
 * SF.net #1022:
 * HAVE_PTHREAD_PRIO_INHERIT is unstable with
 * kernel-2.6.32-504.el6.x86_64.rpm
 * glibc-2.12-1.166.el6_7.7.x86_64.rpm
 * on CentOS 6.6
 */
#undef HAVE_PTHREAD_PRIO_INHERIT

#ifdef HAVE_PTHREAD_PRIO_INHERIT

static pthread_mutexattr_t mutexattr_prio_inherit;
static pthread_mutexattr_t *mutexattr_prio_inherit_p = NULL;

static void
mutexattr_init(void)
{
	int err;

	err = pthread_mutexattr_init(&mutexattr_prio_inherit);
	if (err != 0) {
		gflog_notice(GFARM_MSG_1005032, "pthread_mutexattr_init: %s",
		    strerror(err));
		return;
	}
	err = pthread_mutexattr_setprotocol(&mutexattr_prio_inherit,
	    PTHREAD_PRIO_INHERIT);
	if (err != 0) {
		gflog_notice(GFARM_MSG_1005033,
		    "pthread_mutexattr_setprotocol"
		    "(, PTHREAD_PRIO_INHERIT): %s",
		    strerror(err));
		return;
	}
	mutexattr_prio_inherit_p = &mutexattr_prio_inherit;
}

static pthread_mutexattr_t *
mutex_prio_inherit(void)
{
	static pthread_once_t attr_init_once = PTHREAD_ONCE_INIT;

	pthread_once(&attr_init_once, mutexattr_init);
	return (mutexattr_prio_inherit_p);
}

static int mutex_init_w_attr_error_errno;

static void
mutex_init_w_attr_error(void)
{
	gflog_notice(GFARM_MSG_1005034,
	    "pthread_mutex_init(, PTHREAD_PRIO_INHERIT): %s",
	    strerror(mutex_init_w_attr_error_errno));
}

#endif /* HAVE_PTHREAD_PRIO_INHERIT */

void
gfarm_mutex_init(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err;
#ifdef HAVE_PTHREAD_PRIO_INHERIT
	pthread_mutexattr_t *attr = mutex_prio_inherit();
	static pthread_once_t report_once = PTHREAD_ONCE_INIT;

	if (attr != NULL) {
		err = pthread_mutex_init(mutex, attr);
		if (err == 0)
			return;
		mutex_init_w_attr_error_errno = err;
		pthread_once(&report_once, mutex_init_w_attr_error);
		/* ignore error, and try again with attr == NULL */
	}
#endif
	err = pthread_mutex_init(mutex, NULL);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000212, "%s: %s mutex init: %s",
		    where, what, strerror(err));
}

void
gfarm_mutex_lock(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_lock(mutex);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1000213, "%s: %s mutex lock: %s",
		    where, what, strerror(err));
}

/* false: EBUSY */
int
gfarm_mutex_trylock(pthread_mutex_t *mutex, const char *where,
	const char *what)
{
	int err = pthread_mutex_trylock(mutex);

	if (err != 0 && err != EBUSY)
		gflog_fatal(GFARM_MSG_1003565, "%s: %s mutex trylock: %s",
		    where, what, strerror(err));
	return (err == 0);
}

#ifdef HAVE_PTHREAD_MUTEX_TIMEDLOCK
/* false: ETIMEDOUT */
int
gfarm_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *timeout,
	const char *where, const char *what)
{
	int err = pthread_mutex_timedlock(mutex, timeout);

	if (err != 0 && err != ETIMEDOUT)
		gflog_fatal(GFARM_MSG_1004199, "%s: %s mutex timedlock: %s",
		    where, what, strerror(err));
	return (err == 0);
}
#endif /* HAVE_PTHREAD_MUTEX_TIMEDLOCK */

void
gfarm_mutex_unlock(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_unlock(mutex);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1000214, "%s: %s mutex unlock: %s",
		    where, what, strerror(err));
}

void
gfarm_mutex_destroy(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_destroy(mutex);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1001488, "%s: %s mutex destroy: %s",
		    where, what, strerror(err));
}


#ifndef __KERNEL__ /* gfarm_{cond,rwlock,ticketlock,queuelock}_* */

void
gfarm_cond_init(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_init(cond, NULL);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1000215, "%s: %s cond init: %s",
		    where, what, strerror(err));
}

void
gfarm_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
	const char *where, const char *what)
{
	int err = pthread_cond_wait(cond, mutex);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1000216, "%s: %s cond wait: %s",
		    where, what, strerror(err));
}

int
gfarm_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
	const struct timespec *ts, const char *where, const char *what)
{
	int err = pthread_cond_timedwait(cond, mutex, ts);

	if (err == ETIMEDOUT)
		return (0);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1002521, "%s: %s cond timedwait: %s",
		    where, what, strerror(err));
	return (1);
}

/*
 * pthread_cond_signal() and pthraed_cond_broadcast() may return EAGAIN
 * on MacOS X 10.7 (Lion), if retry count exceeds 8192 times.
 * http://www.opensource.apple.com/source/Libc/Libc-763.11/pthreads/pthread_cond.c
 */
void
gfarm_cond_signal(pthread_cond_t *cond, const char *where, const char *what)
{
	int err;

	while ((err = pthread_cond_signal(cond)) == EAGAIN)
		;
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000217, "%s: %s cond signal: %s",
		    where, what, strerror(err));
}

void
gfarm_cond_broadcast(pthread_cond_t *cond, const char *where, const char *what)
{
	int err;

	while ((err = pthread_cond_broadcast(cond)) == EAGAIN)
		;
	if (err != 0)
		gflog_fatal(GFARM_MSG_1002210, "%s: %s cond broadcast: %s",
		    where, what, strerror(err));
}

void
gfarm_cond_destroy(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_destroy(cond);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1001489, "%s: %s cond destroy: %s",
		    where, what, strerror(err));
}


void
gfarm_rwlock_init(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_init(rwlock, NULL);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1004742, "%s: %s rwlock init: %s",
		    where, what, strerror(err));
}

void
gfarm_rwlock_rdlock(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_rdlock(rwlock);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1004743, "%s: %s rwlock rdlock: %s",
		    where, what, strerror(err));
}

void
gfarm_rwlock_wrlock(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_wrlock(rwlock);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1004744, "%s: %s rwlock wrlock: %s",
		    where, what, strerror(err));
}

/* false: EBUSY */
int
gfarm_rwlock_trywrlock(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_trywrlock(rwlock);

	if (err != 0 && err != EBUSY)
		gflog_fatal(GFARM_MSG_1005045, "%s: %s rwlock trywrlock: %s",
		    where, what, strerror(err));
	return (err == 0);
}

void
gfarm_rwlock_unlock(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_unlock(rwlock);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1004745, "%s: %s rwlock unlock: %s",
		    where, what, strerror(err));
}

void
gfarm_rwlock_destroy(
	pthread_rwlock_t *rwlock, const char *where, const char *what)
{
	int err = pthread_rwlock_destroy(rwlock);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1004746, "%s: %s rwlock destroy: %s",
		    where, what, strerror(err));
}

/*
 * to avoid unfair scheduling about long-term holding mutex/rwlock/
 * POSIX semaphore on Linux (at least on RHEL6 / RHEL7 multicore machine)
 *
 * ticket lock implementation from:
 * https://stackoverflow.com/questions/6449732/fair-critical-section-linux
 * by caf (https://stackoverflow.com/users/134633/caf)
 * 2011-06-23 12:24
 */

void
gfarm_ticketlock_init(struct gfarm_ticketlock *tl, int cond_number,
	const char *where, const char *what)
{
	int i;

	gfarm_mutex_init(&tl->mutex, where, what);
	tl->queue_head = tl->queue_tail = 0;
	GFARM_MALLOC_ARRAY(tl->unlocked, cond_number);
	if (tl->unlocked == NULL)
		gflog_fatal(GFARM_MSG_1005050,
		    "%s: %s %d ticketlocks: no memory",
		    where, what, cond_number);
	for (i = 0; i < cond_number; i++)
		gfarm_cond_init(&tl->unlocked[i], where, what);
	tl->cond_number = cond_number;
}

int
gfarm_ticketlock_trylock(struct gfarm_ticketlock *tl,
	const char *where, const char *what)
{
	int locked;

	gfarm_mutex_lock(&tl->mutex, where, what);
	locked = tl->queue_tail == tl->queue_head;
	if (locked)
		tl->queue_tail++;
	gfarm_mutex_unlock(&tl->mutex, where, what);
	return (locked);
}

void
gfarm_ticketlock_lock(struct gfarm_ticketlock *tl,
	const char *where, const char *what)
{
	unsigned long queue_me;

	gfarm_mutex_lock(&tl->mutex, where, what);
	queue_me = tl->queue_tail++;
	while (queue_me != tl->queue_head) {
		gfarm_cond_wait(
		    &tl->unlocked[queue_me % tl->cond_number],
		    &tl->mutex, where, what);
	}
	gfarm_mutex_unlock(&tl->mutex, where, what);
}

void
gfarm_ticketlock_unlock(struct gfarm_ticketlock *tl,
	const char *where, const char *what)
{
	gfarm_mutex_lock(&tl->mutex, where, what);
	tl->queue_head++;
	gfarm_cond_broadcast(&tl->unlocked[tl->queue_head % tl->cond_number],
	    where, what);
	gfarm_mutex_unlock(&tl->mutex, where, what);
}

void
gfarm_ticketlock_destroy(struct gfarm_ticketlock *tl,
	const char *where, const char *what)
{
	int i;

	/* assertion */
	if (tl->queue_tail != tl->queue_head)
		gflog_fatal(GFARM_MSG_1005051,
		    "destroying ticketlock w/ waiters: head=%lu tail=%lu",
		     tl->queue_head, tl->queue_head);

	for (i = 0; i < tl->cond_number; i++)
		gfarm_cond_destroy(&tl->unlocked[i], where, what);
	free(tl->unlocked);
	gfarm_mutex_destroy(&tl->mutex, where, what);
}

struct gfarm_queuelock_waiter {
	struct gfarm_queuelock_waiter *next;
	pthread_cond_t unlocked;
};

void
gfarm_queuelock_init(struct gfarm_queuelock *ql,
	const char *where, const char *what)
{
	gfarm_mutex_init(&ql->mutex, where, what);
	ql->head = NULL;
	ql->tail = &ql->head;
	ql->locked = 0;
}

int
gfarm_queuelock_trylock(struct gfarm_queuelock *ql,
	const char *where, const char *what)
{
	int locked;

	gfarm_mutex_lock(&ql->mutex, where, what);
	/* unlocked AND there is no waiter */
	locked = !ql->locked && ql->head == NULL;
	if (locked)
		ql->locked = 1;
	gfarm_mutex_unlock(&ql->mutex, where, what);
	return (locked);
}

void
gfarm_queuelock_lock(struct gfarm_queuelock *ql,
	const char *where, const char *what)
{
	struct gfarm_queuelock_waiter me;

	gfarm_mutex_lock(&ql->mutex, where, what);
	if (ql->locked || ql->head != NULL) {
		gfarm_cond_init(&me.unlocked, where, what);
		me.next = NULL;
		*ql->tail = &me;
		ql->tail = &me.next;
		do {
			gfarm_cond_wait(&me.unlocked, &ql->mutex,
			    where, what);
			/* "ql->head != me" is necessary for spurious wakeup */
		} while (ql->locked || ql->head != &me);
		ql->head = me.next;
		if (ql->head == NULL)
			ql->tail = &ql->head;
		gfarm_cond_destroy(&me.unlocked, where, what);
	}
	ql->locked = 1;
	gfarm_mutex_unlock(&ql->mutex, where, what);
}

void
gfarm_queuelock_unlock(struct gfarm_queuelock *ql,
	const char *where, const char *what)
{
	gfarm_mutex_lock(&ql->mutex, where, what);
	assert(ql->locked);
	ql->locked = 0;
	if (ql->head != NULL)
		gfarm_cond_signal(
		    &ql->head->unlocked, where, what);
	gfarm_mutex_unlock(&ql->mutex, where, what);
}

void
gfarm_queuelock_destroy(struct gfarm_queuelock *ql,
	const char *where, const char *what)
{
	/* assertion */
	if (ql->locked || ql->head != NULL)
		gflog_fatal(GFARM_MSG_1005052,
		    "destroying queuelock while using: locked:%d head:%p",
		     ql->locked, ql->head);

	gfarm_mutex_destroy(&ql->mutex, where, what);
}

#endif /* __KERNEL__ */

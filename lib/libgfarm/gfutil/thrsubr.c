#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "thrsubr.h"

void
gfarm_mutex_init(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_init(mutex, NULL);

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

#ifndef __KERNEL__	/* gfarm_cond_xxx */

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
#endif /* __KERNEL__ */

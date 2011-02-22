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
		gflog_fatal(GFARM_MSG_UNFIXED, "%s: %s cond timedwait: %s",
		    where, what, strerror(err));
	return (1);
}

void
gfarm_cond_signal(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_signal(cond);

	if (err != 0)
		gflog_fatal(GFARM_MSG_1000217, "%s: %s cond signal: %s",
		    where, what, strerror(err));
}

void
gfarm_cond_broadcast(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_broadcast(cond);

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

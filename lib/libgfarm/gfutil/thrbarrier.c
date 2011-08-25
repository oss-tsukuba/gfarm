#include <pthread.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "thrbarrier.h"

#ifndef HAVE_PTHREAD_BARRIER_WAIT
/*
 * This barrier implementation is based on
 * "Programming with POSIX threads" by David R. Butenhof,
 * but somewhat simplified.
 * For example, pthread_cancel() is not supported by this implementation,
 * but that's OK because gfarm doesn't use pthread_cancel() and
 * libgfarm doesn't support pthread_cancel() at all for now.
 */

struct gfarm_thr_barrier {
	pthread_mutex_t mutex;
	pthread_cond_t all_entered;
	int n_members;
	int n_pending;
	int cycle;
};
#endif

void
gfarm_barrier_init(pthread_barrier_t *barrier, unsigned int count,
	const char *where, const char *what)
{
#ifdef HAVE_PTHREAD_BARRIER_WAIT
	int err = pthread_barrier_init(barrier, NULL, count);

	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED, "%s: %s barrier init: %s",
		    where, what, strerror(err));
#else
	int err;

	err = pthread_mutex_init(&barrier->mutex, NULL);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier init: mutex_init: %s",
		    where, what, strerror(err));
	err = pthread_cond_init(&barrier->all_entered, NULL);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier init: cond_init all_entered: %s",
		    where, what, strerror(err));
	barrier->n_members = barrier->n_pending = count;
	barrier->cycle = 0;
#endif
}

void
gfarm_barrier_destroy(pthread_barrier_t *barrier,
	const char *where, const char *what)
{
#ifdef HAVE_PTHREAD_BARRIER_WAIT
	int err = pthread_barrier_destroy(barrier);

	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED, "%s: %s barrier destroy: %s",
		    where, what, strerror(err));
#else
	int err;

	err = pthread_cond_destroy(&barrier->all_entered);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier destroy: cond_destroy all_entered: %s",
		    where, what, strerror(err));
	err = pthread_mutex_destroy(&barrier->mutex);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier destroy: mutex_destroy: %s",
		    where, what, strerror(err));
#endif
}

void
gfarm_barrier_wait(pthread_barrier_t *barrier,
	const char *where, const char *what)
{
#ifdef HAVE_PTHREAD_BARRIER_WAIT
	int err = pthread_barrier_wait(barrier);

	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED, "%s: %s barrier wait: %s",
		    where, what, strerror(err));
#else
	int err, cycle;

	err = pthread_mutex_lock(&barrier->mutex);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier wait: mutex_lock: %s",
		    where, what, strerror(err));
	cycle = barrier->cycle;
	if (--barrier->n_pending > 0) {
		/* XXX need to disable cancelstate */
		do {
			err = pthread_cond_wait(&barrier->all_entered,
			    &barrier->mutex);
			if (err != 0)
				gflog_fatal(GFARM_MSG_UNFIXED,
				    "%s: %s barrier wait: "
				    " cond_wait all_entered: %s",
				    where, what, strerror(err));
		} while (barrier->cycle == cycle);
		/* XXX need to re-enable cancelstate */
	} else { /* now all entered */
		barrier->cycle = !barrier->cycle;
		barrier->n_pending = barrier->n_members;

		/*
		 * pthread_cond_signal() and pthraed_cond_broadcast() may
		 * return EAGAIN on MacOS X 10.7 (Lion),
		 * if retry count exceeds 8192 times.
		 * http://www.opensource.apple.com/source/Libc/Libc-763.11/pthreads/pthread_cond.c
		 */
		while ((err = pthread_cond_broadcast(&barrier->all_entered)) ==
		    EAGAIN)
			;
		if (err != 0)
			gflog_fatal(GFARM_MSG_UNFIXED, "%s: %s barrier wait: "
			    "cond_broadcast all_entered: %s",
			    where, what, strerror(err));
	}
	err = pthread_mutex_unlock(&barrier->mutex);
	if (err != 0)
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: %s barrier wait:  mutex_unlock: %s",
		    where, what, strerror(err));
#endif
}

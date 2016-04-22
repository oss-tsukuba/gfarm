#include <stddef.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>

#include "nanosec.h"
#include "thrsubr.h"
#include "gfutil.h"

/*
 * Because privilege_lock() and unlock() are only called from gfmd and gfsd,
 * we don't have to have privilege_mutex in *gfarm_ctxp, at least for now.
 */
static int gfarm_privilege_lock_no_operation = 0; /* false by default */

static pthread_mutex_t gfarm_privilege_mutex =
	GFARM_MUTEX_INITIALIZER(gfarm_privilege_mutex);

static const char privilege_diag[] = "gfarm_privilege_mutex";

#ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
static pthread_mutex_t gfarm_privilege_lock_wait_mutex =
	GFARM_MUTEX_INITIALIZER(gfarm_privilege_lock_wait_mutex);
static pthread_cond_t gfarm_privilege_lockable = PTHREAD_COND_INITIALIZER;
static int gfarm_privilege_lock_waiters = 0;

static const char privilege_lock_wait_diag[] =
	"gfarm_privilege_lock_wait_mutex";
static const char privilege_lockable_diag[] = "gfarm_privilege_lockable";

/* like gfarm_timespec_cmp(), but for timespec instead of gfarm_timespec */
static int
gfarm_os_timespec_cmp(const struct timespec *t1, const struct timespec *t2)
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

#endif

void
gfarm_privilege_lock_disable(void)
{
	gfarm_privilege_lock_no_operation = 1;
}

/*
 * Lock mutex for setuid(), setegid().
 */
#define POSSIBLE_DEADLOCK_TIMEOUT	3600 /* 60 min */
void
gfarm_privilege_lock(const char *diag)
{
	struct timespec timeout;
	int locked;

	if (gfarm_privilege_lock_no_operation)
		return;

	gfarm_gettime(&timeout);
	timeout.tv_sec += POSSIBLE_DEADLOCK_TIMEOUT;
#ifdef HAVE_PTHREAD_MUTEX_TIMEDLOCK
	locked = gfarm_mutex_timedlock(&gfarm_privilege_mutex, &timeout, diag,
	    privilege_diag);
#else
	locked = gfarm_mutex_trylock(&gfarm_privilege_mutex,
	    diag, privilege_diag);

	if (!locked) {
		gfarm_mutex_lock(&gfarm_privilege_lock_wait_mutex,
		    diag, privilege_lock_wait_diag);
		++gfarm_privilege_lock_waiters;
		for (;;) {
			struct timespec now;

			locked = gfarm_mutex_trylock(&gfarm_privilege_mutex,
			    diag, privilege_diag);
			if (locked)
				break;
			gfarm_gettime(&now);
			if (gfarm_os_timespec_cmp(&now, &timeout) >= 0)
				break;
			gfarm_cond_timedwait(&gfarm_privilege_lockable,
			    &gfarm_privilege_lock_wait_mutex, &timeout,
			    diag, privilege_lockable_diag);
		}
		--gfarm_privilege_lock_waiters;
		gfarm_mutex_unlock(&gfarm_privilege_lock_wait_mutex,
		    diag, privilege_lock_wait_diag);
	}
#endif
	if (!locked) {
		/* backtrace may cause deadlock */
		gflog_set_fatal_action(GFLOG_FATAL_ACTION_ABORT);
		gflog_fatal(GFARM_MSG_1004198,
		    "%s: possible deadlock detected, die", diag);
	}
}

/*
 * Unlock mutex for setuid(), setegid().
 */
void
gfarm_privilege_unlock(const char *diag)
{
	if (gfarm_privilege_lock_no_operation)
		return;

	gfarm_mutex_unlock(&gfarm_privilege_mutex, diag, privilege_diag);

#ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
	gfarm_mutex_lock(&gfarm_privilege_lock_wait_mutex,
	    diag, privilege_lock_wait_diag);
	if (gfarm_privilege_lock_waiters > 0)
		gfarm_cond_signal(&gfarm_privilege_lockable,
		    diag, privilege_lockable_diag);
	gfarm_mutex_unlock(&gfarm_privilege_lock_wait_mutex,
	    diag, privilege_lock_wait_diag);
#endif
}

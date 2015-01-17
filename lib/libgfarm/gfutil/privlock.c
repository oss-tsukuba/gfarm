#include <stddef.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include <gfarm/gflog.h>

#include "thrsubr.h"
#include "gfutil.h"

/*
 * Because privilege_lock() and unlock() are only called from gfmd and gfsd,
 * we don't have to have privilege_mutex in *gfarm_ctxp, at least for now.
 */
static pthread_mutex_t gfarm_privilege_mutex =
	GFARM_MUTEX_INITIALIZER(gfarm_privilege_mutex);

static const char privilege_diag[] = "gfarm_privilege_mutex";

/*
 * Lock mutex for setuid(), setegid().
 */
#define POSSIBLE_DEADLOCK_TIMEOUT	3600 /* 60 min */
void
gfarm_privilege_lock(const char *diag)
{
	struct timespec ts;
	int rc;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += POSSIBLE_DEADLOCK_TIMEOUT;
	rc = gfarm_mutex_timedlock(&gfarm_privilege_mutex, &ts, diag,
	    privilege_diag);
	if (rc != 0) {
		/* backtrace may cause deadlock */
		gflog_set_fatal_action(GFLOG_FATAL_ACTION_ABORT);
		gflog_fatal(GFARM_MSG_UNFIXED,
		    "%s: possible deadlock detected, die", diag);
	}
}

/*
 * Unlock mutex for setuid(), setegid().
 */
void
gfarm_privilege_unlock(const char *diag)
{
	gfarm_mutex_unlock(&gfarm_privilege_mutex, diag, privilege_diag);
}

#include <stddef.h>
#include <unistd.h>
#include <pthread.h>

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
void
gfarm_privilege_lock(const char *diag)
{
	gfarm_mutex_lock(&gfarm_privilege_mutex, diag, privilege_diag);
}

/*
 * Unlock mutex for setuid(), setegid().
 */
void
gfarm_privilege_unlock(const char *diag)
{
	gfarm_mutex_unlock(&gfarm_privilege_mutex, diag, privilege_diag);
}

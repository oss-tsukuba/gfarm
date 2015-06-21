#include <pthread.h>

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "config.h"
#include "subr.h"

int debug_mode = 0;

static pthread_mutex_t giant_mutex;

void
giant_init(void)
{
	gfarm_mutex_init(&giant_mutex, "giant_init", "giant");
}

void
giant_lock(void)
{
	gfarm_mutex_lock(&giant_mutex, "giant_lock", "giant");
}

/* false: busy */
int
giant_trylock(void)
{
	return (gfarm_mutex_trylock(&giant_mutex, "giant_trylock", "giant"));
}

void
giant_unlock(void)
{
	gfarm_mutex_unlock(&giant_mutex, "giant_unlock", "giant");
}

static void
gfarm_pthread_attr_setstacksize(pthread_attr_t *attr)
{
	int err;

	if (gfarm_metadb_stack_size != GFARM_METADB_STACK_SIZE_DEFAULT){
#ifdef HAVE_PTHREAD_ATTR_SETSTACKSIZE
		err = pthread_attr_setstacksize(attr,
		    gfarm_metadb_stack_size);
		if (err != 0)
			gflog_warning(GFARM_MSG_1000218, "gfmd.conf: "
			    "metadb_server_stack_size %d: %s",
			    gfarm_metadb_stack_size, strerror(err));
#else
		gflog_warning(GFARM_MSG_1000219, "gfmd.conf: "
		    "metadb_server_stack_size %d: "
		    "configuration ignored due to lack of "
		    "pthread_attr_setstacksize()",
		    gfarm_metadb_stack_size);
#endif
	}
}

pthread_attr_t gfarm_pthread_attr;

void
gfarm_pthread_attr_init(void)
{
	int err;

	err = pthread_attr_init(&gfarm_pthread_attr);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000223,
		    "pthread_attr_init(): %s", strerror(err));
	err = pthread_attr_setdetachstate(&gfarm_pthread_attr,
	    PTHREAD_CREATE_DETACHED);
	if (err != 0)
		gflog_fatal(GFARM_MSG_1000224,
		    "PTHREAD_CREATE_DETACHED: %s", strerror(err));
	gfarm_pthread_attr_setstacksize(&gfarm_pthread_attr);
}


pthread_attr_t *
gfarm_pthread_attr_get(void)
{
	static pthread_once_t gfarm_pthread_attr_initialized =
	    PTHREAD_ONCE_INIT;

	pthread_once(&gfarm_pthread_attr_initialized,
	    gfarm_pthread_attr_init);

	return (&gfarm_pthread_attr);
}

gfarm_error_t
create_detached_thread(void *(*thread_main)(void *), void *arg)
{
	int err;
	pthread_t thread_id;

	err = pthread_create(&thread_id, gfarm_pthread_attr_get(),
	    thread_main, arg);
	return (err == 0 ? GFARM_ERR_NO_ERROR : gfarm_errno_to_error(err));
}

gfarm_error_t
gfarm_pthread_set_priority_minimum(void)
{
	pthread_t self = pthread_self();
	int policy, min_prio;
	struct sched_param param;
	int save_errno;

	save_errno = pthread_getschedparam(self, &policy, &param);
	if (save_errno != 0) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "pthread_get_schedparam(): %s", strerror(errno));
		return (gfarm_errno_to_error(save_errno));
	}

	min_prio = sched_get_priority_min(policy);
	if (min_prio == -1) {
		save_errno = errno;
		gflog_warning(GFARM_MSG_UNFIXED,
		    "sched_get_priority_min(%d): %s", policy, strerror(errno));
		return (gfarm_errno_to_error(save_errno));
	}

	param.sched_priority = min_prio;
	save_errno = pthread_setschedparam(self, policy, &param);
	if (save_errno != 0) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "pthread_set_schedparam(%d, %d): %s",
		    policy, min_prio, strerror(errno));
		return (gfarm_errno_to_error(save_errno));
	}

	return (GFARM_ERR_NO_ERROR);
}

/* only initialization routines are allowed to call this function */
char *
strdup_ck(const char *s, const char *diag)
{
	char *d = strdup(s);

	if (d == NULL)
		gflog_fatal(GFARM_MSG_1002313,
		    "%s: strdup(%s): no memory", diag, s);
	return (d);
}

char *
strdup_log(const char *s, const char *diag)
{
	char *d = strdup(s);

	if (d == NULL)
		gflog_error(GFARM_MSG_1002358,
		    "%s: strdup(%s): no memory", diag, s);
	return (d);
}

int
accmode_to_op(gfarm_uint32_t flag)
{
	int op;

	switch (flag & GFARM_FILE_ACCMODE) {
	case GFARM_FILE_RDONLY:
		op = (flag & GFARM_FILE_TRUNC) ? (GFS_R_OK|GFS_W_OK) :
		    GFS_R_OK;
		break;
	case GFARM_FILE_WRONLY:	op = GFS_W_OK; break;
	case GFARM_FILE_RDWR:	op = GFS_R_OK|GFS_W_OK; break;
	case GFARM_FILE_LOOKUP:	op = 0; break;
	default:
		assert(0);
		op = 0;
	}
	return (op);
}

/* giant_lock should be held before calling this */
gfarm_uint64_t
trace_log_get_sequence_number(void)
{
	static gfarm_uint64_t trace_log_seq_num;

	return (trace_log_seq_num++);
}

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "config.h"

#include "thrsubr.h"

void
mutex_init(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_init(mutex, NULL);

	if (err != 0)
		gflog_fatal("%s: %s mutex init: %s",
		    where, what, strerror(err));
}

void
mutex_lock(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_lock(mutex);

	if (err != 0)
		gflog_fatal("%s: %s mutex lock: %s",
		    where, what, strerror(err));
}

void
mutex_unlock(pthread_mutex_t *mutex, const char *where, const char *what)
{
	int err = pthread_mutex_unlock(mutex);

	if (err != 0)
		gflog_fatal("%s: %s mutex unlock: %s",
		    where, what, strerror(err));
}

void
cond_init(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_init(cond, NULL);

	if (err != 0)
		gflog_fatal("%s: %s cond init: %s",
		    where, what, strerror(err));
}

void
cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
	const char *where, const char *what)
{
	int err = pthread_cond_wait(cond, mutex);

	if (err != 0)
		gflog_fatal("%s: %s cond wait: %s",
		    where, what, strerror(err));
}

void
cond_signal(pthread_cond_t *cond, const char *where, const char *what)
{
	int err = pthread_cond_signal(cond);

	if (err != 0)
		gflog_fatal("%s: %s cond signal: %s",
		    where, what, strerror(err));
}

void
gfarm_pthread_attr_setstacksize(pthread_attr_t *attr)
{
	int err;

	if (gfarm_metadb_stack_size != GFARM_METADB_STACK_SIZE_DEFAULT){
#ifdef HAVE_PTHREAD_ATTR_SETSTACKSIZE
		err = pthread_attr_setstacksize(attr,
		    gfarm_metadb_stack_size);
		if (err != 0)
			gflog_warning("gfmd.conf: "
			    "metadb_server_stack_size %d: %s",
			    gfarm_metadb_stack_size, strerror(err));
#else
		gflog_warning("gfmd.conf: "
		    "metadb_server_stack_size %d: "
		    "configuration ignored due to lack of "
		    "pthread_attr_setstacksize()",
		    gfarm_metadb_stack_size);
#endif
	}
}

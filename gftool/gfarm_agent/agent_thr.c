/*
 * $Id$
 */

#include <string.h>
#include <pthread.h>
#include "gfutil.h"
#include "agent_thr.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void
agent_lock()
{
	pthread_mutex_lock(&lock);
}

void
agent_unlock()
{
	pthread_mutex_unlock(&lock);
}

int
agent_schedule(int *fd, void *(*handler)(void *))
{
	int err;
	pthread_t thread_id;
	static int initialized = 0;
	static pthread_attr_t attr;

	if (!initialized) {
		err = pthread_attr_init(&attr);
		if (err != 0)
			gflog_fatal("pthread_attr_init()", strerror(err));
		err = pthread_attr_setdetachstate(&attr,
		    PTHREAD_CREATE_DETACHED);
		if (err != 0)
			gflog_fatal("PTHREAD_CREATE_DETACHED", strerror(err));
		initialized = 1;
	}

	return (pthread_create(&thread_id, &attr, handler, fd));
}

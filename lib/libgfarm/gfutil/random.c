#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "thrsubr.h"

static void
gfarm_random_initialize(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
#ifdef HAVE_RANDOM
	srandom(t.tv_sec + t.tv_usec + getpid());
#else
	srand(t.tv_sec + t.tv_usec + getpid());
#endif
}

long gfarm_random(void)
{
	long rv;
	static pthread_mutex_t mutex = GFARM_MUTEX_INITIALIZER(mutex);
	static pthread_once_t rand_initialized = PTHREAD_ONCE_INIT;
	static const char diag[] = "gfarm_random";

	pthread_once(&rand_initialized, gfarm_random_initialize);
	gfarm_mutex_lock(&mutex, diag, "");
#ifdef HAVE_RANDOM
	rv = random();
#else
	rv = rand();
#endif
	gfarm_mutex_unlock(&mutex, diag, "");
	return (rv);
}

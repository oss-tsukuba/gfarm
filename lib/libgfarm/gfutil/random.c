#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

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
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_once_t rand_initialized = PTHREAD_ONCE_INIT;

	pthread_once(&rand_initialized, gfarm_random_initialize);
	pthread_mutex_lock(&mutex);
#ifdef HAVE_RANDOM
	rv = random();
#else
	rv = rand();
#endif
	pthread_mutex_unlock(&mutex);
	return (rv);
}

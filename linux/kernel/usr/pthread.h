#ifndef _PTHREAD_H_
#define _PTHREAD_H_
#include <linux/mutex.h>

typedef struct mutex pthread_mutex_t;
typedef void *pthread_mutexattr_t;

struct gfarm_cond_t;
typedef struct gfarm_cond_t pthread_cond_t;

typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0
extern void pthread_once(pthread_once_t *varp, void (*func)(void));

static inline int pthread_mutex_init(pthread_mutex_t *mutex,
	const  pthread_mutexattr_t *mutexattr)
{
	mutex_init(mutex);
	return (0);
}
static inline int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	mutex_lock(mutex);
	return (0);
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	mutex_trylock(mutex);
	return (0);
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	mutex_unlock(mutex);
	return (0);
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	mutex_destroy(mutex);
	return (0);
}

#endif /* _PTHREAD_H_ */


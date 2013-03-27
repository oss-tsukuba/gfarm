#ifndef _PTHREAD_H_
#define _PTHREAD_H_
#include <linux/mutex.h>
#include <linux/sched.h>

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

typedef pid_t pthread_t;
#define pthread_self()	(current->pid)

struct gfarm_rmutex {
	struct mutex	r_lock;
	const char		*r_name;
	pthread_t	r_owner;
	int		r_locked;
};
typedef struct gfarm_rmutex gfarm_rmutex_t;


static inline int gfarm_rmutex_init(gfarm_rmutex_t *mutex, const char *name)
{
	mutex_init(&mutex->r_lock);
	mutex->r_name = name;
	mutex->r_owner = 0;
	mutex->r_locked = 0;
	return (0);
}
static inline int gfarm_rmutex_lock(gfarm_rmutex_t *mutex)
{
	if (mutex->r_owner != pthread_self()) {
		if (mutex->r_owner) {
			pr_debug("gfarm_rmutex_lock:locked:%p by %x",
					mutex, mutex->r_owner);
		}
		mutex_lock(&mutex->r_lock);
		mutex->r_owner = pthread_self();
	}
	mutex->r_locked++;
	return (0);
}

static inline int gfarm_rmutex_unlock(gfarm_rmutex_t *mutex)
{
	if (mutex->r_owner != pthread_self())
		return (-EPERM);
	else if (mutex->r_locked < 1)
		return (-EIO);
	else if (mutex->r_locked-- == 1) {
		mutex->r_owner = 0;
		mutex_unlock(&mutex->r_lock);
	}
	return (0);
}

static inline int gfarm_rmutex_destroy(gfarm_rmutex_t *mutex)
{
	mutex_destroy(&mutex->r_lock);
	return (0);
}

#endif /* _PTHREAD_H_ */


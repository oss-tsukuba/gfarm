struct timespec;

void gfarm_mutex_init(pthread_mutex_t *, const char *, const char *);
void gfarm_mutex_lock(pthread_mutex_t *, const char *, const char *);
int gfarm_mutex_trylock(pthread_mutex_t *, const char *, const char *);
void gfarm_mutex_unlock(pthread_mutex_t *, const char *, const char *);
void gfarm_mutex_destroy(pthread_mutex_t *, const char *, const char *);
void gfarm_cond_init(pthread_cond_t *, const char *, const char *);
void gfarm_cond_wait(pthread_cond_t *, pthread_mutex_t *,
	const char *, const char *);
int gfarm_cond_timedwait(pthread_cond_t *, pthread_mutex_t *,
	const struct timespec *, const char *, const char *);
void gfarm_cond_signal(pthread_cond_t *, const char *, const char *);
void gfarm_cond_broadcast(pthread_cond_t *, const char *, const char *);
void gfarm_cond_destroy(pthread_cond_t *, const char *, const char *);

#ifdef __KERNEL__
#define GFARM_MUTEX_INITIALIZER(name)   __MUTEX_INITIALIZER(name)
#else /* __KERNEL__ */
#define GFARM_MUTEX_INITIALIZER(name)   PTHREAD_MUTEX_INITIALIZER
#define GFARM_COND_INITIALIZER(name)   PTHREAD_COND_INITIALIZER
#endif /* __KERNEL__ */

#define GFARM_DEFINE_MUTEX(name) \
		gfarm_mutex_t name = GFARM_MUTEX_INITIALIZER(name)
#define GFARM_DEFINE_CONDVAR(name) \
		 gfarm_condvar_t name = GFARM_CONDVAR_INITIALIZER(name)

struct timespec;

void gfarm_mutex_init(pthread_mutex_t *, const char *, const char *);
void gfarm_mutex_lock(pthread_mutex_t *, const char *, const char *);
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

#ifndef HAVE_PTHREAD_BARRIER_WAIT
struct gfarm_thr_barrier;
typedef struct gfarm_thr_barrier pthread_barrier_t;
#endif
void gfarm_barrier_init(pthread_barrier_t *, unsigned int,
	const char *, const char *);
void gfarm_barrier_destroy(pthread_barrier_t *, const char *, const char *);
void gfarm_barrier_wait(pthread_barrier_t *, const char *, const char *);

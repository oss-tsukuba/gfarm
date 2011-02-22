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

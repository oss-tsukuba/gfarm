void mutex_init(pthread_mutex_t *, const char *, const char *);
void mutex_lock(pthread_mutex_t *, const char *, const char *);
void mutex_unlock(pthread_mutex_t *, const char *, const char *);
void mutex_destroy(pthread_mutex_t *, const char *, const char *);
void cond_init(pthread_cond_t *, const char *, const char *);
void cond_wait(pthread_cond_t *, pthread_mutex_t *, const char *, const char *);
void cond_signal(pthread_cond_t *, const char *, const char *);
void cond_broadcast(pthread_cond_t *, const char *, const char *);
void cond_destroy(pthread_cond_t *, const char *, const char *);

void gfarm_pthread_attr_setstacksize(pthread_attr_t *attr);

struct timespec;

void gfarm_mutex_init(pthread_mutex_t *, const char *, const char *);
void gfarm_mutex_lock(pthread_mutex_t *, const char *, const char *);
int gfarm_mutex_trylock(pthread_mutex_t *, const char *, const char *);
int gfarm_mutex_timedlock(pthread_mutex_t *, const struct timespec *,
	const char *, const char *);
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

void gfarm_rwlock_init(pthread_rwlock_t *, const char *, const char *);
void gfarm_rwlock_rdlock(pthread_rwlock_t *, const char *, const char *);
void gfarm_rwlock_wrlock(pthread_rwlock_t *, const char *, const char *);
int gfarm_rwlock_trywrlock(pthread_rwlock_t *, const char *, const char *);
void gfarm_rwlock_unlock(pthread_rwlock_t *, const char *, const char *);
void gfarm_rwlock_destroy(pthread_rwlock_t *, const char *, const char *);

struct gfarm_ticketlock {
	pthread_mutex_t mutex;
	unsigned long queue_head, queue_tail;
	pthread_cond_t *unlocked;
	int cond_number;
};
void gfarm_ticketlock_init(struct gfarm_ticketlock *, int,
	const char *, const char *);
void gfarm_ticketlock_lock(struct gfarm_ticketlock *,
	const char *, const char *);
int gfarm_ticketlock_trylock(struct gfarm_ticketlock *,
	const char *, const char *);
void gfarm_ticketlock_unlock(struct gfarm_ticketlock *,
	const char *, const char *);
void gfarm_ticketlock_destroy(struct gfarm_ticketlock *,
	const char *, const char *);

struct gfarm_queuelock_waiter;
struct gfarm_queuelock {
	pthread_mutex_t mutex;
	struct gfarm_queuelock_waiter *head, **tail;
	int locked;
};
void gfarm_queuelock_init(struct gfarm_queuelock *,
	const char *, const char *);
void gfarm_queuelock_lock(struct gfarm_queuelock *,
	const char *, const char *);
int gfarm_queuelock_trylock(struct gfarm_queuelock *,
	const char *, const char *);
void gfarm_queuelock_unlock(struct gfarm_queuelock *,
	const char *, const char *);
void gfarm_queuelock_destroy(struct gfarm_queuelock *,
	const char *, const char *);

#ifdef __KERNEL__	/* PTHREAD_MUTEX_INITIALIZER */
#define GFARM_MUTEX_INITIALIZER(name)   __MUTEX_INITIALIZER(name)
#else /* __KERNEL__ */
#define GFARM_MUTEX_INITIALIZER(name)   PTHREAD_MUTEX_INITIALIZER
#endif /* __KERNEL__ */

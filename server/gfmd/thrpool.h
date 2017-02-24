struct thread_pool;
struct thread_pool *thrpool_new(int, int, const char *);
void thrpool_add_job(struct thread_pool *, void *(*)(void *), void *);
void thrpool_add_job_low_priority(struct thread_pool *,
	void *(*)(void *), void *);
void thrpool_set_jobq_low_priority_limit(struct thread_pool *, int);

void thrpool_info(void);

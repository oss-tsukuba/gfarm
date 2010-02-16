struct thread_pool;
struct thread_pool *thrpool_new(int, int, const char *);
void thrpool_add_job(struct thread_pool *, void *(*)(void *), void *);

void thrpool_info(void);

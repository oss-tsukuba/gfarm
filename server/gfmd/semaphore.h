struct semaphore {
	pthread_mutex_t mutex;
	pthread_cond_t posted;
	int count;
};

void semaphore_init(struct semaphore *, int);
void semaphore_post(struct semaphore *);
void semaphore_wait(struct semaphore *);
void semaphore_destroy(struct semaphore *);

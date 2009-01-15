void thrpool_init(void);
void thrpool_add_job(void *(*)(void *), void *);
void thrpool_info(void);

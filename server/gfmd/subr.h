extern int debug_mode;

void giant_init(void);
void giant_lock(void);
int giant_trylock(void);
void giant_unlock(void);

gfarm_error_t create_detached_thread(void *(*)(void *), void *);

char *strdup_ck(const char *, const char *);
char *strdup_log(const char *, const char *);

int accmode_to_op(gfarm_uint32_t);

gfarm_uint64_t trace_log_get_sequence_number(void);

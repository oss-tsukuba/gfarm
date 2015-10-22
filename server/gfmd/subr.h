extern int debug_mode;

void giant_init(void);
void giant_lock(void);
int giant_trylock(void);
void giant_unlock(void);

gfarm_error_t create_detached_thread(void *(*)(void *), void *);
gfarm_error_t gfarm_pthread_set_priority_minimum(const char *);

char *strdup_ck(const char *, const char *);
char *strdup_log(const char *, const char *);

int accmode_to_op(gfarm_uint32_t);
const char *accmode_to_string(gfarm_uint32_t);

gfarm_uint64_t trace_log_get_sequence_number(void);

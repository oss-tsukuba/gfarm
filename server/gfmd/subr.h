extern int debug_mode;

void giant_init(void);
void giant_type_log(void);
void giant_lock(void);
int giant_trylock(void);
void giant_unlock(void);

void config_var_init(void);
void config_var_lock(void);
void config_var_unlock(void);
int gfarm_read_only_mode(void);
void gfarm_read_only_disabled_wait(const char *);
void gfarm_read_only_disabled_broadcast(const char *);

gfarm_error_t create_detached_thread(void *(*)(void *), void *);
gfarm_error_t gfarm_pthread_set_priority_minimum(const char *);

char *strdup_ck(const char *, const char *);
char *strdup_log(const char *, const char *);
char *alloc_name_with_tenant(const char *, const char *, const char *);
char *alloc_name_without_tenant(const char *, const char *);

int accmode_to_op(gfarm_uint32_t);
const char *accmode_to_string(gfarm_uint32_t);

gfarm_uint64_t trace_log_get_sequence_number(void);

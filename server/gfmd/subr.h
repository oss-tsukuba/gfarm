extern int debug_mode;

void giant_init(void);
void giant_lock(void);
void giant_unlock(void);

gfarm_error_t create_detached_thread(void *(*)(void *), void *);

int accmode_to_op(gfarm_uint32_t);

struct peer;
gfarm_error_t gfm_server_get_request(struct peer *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_reply(struct peer *, const char *,
	gfarm_error_t, const char *, ...);

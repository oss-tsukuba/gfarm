void giant_init(void);
void giant_lock(void);
void giant_unlock(void);

struct peer;
gfarm_error_t gfm_server_get_request(struct peer *, char *, const char *, ...);
gfarm_error_t gfm_server_put_reply(struct peer *, char *,
	int, const char *, ...);

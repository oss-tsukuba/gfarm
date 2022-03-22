struct peer;
struct process;
gfarm_error_t gfm_server_get_request(struct peer *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_reply(struct peer *, const char *,
	gfarm_error_t, const char *, ...);

gfarm_error_t rpc_name_with_tenant(struct peer *, int,
	int *, struct process **, const char *);

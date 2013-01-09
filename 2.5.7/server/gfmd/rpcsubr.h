struct peer;
gfarm_error_t gfm_server_get_request(struct peer *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_reply(struct peer *, const char *,
	gfarm_error_t, const char *, ...);


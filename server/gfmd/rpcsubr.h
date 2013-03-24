struct peer;
gfarm_error_t gfm_server_get_request(struct peer *, size_t *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_reply(struct peer *, gfp_xdr_xid_t, size_t *,
	const char *, gfarm_error_t, const char *, ...);
gfarm_error_t gfm_server_put_reply_begin(struct peer *, struct peer **,
	gfp_xdr_xid_t, int *, const char *, gfarm_error_t, const char *, ...);
gfarm_error_t gfm_server_put_reply_end(struct peer *, struct peer *,
	const char *, int);

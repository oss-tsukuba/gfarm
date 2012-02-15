struct peer;
gfarm_error_t gfm_server_get_vrequest(struct peer *, size_t *, const char *,
	const char *, va_list *);
gfarm_error_t gfm_server_get_request(struct peer *, size_t *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_vreply(struct peer *, gfp_xdr_xid_t, size_t *,
	gfarm_error_t (*xdr_vsend)(struct gfp_xdr *, const char **, va_list *),
	const char *, gfarm_error_t, const char *, va_list *);
gfarm_error_t gfm_server_put_reply(struct peer *, gfp_xdr_xid_t, size_t *,
	const char *, gfarm_error_t, const char *, ...);


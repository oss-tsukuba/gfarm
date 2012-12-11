struct peer;
void gfm_server_start_get_request(struct peer *, const char *);
gfarm_error_t gfm_server_vrecv(struct peer *, size_t *, const char *,
	const char *, va_list *);
gfarm_error_t gfm_server_get_vrequest(struct peer *, size_t *, const char *,
	const char *, va_list *);
gfarm_error_t gfm_server_get_request(struct peer *, size_t *, const char *,
	const char *, ...);
gfarm_error_t gfm_server_put_vreply(struct peer *, gfp_xdr_xid_t, size_t *,
	xdr_vsend_t, const char *, gfarm_error_t, const char *, va_list *);
gfarm_error_t gfm_server_put_wrapped_vreply(struct peer *, gfp_xdr_xid_t,
	size_t *, xdr_vsend_t, const char *, gfarm_error_t, const char *,
	va_list *, const char *, va_list *);
gfarm_error_t gfm_server_put_reply(struct peer *, gfp_xdr_xid_t, size_t *,
	const char *, gfarm_error_t, const char *, ...);

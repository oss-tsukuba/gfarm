struct relayed_request;

gfarm_error_t relay_put_request(struct relayed_request **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t relay_get_reply(struct relayed_request *, const char *,
	gfarm_error_t *, const char *, ...);

struct peer;
gfarm_error_t gfm_server_get_request_with_relay(struct peer *, size_t *,
	int, struct relayed_request **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t gfm_server_put_reply_with_relay(
	struct peer *, gfp_xdr_xid_t, size_t *,
	struct relayed_request *, const char *,
	gfarm_error_t *, const char *, ...);

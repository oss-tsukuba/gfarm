struct peer;
struct relayed_request;

enum request_reply_mode {
	RELAY_NOT_SET,
	NO_RELAY,
	RELAY_CALC_SIZE,
	RELAY_TRANSFER,
};

typedef gfarm_error_t (*get_request_op_t)(enum request_reply_mode,
	struct peer *, size_t *, int, struct relayed_request *, void *,
	const char *);
typedef gfarm_error_t (*put_reply_op_t)(enum request_reply_mode,
	struct peer *, size_t *, int, void *, const char *);

gfarm_error_t gfm_server_get_request_with_relay(struct peer *, size_t *,
	int, struct relayed_request **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t gfm_server_put_reply_with_relay(
	struct peer *, gfp_xdr_xid_t, size_t *,
	struct relayed_request *, const char *,
	gfarm_error_t *, const char *, ...);
gfarm_error_t gfm_server_get_request_with_vrelay(struct peer *, size_t *,
	int, struct relayed_request *, const char *, const char *format, ...);
gfarm_error_t gfm_server_put_reply_with_vrelay(struct peer *, size_t *,
	const char *, const char *format, ...);
gfarm_error_t gfm_server_request_reply_with_vrelay(struct peer *, gfp_xdr_xid_t,
	int, get_request_op_t, put_reply_op_t, gfarm_int32_t, void *,
	const char *);
int request_reply_giant_lock(enum request_reply_mode);
int request_reply_giant_unlock(enum request_reply_mode, gfarm_error_t);

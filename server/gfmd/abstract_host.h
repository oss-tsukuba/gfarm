struct abstract_host;
struct host;
struct mdhost;
struct peer;
struct netsendq;

#ifdef COMPAT_GFARM_2_3
typedef void (*host_set_callback_t)(struct abstract_host *, struct peer *,
    result_callback_t, disconnect_callback_t, void *);
#endif

struct host *abstract_host_to_host(struct abstract_host *);
struct mdhost *abstract_host_to_mdhost(struct abstract_host *);

int abstract_host_get_protocol_version(struct abstract_host *);
void abstract_host_invalidate(struct abstract_host *);
void abstract_host_validate(struct abstract_host *);
int abstract_host_is_invalid_unlocked(struct abstract_host *);
int abstract_host_is_valid_unlocked(struct abstract_host *);
int abstract_host_is_valid(struct abstract_host *, const char *);
void abstract_host_activate(struct abstract_host *, const char *);
int abstract_host_is_up_unlocked(struct abstract_host *);
int abstract_host_is_up(struct abstract_host *);
const char *abstract_host_get_name(struct abstract_host *);
int abstract_host_get_port(struct abstract_host *);
struct peer *abstract_host_get_peer(struct abstract_host *, const char *);
struct peer *abstract_host_get_peer_with_id(struct abstract_host *,
	gfarm_int64_t, const char *);
void abstract_host_put_peer(struct abstract_host *, struct peer *);
void abstract_host_set_peer(struct abstract_host *, struct peer *, int);
struct netsendq *abstract_host_get_sendq(struct abstract_host *);
void abstract_host_disconnect_request(struct abstract_host *, struct peer *,
	const char *);

typedef gfarm_error_t (*channel_protocol_switch_t)(struct abstract_host *,
	struct peer *, int, gfp_xdr_xid_t, size_t, int *);

struct local_peer;
gfarm_error_t async_channel_protocol_switch(struct abstract_host *,
	struct peer *, gfp_xdr_xid_t, size_t, channel_protocol_switch_t);

void *async_server_main(struct local_peer *,
	channel_protocol_switch_t
#ifdef COMPAT_GFARM_2_3
	,void (*)(struct abstract_host *),
	gfarm_error_t (*)(struct abstract_host *, struct peer *)
#endif
	);
void async_server_disconnect_request(struct abstract_host *,
	struct peer *, const char *, const char *, const char *);
void async_server_already_disconnected_message(struct abstract_host *,
	const char *, const char *, const char *);
gfarm_error_t async_server_vget_request(struct peer *, size_t,
	const char *, const char *, va_list *);
gfarm_error_t async_server_vput_reply(struct abstract_host *,
	struct peer *, gfp_xdr_xid_t,
	const char *, gfarm_error_t, const char *, va_list *);
gfarm_error_t async_server_put_reply(struct abstract_host *,
	struct peer *, gfp_xdr_xid_t,
	const char *, gfarm_error_t, const char *, ...);
gfarm_error_t async_server_vput_wrapped_reply_unlocked(struct abstract_host *,
	gfp_xdr_xid_t, int, const char *,
	gfarm_error_t, const char *, va_list *,
	gfarm_error_t, const char *, va_list *);
gfarm_error_t async_server_vput_wrapped_reply(struct abstract_host *,
	struct peer *, gfp_xdr_xid_t, int, const char *,
	gfarm_error_t, const char *, va_list *,
	gfarm_error_t, const char *, va_list *);

gfarm_error_t async_client_vsend_wrapped_request_unlocked(
	struct abstract_host *,
	const char *, result_callback_t, disconnect_callback_t,
	void *,
#ifdef COMPAT_GFARM_2_3
	host_set_callback_t,
#endif
	const char *, va_list *,
	gfarm_int32_t, const char *, va_list *, int);
gfarm_error_t async_client_vsend_wrapped_request(struct abstract_host *,
	struct peer *, const char *, result_callback_t, disconnect_callback_t,
	void *,
#ifdef COMPAT_GFARM_2_3
	host_set_callback_t,
#endif
	const char *, va_list *,
	gfarm_int32_t, const char *, va_list *, int);
gfarm_error_t async_client_vsend_request(struct abstract_host *,
	struct peer *, const char *, result_callback_t, disconnect_callback_t,
	void *,
#ifdef COMPAT_GFARM_2_3
	host_set_callback_t,
#endif
	gfarm_int32_t, const char *, va_list *);
gfarm_error_t async_client_send_raw_request(struct abstract_host *,
	struct peer *, const char *, result_callback_t,
	disconnect_callback_t, void *, size_t, void *);
gfarm_error_t async_client_vrecv_result(struct peer *,
	struct abstract_host *, size_t, const char *, const char **,
	gfarm_error_t *, va_list *);
gfarm_error_t async_client_vrecv_wrapped_result(struct peer *,
	struct abstract_host *, size_t, const char *,
	gfarm_error_t *, const char *, va_list *,
	gfarm_error_t *, const char **, va_list *);
gfarm_error_t abstract_host_sender_lock(struct abstract_host *,
    struct peer *, struct peer **, const char *);
void abstract_host_sender_unlock(struct abstract_host *, struct peer *,
	const char *);

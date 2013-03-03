struct peer;
struct relayed_request;

enum request_reply_mode {
	RELAY_NOT_SET,
	NO_RELAY,
	RELAY_CALC_SIZE,
	RELAY_TRANSFER,
};

/* db_update_info flags */
/* XXXRELAY define flags for meta data update here */
#define DBUPDATE_USER		((gfarm_uint64_t)1 <<  0)
#define DBUPDATE_GROUP		((gfarm_uint64_t)1 <<  1)
#define DBUPDATE_HOST		((gfarm_uint64_t)1 <<  2)
#define DBUPDATE_XMLATTR	((gfarm_uint64_t)1 <<  3)
#define DBUPDATE_FS_DIRENT	((gfarm_uint64_t)1 << 32)
#define DBUPDATE_FS_STAT	((gfarm_uint64_t)1 << 33)
#define DBUPDATE_FS		(DBUPDATE_FS_DIRENT | DBUPDATE_FS_STAT)
#define DBUPDATE_NOWAIT		((gfarm_uint64_t)1 << 63)
				/* NOWAIT is not transfered in protocol */
#define DBUPDATE_PROTOCOL_MASK	(GFARM_UINT64_MAX & ~DBUPDATE_NOWAIT)
#define DBUPDATE_ALL		DBUPDATE_PROTOCOL_MASK

typedef gfarm_error_t (*get_request_op_t)(enum request_reply_mode,
	struct peer *, size_t *, int, struct relayed_request *, void *,
	const char *);
typedef gfarm_error_t (*put_reply_op_t)(enum request_reply_mode,
	struct peer *, size_t *, int, void *, const char *);

void relay_init(void);
gfarm_error_t gfm_server_relay_put_request(struct peer *,
	struct relayed_request **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t gfm_server_relay_get_request(struct peer *, size_t *,
	int, struct relayed_request **, const char *,
	gfarm_int32_t, const char *, ...);
gfarm_error_t gfm_server_relay_get_reply(struct relayed_request *,
	const char *, const char *, ...);
gfarm_error_t gfm_server_relay_put_reply(
	struct peer *, gfp_xdr_xid_t, size_t *,
	struct relayed_request *, const char *,
	gfarm_error_t *, const char *, ...);
gfarm_error_t gfm_server_relay_get_request_dynarg(struct peer *, size_t *,
	int, struct relayed_request *, const char *, const char *format, ...);
gfarm_error_t gfm_server_relay_put_reply_dynarg(struct peer *, size_t *,
    const char *, gfarm_error_t, const char *, ...);
gfarm_error_t gfm_server_relay_put_reply_arg_dynarg(struct peer *, size_t *,
    const char *, const char *, ...);
gfarm_error_t gfm_server_relay_request_reply(struct peer *, gfp_xdr_xid_t,
	int, get_request_op_t, put_reply_op_t, gfarm_int32_t, void *,
	const char *);
void master_set_db_update_info_to_peer(struct peer *, gfarm_uint64_t);
gfarm_error_t slave_add_initial_db_update_info(gfarm_uint64_t, const char *);
void slave_clear_db_update_info(void);
gfarm_error_t wait_db_update_info(struct peer *, gfarm_uint64_t, const char *);

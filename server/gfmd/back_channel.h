/*
 * $Id$
 */

struct peer;

gfarm_error_t gfm_async_server_get_request(struct peer *, size_t,
	const char *, const char *, ...);
gfarm_error_t gfm_async_server_put_reply(struct host *, struct peer *,
	gfp_xdr_xid_t, const char *, gfarm_error_t, char *, ...);

gfarm_error_t gfs_client_send_request(struct host *,
	struct peer *, const char *, gfarm_int32_t (*)(void *, void *, size_t),
	void (*)(void *, void *), void *, gfarm_int32_t, const char *, ...);
gfarm_error_t gfs_client_recv_result_and_error(struct peer *, struct host *,
	size_t, gfarm_error_t *, const char *, const char *, ...);
gfarm_error_t gfs_client_recv_result(struct peer *, struct host *,
       size_t, const char *, const char *, ...);

gfarm_error_t async_back_channel_protocol_switch(struct abstract_host *,
	struct peer *, int, gfp_xdr_xid_t, size_t, int *);

struct file_replication;
gfarm_error_t async_back_channel_replication_request(char *, int,
	struct host *, gfarm_ino_t, gfarm_int64_t, struct file_replication *);

gfarm_error_t gfm_server_switch_back_channel(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_switch_async_back_channel(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfs_client_relay(struct abstract_host *, struct peer *, size_t,
	void *, void *,
	gfarm_int32_t (*)(gfarm_error_t, void *, size_t, void *),
	void (*)(gfarm_error_t, void *));

struct netsendq_type;
extern struct netsendq_type gfs_proto_status_queue;
extern struct netsendq_type gfm_async_server_reply_to_gfsd_queue;


void back_channel_init(void);

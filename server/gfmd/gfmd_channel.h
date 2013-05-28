/*
 * $Id$
 */

struct peer;
struct mdhost;
struct gfmdc_peer_record;
struct abstract_host;

void gfmdc_peer_record_free(struct gfmdc_peer_record *, const char *);
gfarm_error_t gfm_server_switch_gfmd_channel(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
void gfmdc_init(void);
void gfmdc_pre_init(void);
void *gfmdc_journal_asyncsend_thread(void *);
void *gfmdc_connect_thread(void *);
gfarm_error_t gfmdc_client_remote_peer_alloc(struct peer *);
gfarm_error_t gfmdc_client_remote_peer_free(gfarm_uint64_t);
gfarm_error_t gfmdc_ensure_remote_peer(struct peer *, struct peer **);
void gfmdc_ensure_remote_peer_end(struct peer *);
gfarm_error_t gfmdc_client_remote_peer_disconnect(struct mdhost *,
    struct peer *, gfarm_int64_t);

gfarm_error_t gfmdc_slave_client_remote_gfs_rpc(struct peer *, void *,
	gfarm_error_t (*)(gfarm_error_t, void *, size_t, void *),
	void (*)(gfarm_error_t, void *), int, size_t, void *);
gfarm_error_t gfmdc_master_client_remote_gfs_rpc(struct abstract_host *,
	struct peer *, const char *, gfarm_int32_t (*)(void *, void *, size_t),
	void (*)(void *, void *), void *, gfarm_int32_t, const char *,
	va_list *);
gfarm_error_t gfmdc_server_vput_remote_gfs_rpc_reply(struct abstract_host *,
	struct peer *, gfp_xdr_xid_t, const char *, gfarm_error_t,
	char *, va_list *);

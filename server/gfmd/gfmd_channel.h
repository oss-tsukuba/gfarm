/*
 * $Id$
 */

struct peer;
struct gfmdc_peer_record;

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

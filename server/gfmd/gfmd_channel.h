/*
 * $Id$
 */

struct peer;
struct gfmdc_peer_record;

void gfmdc_peer_record_free(struct gfmdc_peer_record *, const char *);
gfarm_error_t gfm_server_switch_gfmd_channel(struct peer *, int, int);
void gfmdc_init(void);
void gfmdc_pre_init(void);
int gfmdc_is_master_gfmd_running(void);
void *gfmdc_journal_asyncsend_thread(void *);
void *gfmdc_connect_thread(void *);
void gfmdc_journal_transfer_wait(void);

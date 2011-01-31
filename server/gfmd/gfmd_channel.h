/*
 * $Id$
 */

struct peer;
struct mdhost;

gfarm_error_t gfm_server_switch_gfmd_channel(struct peer *, int, int);
void gfmdc_init(void);
void gfmdc_thread(void);

gfarm_error_t gfmdc_client_journal_get_request(struct mdhost *, gfarm_uint64_t);

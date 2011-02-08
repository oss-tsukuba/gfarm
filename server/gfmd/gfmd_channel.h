/*
 * $Id$
 */

#ifdef ENABLE_JOURNAL
struct peer;
struct mdhost;

gfarm_error_t gfm_server_switch_gfmd_channel(struct peer *, int, int);
void gfmdc_init(void);
void *gfmdc_master_thread(void *);
void *gfmdc_slave_thread(void *);
#endif

/*
 * $Id$
 */

void replica_check_start();
void replica_check_signal_host_up();
void replica_check_signal_host_down();
void replica_check_signal_update_xattr();
void replica_check_signal_rename();
void replica_check_signal_rep_request_failed();
void replica_check_signal_rep_result_failed();

gfarm_error_t gfm_server_replica_check_ctrl(struct peer *, int, int);

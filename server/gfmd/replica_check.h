/*
 * $Id$
 */

void replica_check_start();
void replica_check_signal_general(const char *, long);
void replica_check_signal_host_up();
void replica_check_signal_host_down();
void replica_check_signal_update_xattr();
void replica_check_signal_rename();

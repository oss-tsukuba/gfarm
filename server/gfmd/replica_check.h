/*
 * $Id$
 */

void replica_check_init(void);

void replica_check_start_host_up(void);
void replica_check_start_host_down(void);
void replica_check_start_xattr_update(void);
void replica_check_start_move(void);
void replica_check_start_rep_request_failed(void);
void replica_check_start_rep_result_failed(void);
void replica_check_start_fsngroup_modify(void);
void replica_check_start_host_is_not_busy(void);

void replica_check_enqueue(struct inode *, struct dirset *, int, char *,
	const char *);

void replica_check_info(void);

gfarm_error_t gfm_server_replica_check_ctrl(struct peer *, int, int);
gfarm_error_t gfm_server_replica_check_status(struct peer *, int, int);

#define IS_REPLICA_CHECK_REQUIRED(e) \
	(e != GFARM_ERR_NO_ERROR && \
	 e != GFARM_ERR_READ_ONLY_FILE_SYSTEM && \
	 e != GFARM_ERR_DISK_QUOTA_EXCEEDED && \
	 e != GFARM_ERR_NO_MEMORY)

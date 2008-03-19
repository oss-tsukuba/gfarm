struct gfm_connection;
struct gfs_dirent;

struct gfarm_host_info;
struct gfarm_user_info;
struct gfarm_group_info;
struct gfarm_group_names;

struct gfarm_host_sched_info {
	char *host;
	gfarm_uint32_t port;
	gfarm_uint32_t ncpu;	/* XXX should have whole gfarm_host_info? */

	/* if GFM_PROTO_SCHED_FLAG_LOADAVG_AVAIL */
	float loadavg;
	gfarm_uint64_t cache_time;
	gfarm_uint64_t disk_used;
	gfarm_uint64_t disk_avail;

	/* if GFM_PROTO_SCHED_FLAG_RTT_AVAIL */
	gfarm_uint64_t rtt_cache_time;
	gfarm_uint32_t rtt_usec;

	gfarm_uint32_t flags;			/* GFM_PROTO_SCHED_FLAG_* */
};
void gfarm_host_sched_info_free(int, struct gfarm_host_sched_info *);

struct gfp_xdr *gfm_client_connection_conn(struct gfm_connection *);
int gfm_client_connection_fd(struct gfm_connection *);
enum gfarm_auth_method gfm_client_connection_auth_method(
	struct gfm_connection *);

gfarm_error_t gfm_client_connection_acquire(const char *, int,
	struct gfm_connection **);
void gfm_client_connection_free(struct gfm_connection *);

/* host/user/group metadata */

gfarm_error_t gfm_client_host_info_get_all(struct gfm_connection *,
	int *, struct gfarm_host_info **);
gfarm_error_t gfm_client_host_info_get_by_architecture(
	struct gfm_connection *, const char *,
	int *, struct gfarm_host_info **);
gfarm_error_t gfm_client_host_info_get_by_names(struct gfm_connection *,
	int, const char **,
	gfarm_error_t *, struct gfarm_host_info *);
gfarm_error_t gfm_client_host_info_get_by_namealiases(struct gfm_connection *,
	int, const char **,
	gfarm_error_t *, struct gfarm_host_info *);
gfarm_error_t gfm_client_host_info_set(struct gfm_connection *,
	const struct gfarm_host_info *);
gfarm_error_t gfm_client_host_info_modify(struct gfm_connection *,
	const struct gfarm_host_info *);
gfarm_error_t gfm_client_host_info_remove(struct gfm_connection *,
	const char *);

gfarm_error_t gfm_client_user_info_get_all(struct gfm_connection *,
	int *, struct gfarm_user_info **);
gfarm_error_t gfm_client_user_info_get_by_names(struct gfm_connection *,
	int, const char **, gfarm_error_t *, struct gfarm_user_info *);
gfarm_error_t gfm_client_user_info_get_by_gsi_dn(struct gfm_connection *,
	const char *, struct gfarm_user_info *);
gfarm_error_t gfm_client_user_info_set(struct gfm_connection *,
	const struct gfarm_user_info *);
gfarm_error_t gfm_client_user_info_modify(struct gfm_connection *,
	const struct gfarm_user_info *);
gfarm_error_t gfm_client_user_info_remove(struct gfm_connection *,
	const char *);

gfarm_error_t gfm_client_group_info_get_all(struct gfm_connection *,
	int *, struct gfarm_group_info **);
gfarm_error_t gfm_client_group_info_get_by_names(struct gfm_connection *,
	int, const char **,
	gfarm_error_t *, struct gfarm_group_info *);
gfarm_error_t gfm_client_group_info_set(struct gfm_connection *,
	const struct gfarm_group_info *);
gfarm_error_t gfm_client_group_info_modify(struct gfm_connection *,
	const struct gfarm_group_info *);
gfarm_error_t gfm_client_group_info_remove(struct gfm_connection *,
	const char *);
gfarm_error_t gfm_client_group_info_add_users(struct gfm_connection *,
	const char *, int, const char **, gfarm_error_t *);
gfarm_error_t gfm_client_group_info_remove_users(
	struct gfm_connection *, const char *, int,
	const char **, gfarm_error_t *);
gfarm_error_t gfm_client_group_names_get_by_users(struct gfm_connection *,
	int, const char **,
	gfarm_error_t *, struct gfarm_group_names *);

/* gfs from client */
gfarm_error_t gfm_client_compound_begin_request(struct gfm_connection *);
gfarm_error_t gfm_client_compound_begin_result(struct gfm_connection *);
gfarm_error_t gfm_client_compound_end_request(struct gfm_connection *);
gfarm_error_t gfm_client_compound_end_result(struct gfm_connection *);
gfarm_error_t gfm_client_compound_until_eof_request(struct gfm_connection *);
gfarm_error_t gfm_client_compound_until_eof_result(struct gfm_connection *);
gfarm_error_t gfm_client_compound_on_eof_request(struct gfm_connection *);
gfarm_error_t gfm_client_compound_on_eof_result(struct gfm_connection *);
gfarm_error_t gfm_client_compound_on_error_request(struct gfm_connection *,
	gfarm_error_t);
gfarm_error_t gfm_client_compound_on_error_result(struct gfm_connection *);
gfarm_error_t gfm_client_get_fd_request(struct gfm_connection *);
gfarm_error_t gfm_client_get_fd_result(struct gfm_connection *,
	gfarm_int32_t *);
gfarm_error_t gfm_client_put_fd_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_put_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_save_fd_request(struct gfm_connection *);
gfarm_error_t gfm_client_save_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_restore_fd_request(struct gfm_connection *);
gfarm_error_t gfm_client_restore_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_create_request(struct gfm_connection *,
	const char *, gfarm_uint32_t, gfarm_uint32_t);
gfarm_error_t gfm_client_create_result(struct gfm_connection *,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_mode_t *);
gfarm_error_t gfm_client_open_request(struct gfm_connection *,
	const char *, size_t, gfarm_uint32_t);
gfarm_error_t gfm_client_open_result(struct gfm_connection *,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_mode_t *);
gfarm_error_t gfm_client_open_root_request(struct gfm_connection *,
	gfarm_uint32_t);
gfarm_error_t gfm_client_open_root_result(struct gfm_connection *);
gfarm_error_t gfm_client_open_parent_request(struct gfm_connection *,
	gfarm_uint32_t);
gfarm_error_t gfm_client_open_parent_result(struct gfm_connection *);
gfarm_error_t gfm_client_close_request(struct gfm_connection *);
gfarm_error_t gfm_client_close_result(struct gfm_connection *);
gfarm_error_t gfm_client_close_read_request(struct gfm_connection *,
	gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_close_read_result(struct gfm_connection *);
gfarm_error_t gfm_client_close_write_request(struct gfm_connection *,
	gfarm_off_t,
	gfarm_int64_t, gfarm_int32_t, gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_close_write_result(struct gfm_connection *);
gfarm_error_t gfm_client_verify_type_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_verify_type_result(struct gfm_connection *);
gfarm_error_t gfm_client_verify_type_not_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_verify_type_not_result(struct gfm_connection *);
gfarm_error_t gfm_client_bequeath_fd_request(struct gfm_connection *);
gfarm_error_t gfm_client_bequeath_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_inherit_fd_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_inherit_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_fstat_request(struct gfm_connection *);
gfarm_error_t gfm_client_fstat_result(struct gfm_connection *,
	struct gfs_stat *);
gfarm_error_t gfm_client_futimes_request(struct gfm_connection *,
	gfarm_int64_t, gfarm_int32_t, gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_futimes_result(struct gfm_connection *);
gfarm_error_t gfm_client_fchmod_request(struct gfm_connection *,
	gfarm_mode_t);
gfarm_error_t gfm_client_fchmod_result(struct gfm_connection *);
gfarm_error_t gfm_client_fchown_request(struct gfm_connection *,
	const char *, const char *);
gfarm_error_t gfm_client_fchown_result(struct gfm_connection *);
gfarm_error_t gfm_client_cksum_get_request(struct gfm_connection *);
gfarm_error_t gfm_client_cksum_get_result(struct gfm_connection *,
	char **, size_t, size_t *, char *, gfarm_int32_t *);
gfarm_error_t gfm_client_cksum_set_request(struct gfm_connection *,
	char *, size_t, const char *,
	gfarm_int32_t, gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_cksum_set_result(struct gfm_connection *);
gfarm_error_t gfm_client_schedule_file_request(struct gfm_connection *,
	const char *);
gfarm_error_t gfm_client_schedule_file_result(
	struct gfm_connection *, int *, struct gfarm_host_sched_info **);
gfarm_error_t gfm_client_schedule_file_with_program_request(
	struct gfm_connection *, const char *);
gfarm_error_t gfm_client_schedule_file_with_program_result(
	struct gfm_connection *, int *, struct gfarm_host_sched_info **);
gfarm_error_t gfm_client_remove_request(struct gfm_connection *, const char *);
gfarm_error_t gfm_client_remove_result(struct gfm_connection *);
gfarm_error_t gfm_client_rename_request(struct gfm_connection *,
	const char *, const char *);
gfarm_error_t gfm_client_rename_result(struct gfm_connection *);
gfarm_error_t gfm_client_flink_request(struct gfm_connection *, const char *);
gfarm_error_t gfm_client_flink_result(struct gfm_connection *);
gfarm_error_t gfm_client_mkdir_request(struct gfm_connection *,
	const char *, gfarm_mode_t);
gfarm_error_t gfm_client_mkdir_result(struct gfm_connection *);
gfarm_error_t gfm_client_symlink_request(struct gfm_connection *,
	const char *, const char *);
gfarm_error_t gfm_client_symlink_result(struct gfm_connection *);
gfarm_error_t gfm_client_readlink(struct gfm_connection *);
gfarm_error_t gfm_client_readlink_result(struct gfm_connection *, char **);
gfarm_error_t gfm_client_getdirpath_request(struct gfm_connection *);
gfarm_error_t gfm_client_getdirpath_result(struct gfm_connection *, char **);
gfarm_error_t gfm_client_getdirents_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_getdirents_result(struct gfm_connection *,
	int *, struct gfs_dirent *);
gfarm_error_t gfm_client_seek_request(struct gfm_connection *,
	gfarm_off_t, gfarm_int32_t);
gfarm_error_t gfm_client_seek_result(struct gfm_connection *, gfarm_off_t *);

/* gfs from gfsd */
gfarm_error_t gfm_client_reopen_request(struct gfm_connection *);
gfarm_error_t gfm_client_reopen_result(struct gfm_connection *,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_int32_t *, gfarm_int32_t *,
	gfarm_int32_t *);
gfarm_error_t gfm_client_lock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfm_client_lock_result(struct gfm_connection *);
gfarm_error_t gfm_client_trylock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfm_client_trylock_result(struct gfm_connection *);
gfarm_error_t gfm_client_unlock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfm_client_unlock_result(struct gfm_connection *);
gfarm_error_t gfm_client_lock_info_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfm_client_lock_info_result(struct gfm_connection *,
	gfarm_off_t *, gfarm_off_t *, gfarm_int32_t *, char **, gfarm_pid_t *);
gfarm_error_t gfm_client_switch_back_channel(struct gfm_connection *);

/* gfs_pio from client */
/*XXX*/

/* misc operations from gfsd */
gfarm_error_t gfm_client_hostname_set(struct gfm_connection *, char *);

/* replica management from client */
gfarm_error_t gfm_client_replica_list_by_name_request(struct gfm_connection *);
gfarm_error_t gfm_client_replica_list_by_name_result(struct gfm_connection *,
	gfarm_int32_t *, char ***);
gfarm_error_t gfm_client_replica_list_by_host_request(struct gfm_connection *,
	const char *, gfarm_int32_t);
gfarm_error_t gfm_client_replica_list_by_host_result(struct gfm_connection *,
	gfarm_int32_t *, gfarm_ino_t **);
gfarm_error_t gfm_client_replica_remove_by_host_request(
	struct gfm_connection *, const char *, gfarm_int32_t);
gfarm_error_t gfm_client_replica_remove_by_host_result(
	struct gfm_connection *);

/* replica management from gfsd */
gfarm_error_t gfm_client_replica_adding_request(struct gfm_connection *,
	char *);
gfarm_error_t gfm_client_replica_adding_result(struct gfm_connection *,
	gfarm_ino_t *, gfarm_uint64_t *, gfarm_int64_t *, gfarm_int32_t *);
gfarm_error_t gfm_client_replica_added_request(struct gfm_connection *,
	gfarm_int32_t, gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_replica_added_result(struct gfm_connection *);
gfarm_error_t gfm_client_replica_remove_request(struct gfm_connection *,
	gfarm_ino_t, gfarm_uint64_t);
gfarm_error_t gfm_client_replica_remove_result(struct gfm_connection *);
gfarm_error_t gfm_client_replica_add_request(struct gfm_connection *,
	gfarm_ino_t, gfarm_uint64_t, gfarm_off_t);
gfarm_error_t gfm_client_replica_add_result(struct gfm_connection *);

/* process management */
gfarm_error_t gfm_client_process_alloc(struct gfm_connection *,
	gfarm_int32_t, const char *, size_t, gfarm_pid_t *);
gfarm_error_t gfm_client_process_alloc_child(struct gfm_connection *,
	gfarm_int32_t, const char *, size_t, gfarm_pid_t,
	gfarm_int32_t, const char *, size_t, gfarm_pid_t *);

gfarm_error_t gfm_client_process_free(struct gfm_connection *);
gfarm_error_t gfm_client_process_set(struct gfm_connection *,
	gfarm_int32_t, const char *, size_t, gfarm_pid_t);

struct gfm_connection;
struct gfs_dirent;

extern struct gfm_connection *gfarm_metadb_server;
#define gfarm_jobmanager_server	gfarm_metadb_server


/* host/user/group metadata */

char *gfm_client_host_info_get_all(struct gfm_connection *,
	int *, struct gfarm_host_info **);
char *gfm_client_host_info_get_by_architecture(struct gfm_connection *, char *,
	int *, struct gfarm_host_info **);
void gfm_client_host_info_get_by_names(struct gfm_connection *, int, char **,
	char **, struct gfarm_host_info *);
void gfm_client_host_info_get_by_namealiases(struct gfm_connection *,
	int, char **,
	char **, struct gfarm_host_info *);
char *gfm_client_host_info_set(struct gfm_connection *, char *,
	struct gfarm_host_info *);
char *gfm_client_host_info_modify(struct gfm_connection *, char *,
	struct gfarm_host_info *);
char *gfm_client_host_info_remove(struct gfm_connection *, char *);

char *gfm_client_user_info_get_all(struct gfm_connection *,
	int *, struct gfarm_user_info **);
void gfm_client_user_info_get_by_names(struct gfm_connection *, int, char **,
	char **, struct gfarm_user_info *);
char *gfm_client_user_info_set(struct gfm_connection *, char *,
	struct gfarm_user_info *);
char *gfm_client_user_info_modify(struct gfm_connection *, char *,
	struct gfarm_user_info *);
char *gfm_client_user_info_remove(struct gfm_connection *, char *);

char *gfm_client_group_info_get_all(struct gfm_connection *,
	int *, struct gfarm_group_info **);
void gfm_client_group_info_get_by_names(struct gfm_connection *, int, char **,
	char **, struct gfarm_group_info *);
char *gfm_client_group_info_set(struct gfm_connection *, char *,
	struct gfarm_group_info *);
char *gfm_client_group_info_modify(struct gfm_connection *, char *,
	struct gfarm_group_info *);
char *gfm_client_group_info_remove(struct gfm_connection *, char *);
char *gfm_client_group_info_add_users(struct gfm_connection *, char *,
	int, char **);
char *gfm_client_group_info_remove_users(struct gfm_connection *, char *,
	int, char **);
void gfm_client_group_names_get_by_users(struct gfm_connection *,
	int, char **,
	char **, struct gfarm_group_names *);

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
gfarm_error_t gfm_client_push_fd_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_push_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_swap_fd_request(struct gfm_connection *);
gfarm_error_t gfm_client_swap_fd_result(struct gfm_connection *);
gfarm_error_t gfm_client_create_request(struct gfm_connection *,
	const char *, size_t, gfarm_uint32_t, gfarm_uint32_t);
gfarm_error_t gfm_client_create_result(struct gfm_connection *,
	gfarm_uint32_t *, gfarm_uint64_t *);
gfarm_error_t gfm_client_open_request(struct gfm_connection *,
	const char *, size_t, gfarm_uint32_t);
gfarm_error_t gfm_client_open_result(struct gfm_connection *,
	gfarm_uint32_t *, gfarm_uint64_t *);
gfarm_error_t gfm_client_open_root_request(struct gfm_connection *);
gfarm_error_t gfm_client_open_root_result(struct gfm_connection *,
	gfarm_uint32_t *);
gfarm_error_t gfm_client_close_request(struct gfm_connection *);
gfarm_error_t gfm_client_close_result(struct gfm_connection *);
gfarm_error_t gfm_client_close_read_request(struct gfm_connection *,
	gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_close_read_result(struct gfm_connection *);
gfarm_error_t gfm_client_close_write_request(struct gfm_connection *,
	gfarm_int64_t, gfarm_int32_t, gfarm_int64_t, gfarm_int32_t);
gfarm_error_t gfm_client_close_write_result(struct gfm_connection *);
gfarm_error_t gfm_client_verify_type_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_verify_type_result(struct gfm_connection *);
gfarm_error_t gfm_client_verify_type_not_request(struct gfm_connection *,
	gfarm_int32_t);
gfarm_error_t gfm_client_verify_type_not_result(struct gfm_connection *);
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
gfarm_error_t gfm_client_schedule_file_request(struct gfm_connection *,
	const char *);
gfarm_error_t gfm_client_schedule_file_result(struct gfm_connection *,
	char **, gfarm_int32_t *);
gfarm_error_t gfm_client_schedule_file_with_program_request(
	struct gfm_connection *, const char *);
gfarm_error_t gfm_client_schedule_file_with_program_result(
	struct gfm_connection *, char **, gfarm_int32_t *);
gfarm_error_t gfm_client_remove_request(struct gfm_connection *, const char *);
gfarm_error_t gfm_client_remove_result(struct gfm_connection *);
gfarm_error_t gfm_client_rmdir_request(struct gfm_connection *, const char *);
gfarm_error_t gfm_client_rmdir_result(struct gfm_connection *);
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
gfarm_error_t gfm_client_lock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, const char *, gfarm_pid_t);
gfarm_error_t gfm_client_lock_result(struct gfm_connection *);
gfarm_error_t gfm_client_trylock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, const char *, gfarm_pid_t);
gfarm_error_t gfm_client_trylock_result(struct gfm_connection *);
gfarm_error_t gfm_client_unlock_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t);
gfarm_error_t gfm_client_unlock_result(struct gfm_connection *);
gfarm_error_t gfm_client_lock_info_request(struct gfm_connection *,
	gfarm_off_t, gfarm_off_t);
gfarm_error_t gfm_client_lock_info_result(struct gfm_connection *,
	gfarm_int32_t, char **, gfarm_pid_t);

/* gfs_pio from client */
/*XXX*/

/* replica management from client */
gfarm_error_t gfm_client_replica_list_by_name_request(struct gfm_connection *);
gfarm_error_t gfm_client_replica_list_by_name_result(struct gfm_connection *,
	gfarm_int32_t *, char ***, int **);
gfarm_error_t gfm_client_replica_list_by_host_request(struct gfm_connection *,
	const char *, gfarm_int32_t);
gfarm_error_t gfm_client_replica_list_by_host_result(struct gfm_connection *,
	gfarm_int32_t *, gfarm_ino_t **);
gfarm_error_t gfm_client_replica_remove_by_host_request(
	struct gfm_connection *, const char *, gfarm_int32_t);
gfarm_error_t gfm_client_replica_remove_by_host_result(
	struct gfm_connection *);

/* replica management from gfsd */
gfarm_error_t gfm_client_replica_add_request(struct gfm_connection *,
	gfarm_ino_t);
gfarm_error_t gfm_client_replica_add_result(struct gfm_connection *);
gfarm_error_t gfm_client_replica_remove_request(struct gfm_connection *,
	gfarm_ino_t);
gfarm_error_t gfm_client_replica_remove_result(struct gfm_connection *);

/* process management */
gfarm_error_t gfm_client_process_alloc(struct gfm_connection *,
	const char *, size_t, gfarm_pid_t *);
gfarm_error_t gfm_client_process_free(struct gfm_connection *);
gfarm_error_t gfm_client_process_set(struct gfm_connection *,
	const char *, size_t, gfarm_pid_t);

#define GFARM_CONFIG_MISC_DEFAULT	-1
#define GFARM_F2LL_SCALE		1000LL
#define GFARM_F2LL_SCALE_SIZE		3

/* gfsd dependent */
/* GFS dependent */
extern int gfarm_spool_server_listen_backlog;
extern char *gfarm_spool_server_listen_address;
extern int gfarm_spool_server_back_channel_rcvbuf_limit;
#define GFARM_SPOOL_ROOT_NUM	5
extern char *gfarm_spool_root[];
gfarm_error_t parse_set_spool_root(char *);
enum gfarm_spool_check_level {
	GFARM_SPOOL_CHECK_LEVEL_DEFAULT,
	GFARM_SPOOL_CHECK_LEVEL_DISABLE,
	GFARM_SPOOL_CHECK_LEVEL_DISPLAY,
	GFARM_SPOOL_CHECK_LEVEL_DELETE,
	GFARM_SPOOL_CHECK_LEVEL_LOST_FOUND,
};
enum gfarm_spool_check_level gfarm_spool_check_level_get(void);
const char *gfarm_spool_check_level_get_by_name(void);
gfarm_error_t gfarm_spool_check_level_set(enum gfarm_spool_check_level);
gfarm_error_t gfarm_spool_check_level_set_by_name(const char *);
#define GFARM_SPOOL_CHECK_PARALLEL_AUTOMATIC	0
extern int gfarm_spool_check_parallel;
extern int gfarm_spool_check_parallel_max;
extern int gfarm_spool_check_parallel_step;
extern gfarm_off_t gfarm_spool_check_parallel_per_capacity;
extern float gfarm_spool_base_load;
extern int gfarm_spool_digest_error_check;
extern int gfarm_write_verify;
extern int gfarm_write_verify_interval;
extern int gfarm_write_verify_retry_interval;
extern int gfarm_write_verify_log_interval;

/* GFM dependent */
enum gfarm_atime_type {
	GFARM_ATIME_DEFAULT,
	GFARM_ATIME_DISABLE,
	GFARM_ATIME_RELATIVE,
	GFARM_ATIME_STRICT,
};
enum gfarm_atime_type gfarm_atime_type_get(void);
const char *gfarm_atime_type_get_by_name(void);
gfarm_error_t gfarm_atime_type_set(enum gfarm_atime_type);
gfarm_error_t gfarm_atime_type_set_by_name(const char *);
extern int gfarm_max_open_files;

enum gfarm_backend_db_type {
	GFARM_BACKEND_DB_TYPE_UNKNOWN,
	GFARM_BACKEND_DB_TYPE_LDAP,
	GFARM_BACKEND_DB_TYPE_POSTGRESQL,
	GFARM_BACKEND_DB_TYPE_LOCALFS
};
extern enum gfarm_backend_db_type gfarm_backend_db_type;

extern int gfarm_metadb_server_listen_backlog;
extern int gfarm_xattr_size_limit;
extern int gfarm_xmlattr_size_limit;
extern int gfarm_max_directory_depth;
extern int gfarm_metadb_version_major;
extern int gfarm_metadb_version_minor;
extern int gfarm_metadb_version_teeny;
extern int gfarm_metadb_max_descriptors;
extern int gfarm_metadb_stack_size;
extern int gfarm_metadb_thread_pool_size;
extern int gfarm_metadb_job_queue_length;
extern int gfarm_metadb_remover_queue_length;
extern int gfarm_metadb_remove_scan_log_interval;
extern int gfarm_metadb_remove_scan_interval_factor;
extern int gfarm_metadb_heartbeat_interval;
extern int gfarm_metadb_dbq_size;
extern int gfarm_metadb_server_back_channel_sndbuf_limit;
extern int gfarm_metadb_server_nfs_root_squash_support;

enum gfarm_lock_type {
	GFARM_LOCK_TYPE_MUTEX,
	GFARM_LOCK_TYPE_TICKETLOCK,
	GFARM_LOCK_TYPE_QUEUELOCK,
	GFARM_LOCK_TYPE_LIMIT
};
extern int gfarm_metadb_server_long_term_lock_type;

extern int gfarm_metadb_replica_remover_by_host_sleep_time;
extern int gfarm_metadb_replica_remover_by_host_inode_step;
extern int gfarm_replica_check;
extern int gfarm_replica_check_remove;
extern int gfarm_replica_check_remove_grace_used_space_ratio;
extern int gfarm_replica_check_remove_grace_time;
extern int gfarm_replica_check_reduced_log;
extern int gfarm_replica_check_host_down_thresh;
extern int gfarm_replica_check_sleep_time;
extern int gfarm_replica_check_yield_time;
extern int gfarm_replica_check_minimum_interval;
extern int gfarm_replicainfo_enabled;
#define GFARM_METADB_STACK_SIZE_DEFAULT 0 /* use OS default */
#define GFARM_METADB_THREAD_POOL_SIZE_DEFAULT	16  /* quadcore, quadsocket */
#if 0
#define GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT	160 /* THREAD_POOL * 10 */
#define GFARM_METADB_REMOVER_QUEUE_LENGTH_DEFAULT	16
#else /* XXX FIXME: until bcworkq is implemented */
#define GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT	16000
#define GFARM_METADB_REMOVER_QUEUE_LENGTH_DEFAULT	160
#endif
#define GFARM_METADB_REMOVE_SCAN_LOG_INTERVAL_DEFAULT	3600 /* 3600 seconds */
#define GFARM_METADB_REMOVE_SCAN_INTERVAL_FACTOR_DEFAULT 5 /* 1/5 */
#define GFARM_METADB_HEARTBEAT_INTERVAL_DEFAULT 180 /* 3 min */
#define GFARM_METADB_DBQ_SIZE_DEFAULT	65536
#define GFARM_SYMLINK_LEVEL_MAX			20

/* LDAP dependent */
extern char *gfarm_ldap_server_name;
extern char *gfarm_ldap_server_port;
extern char *gfarm_ldap_base_dn;
extern char *gfarm_ldap_bind_dn;
extern char *gfarm_ldap_bind_password;
extern char *gfarm_ldap_tls;
extern char *gfarm_ldap_tls_cipher_suite;
extern char *gfarm_ldap_tls_certificate_key_file;
extern char *gfarm_ldap_tls_certificate_file;

/* PostgreSQL dependent */
extern char *gfarm_postgresql_server_name;
extern char *gfarm_postgresql_server_port;
extern char *gfarm_postgresql_dbname;
extern char *gfarm_postgresql_user;
extern char *gfarm_postgresql_password;
extern char *gfarm_postgresql_conninfo;

/* LocalFS dependent */
extern char *gfarm_localfs_datadir;

/* security */
extern char *gfarm_get_shared_key_file(void);

/* IO statistics */
extern char *gfarm_iostat_gfmd_path;
extern char *gfarm_iostat_gfsd_path;
extern int gfarm_iostat_max_client;

/* miscellaneous configurations */
extern char *gfarm_digest;
extern int gfarm_simultaneous_replication_receivers;
extern int gfarm_replication_busy_host;

gfarm_error_t gfarm_get_global_username_by_host_for_connection_cache(
	const char *, int, char **);

int gfarm_schedule_write_local_priority(void);
char *gfarm_schedule_write_target_domain(void);
gfarm_off_t gfarm_get_minimum_free_disk_space(void);
const char *gfarm_config_get_argv0(void);
gfarm_error_t gfarm_config_set_argv0(const char *);

void gfarm_setup_debug_command(void);

int gfarm_get_metadb_replication_enabled(void);
void gfarm_set_metadb_replication_enabled(int);
const char *gfarm_get_journal_dir(void);
int gfarm_get_journal_max_size(void);
int gfarm_get_journal_recvq_size(void);
int gfarm_get_journal_sync_file(void);
int gfarm_get_journal_sync_slave_timeout(void);
int gfarm_get_metadb_server_slave_replication_timeout(void);
int gfarm_get_metadb_server_slave_max_size(void);
int gfarm_get_metadb_server_force_slave(void);
void gfarm_set_metadb_server_force_slave(int);

/* configuration manipulation */

struct gfarm_config_type;
union gfarm_config_storage {
	int i;
	char *s;
};
gfarm_error_t gfarm_config_type_by_name_for_metadb(const char *,
	const struct gfarm_config_type **);
char gfarm_config_type_get_format(const struct gfarm_config_type *);
int gfarm_config_type_is_privileged_to_get(const struct gfarm_config_type *);
gfarm_error_t gfarm_config_copyin(const struct gfarm_config_type *,
	union gfarm_config_storage *);
gfarm_error_t gfarm_config_copyout(const struct gfarm_config_type *,
	union gfarm_config_storage *);

gfarm_error_t gfarm_config_local_name_to_string(const char *, char *, size_t);
gfarm_error_t gfarm_config_name_foreach(
	gfarm_error_t (*)(void *, const char *), void *, int);
#define GFARM_CONFIG_NAME_FLAG_FOR_METADB	1
#define GFARM_CONFIG_NAME_FLAG_FOR_CLIENT	2

struct gfm_connection;
gfarm_error_t gfm_client_config_name_to_string(struct gfm_connection *,
	const char *, char *, size_t);
gfarm_error_t gfm_client_config_set_by_string(struct gfm_connection *, char *);
gfarm_error_t gfm_client_config_get_vars_request(struct gfm_connection *,
	int, void **);
gfarm_error_t gfm_client_config_get_vars_result(struct gfm_connection *,
	int, void **);

/* miscellaneous */
extern int gfarm_network_receive_timeout;
extern int gfarm_file_trace;

void gfarm_config_set_filename(char *);
char *gfarm_config_get_filename(void);

void gfarm_config_clear(void);
#ifdef GFARM_USE_STDIO
gfarm_error_t gfarm_config_read_file(FILE *, int *);
#endif
void gfarm_config_set_default_ports(void);
void gfarm_config_set_default_misc(void);
gfarm_error_t gfarm_sockbuf_apply_limit(int, int, int, const char *);
void gfs_display_timers(void);

int gfarm_xattr_caching_patterns_number(void);
char **gfarm_xattr_caching_patterns(void);

gfarm_error_t gfarm_set_local_user_for_this_uid(uid_t);

/* for client */
struct gfs_connection;

gfarm_error_t gfarm_client_process_set(struct gfs_connection *,
	struct gfm_connection *);
gfarm_error_t gfarm_client_process_reset(struct gfs_connection *,
	struct gfm_connection *);

/* for server */
gfarm_error_t gfarm_server_initialize(char *, int *, char ***);
gfarm_error_t gfarm_server_terminate(void);
gfarm_error_t gfarm_server_config_read(void);

/* for linux helper */
extern void(*gfarm_ug_maps_notify)(const char *, int , int , const char *);


#define GFARM_CONFIG_MISC_DEFAULT	-1
#define GFARM_F2LL_SCALE		1000LL
#define GFARM_F2LL_SCALE_SIZE		3

/* gfsd dependent */
/* GFS dependent */
extern int gfarm_spool_server_listen_backlog;
extern char *gfarm_spool_server_listen_address;
extern char *gfarm_spool_root;
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
extern int gfarm_metadb_max_descriptors;
extern int gfarm_metadb_stack_size;
extern int gfarm_metadb_thread_pool_size;
extern int gfarm_metadb_job_queue_length;
extern int gfarm_metadb_heartbeat_interval;
extern int gfarm_metadb_dbq_size;
#ifdef not_def_REPLY_QUEUE
extern int gfm_proto_reply_to_gfsd_window;
#endif
extern int gfs_proto_fhremove_request_window;
extern int gfs_proto_replication_request_window;
extern int gfarm_outstanding_file_replication_limit;
extern int gfarm_relatime;
extern int gfarm_replica_check;
extern int gfarm_replica_check_host_down_thresh;
extern int gfarm_replica_check_sleep_time;
extern int gfarm_replica_check_minimum_interval;
#define GFARM_METADB_STACK_SIZE_DEFAULT 0 /* use OS default */
#define GFARM_METADB_THREAD_POOL_SIZE_DEFAULT	16  /* quadcore, quadsocket */
#if 0
#define GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT	160 /* THREAD_POOL * 10 */
#else /* XXX FIXME: until bcworkq is implemented */
#define GFARM_METADB_JOB_QUEUE_LENGTH_DEFAULT	16000
#endif
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
int gfarm_get_metadb_server_slave_max_size(void);
int gfarm_get_metadb_server_force_slave(void);
void gfarm_set_metadb_server_force_slave(int);
int gfarm_get_metadb_server_slave_listen(void);

/* miscellaneous */
extern int gfarm_network_receive_timeout;
extern int gfarm_file_trace;

void gfarm_config_set_filename(char *);
char *gfarm_config_get_filename(void);

void gfarm_config_clear(void);
#ifdef GFARM_USE_STDIO
gfarm_error_t gfarm_config_read_file(FILE *, int *);
#endif
gfarm_error_t gfarm_init_config(void);
gfarm_error_t gfarm_free_config(void);
void gfarm_config_set_default_ports(void);
void gfarm_config_set_default_misc(void);
void gfs_display_timers(void);

int gfarm_xattr_caching_patterns_number(void);
char **gfarm_xattr_caching_patterns(void);

gfarm_error_t gfarm_set_local_user_for_this_uid(uid_t);

/* for client */
struct gfs_connection;

struct gfm_connection;
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


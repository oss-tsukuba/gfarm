extern char *gfarm_config_file;

/* gfsd dependent */
/* GFS dependent */
extern char *gfarm_spool_server_listen_address;
extern char *gfarm_spool_root;

enum gfarm_backend_db_type {
	GFARM_BACKEND_DB_TYPE_UNKNOWN,
	GFARM_BACKEND_DB_TYPE_LDAP,
	GFARM_BACKEND_DB_TYPE_POSTGRESQL,
	GFARM_BACKEND_DB_TYPE_LOCALFS
};

extern enum gfarm_backend_db_type gfarm_backend_db_type;

/* GFM dependent */
extern char *gfarm_metadb_server_name;
extern int gfarm_metadb_server_port;

extern char *gfarm_metadb_admin_user;

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

/* miscellaneous configurations */
extern int gfarm_log_level; /* syslog priority level to log */
#define GFARM_DIR_CACHE_TIMEOUT_DEFAULT	86400 /* 1 day */
extern int gfarm_dir_cache_timeout;
extern int gfarm_host_cache_timeout;
extern int gfarm_schedule_cache_timeout;
extern int gfarm_gfsd_connection_cache;
extern int gfarm_record_atime;

extern int gf_on_demand_replication;
extern int gf_hook_default_global;

int gfarm_schedule_write_local_priority(void);
char *gfarm_schedule_write_target_domain(void);
gfarm_off_t gfarm_get_minimum_free_disk_space(void);

/* redirection */
extern struct gfs_file *gf_stdout, *gf_stderr;

/* miscellaneous */
extern int gfarm_schedule_cache_timeout;
extern gfarm_int64_t gfarm_minimum_free_disk_space;

/* XXX FIXME this should disappear to support multiple metadata server */
struct gfm_connection;
extern struct gfm_connection *gfarm_metadb_server;

void gfarm_config_clear(void);
#ifdef GFARM_USE_STDIO
gfarm_error_t gfarm_config_read_file(FILE *, int *);
#endif
gfarm_error_t gfarm_init_user_map(void);
void gfarm_config_set_default_ports(void);
void gfarm_config_set_default_misc(void);

/* for client */
struct gfs_connection;
gfarm_error_t gfarm_client_process_set(struct gfs_connection *);

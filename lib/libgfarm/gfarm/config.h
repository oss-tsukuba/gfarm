extern int gfarm_is_active_file_system_node;

/* GFS dependent */
extern char *gfarm_spool_server_listen_address;
extern char *gfarm_spool_root;
extern int gfarm_spool_server_port;

/* GFM dependent */
extern char *gfarm_metadb_server_name;
extern int gfarm_metadb_server_port;

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

/* miscellaneous */
#define GFARM_DIR_CACHE_TIMEOUT_DEFAULT	86400 /* 1 day */
extern int gfarm_dir_cache_timeout;
extern int gfarm_host_cache_timeout;
extern int gfarm_schedule_cache_timeout;
extern file_offset_t gfarm_minimum_free_disk_space;

int gfarm_schedule_write_local_priority(void);

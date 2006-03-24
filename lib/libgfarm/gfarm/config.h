#ifndef GFARM_CONFIG
#define GFARM_CONFIG	"/etc/gfarm.conf"
#endif
#ifndef GFARM_CLIENT_RC
#define GFARM_CLIENT_RC		".gfarmrc"
#endif
#ifndef GFARM_SPOOL_ROOT
#define GFARM_SPOOL_ROOT	"/var/spool/gfarm"
#endif

extern char *gfarm_config_file;

/* gfsd dependent */
extern char *gfarm_spool_root;
extern int gfarm_spool_server_port;

/* GFM dependent */
extern char *gfarm_metadb_server_name;
extern int gfarm_metadb_server_port;

/* XXX FIXME this should disappear to support multiple metadata server */
struct gfm_connection;
extern struct gfm_connection *gfarm_metadb_server;

void gfarm_config_clear(void);
gfarm_error_t gfarm_config_read_file(FILE *, char *, int *);
gfarm_error_t gfarm_init_user_map(void);
void gfarm_config_set_default_ports(void);

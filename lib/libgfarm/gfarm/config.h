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
extern char *gfarm_spool_root;

void gfarm_config_clear(void);
gfarm_error_t gfarm_config_read_file(FILE *, char *, int *);
void gfarm_config_set_default_ports(void);

gfarm_error_t gfarm_init_user_map(void);
gfarm_error_t gfarm_username_map_global_to_local(char *, char **);
gfarm_error_t gfarm_username_map_local_to_global(char *, char **);

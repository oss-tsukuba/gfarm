struct gfm_connection;

extern struct gfm_connection *gfarm_metadb_server;
#define gfarm_jobmanager_server	gfarm_metadb_server


char *gfm_initialize(void);

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

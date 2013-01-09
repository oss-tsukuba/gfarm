struct gfarm_filesystem;
struct gfarm_metadb_server;
struct gfm_connection;

gfarm_error_t gfarm_filesystem_init(void);
struct gfarm_filesystem *gfarm_filesystem_get(const char *, int);
struct gfarm_filesystem *gfarm_filesystem_get_default(void);
struct gfarm_filesystem *gfarm_filesystem_get_by_connection(
	struct gfm_connection *);
gfarm_error_t gfarm_filesystem_set_metadb_server_list(struct gfarm_filesystem *,
	struct gfarm_metadb_server **, int);
struct gfarm_metadb_server **gfarm_filesystem_get_metadb_server_list(
	struct gfarm_filesystem *, int *);
int gfarm_filesystem_is_initialized(void);
int gfarm_filesystem_has_multiple_servers(struct gfarm_filesystem *);

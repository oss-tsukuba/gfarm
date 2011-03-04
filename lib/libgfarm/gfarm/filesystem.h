struct gfarm_filesystem;
struct gfarm_metadb_server;

gfarm_error_t gfarm_filesystem_init(void);
struct gfarm_filesystem *gfarm_filesystem_get(const char *, int);
struct gfarm_filesystem *gfarm_filesystem_get_default(void);
struct gfarm_filesystem *gfarm_get_filesystem(const char *, int);
gfarm_error_t gfarm_filesystem_set_metadb_server_list(struct gfarm_filesystem *,
	struct gfarm_metadb_server **, int);
struct gfarm_metadb_server **gfarm_filesystem_get_metadb_server_list(
	struct gfarm_filesystem *, int *);
int gfarm_filesystem_is_initialized(void);

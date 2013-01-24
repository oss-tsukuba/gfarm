struct gfarm_filesystem;
struct gfarm_metadb_server;
struct gfm_connection;
struct gfs_file_list;

gfarm_error_t gfarm_filesystem_init(void);
struct gfarm_filesystem *gfarm_filesystem_get(const char *, int);
struct gfarm_filesystem *gfarm_filesystem_get_default(void);
gfarm_error_t gfarm_filesystem_add(const char *, int,
	struct gfarm_filesystem **);
int gfarm_filesystem_is_default(struct gfarm_filesystem *);
struct gfarm_filesystem *gfarm_filesystem_get_by_connection(
	struct gfm_connection *);
gfarm_error_t gfarm_filesystem_set_metadb_server_list(struct gfarm_filesystem *,
	struct gfarm_metadb_server **, int);
struct gfarm_metadb_server **gfarm_filesystem_get_metadb_server_list(
	struct gfarm_filesystem *, int *);
struct gfarm_metadb_server *gfarm_filesystem_get_metadb_server_first(
	struct gfarm_filesystem *);
int gfarm_filesystem_is_initialized(void);
struct gfs_file_list *gfarm_filesystem_opened_file_list(
	struct gfarm_filesystem *);
int gfarm_filesystem_failover_detected(struct gfarm_filesystem *);
void gfarm_filesystem_set_failover_detected(struct gfarm_filesystem *, int);
int gfarm_filesystem_failover_count(struct gfarm_filesystem *);
void gfarm_filesystem_set_failover_count(struct gfarm_filesystem *, int);
int gfarm_filesystem_in_failover_process(struct gfarm_filesystem *);
void gfarm_filesystem_set_in_failover_process(struct gfarm_filesystem *, int);

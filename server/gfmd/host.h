gfarm_error_t host_init(void);

struct host;
struct host *host_lookup(const char *);
char *host_name(struct host *);

gfarm_error_t host_get_loadav(struct host *, double *);
gfarm_error_t host_remove_replica(struct host *, gfarm_ino_t);

struct peer;
gfarm_error_t gfm_server_host_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_architecture(struct peer *, int,int);
gfarm_error_t gfm_server_host_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_namealises(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_remove(struct peer *, int, int);

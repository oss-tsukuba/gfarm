void host_init(void);

struct host;
struct host *host_lookup(const char *);
struct sockaddr;
struct host *host_addr_lookup(const char *, struct sockaddr *);
char *host_name(struct host *);
int host_port(struct host *);
int host_is_up(struct host *);


gfarm_error_t host_remove_replica(struct host *, gfarm_ino_t, gfarm_uint64_t);

struct peer;
gfarm_error_t gfm_server_host_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_architecture(struct peer *, int,int);
gfarm_error_t gfm_server_host_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_get_by_namealiases(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_host_info_remove(struct peer *, int, int);

gfarm_error_t host_schedule_reply_n(struct peer *, gfarm_int32_t,const char *);
gfarm_error_t host_schedule_reply(struct host *, struct peer *, const char *);
gfarm_error_t host_schedule_reply_all(struct peer *, const char *);

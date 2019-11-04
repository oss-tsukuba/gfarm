/* need #include <gfarm/gfarm_config.h> to see HAVE_GETLOADAVG */

extern int debug_mode;
extern struct gfm_connection *gfm_server;
extern const char READONLY_CONFIG_FILE[];
extern int gfarm_spool_root_len[];
extern int gfarm_spool_root_num;
extern char *canonical_self_name;

#ifndef HAVE_GETLOADAVG
int getloadavg(double *, int);
#endif

int gfsd_statfs(char *, gfarm_int32_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	int *);
void gfsd_statfs_all(gfarm_int32_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	int *);

gfarm_error_t register_to_lost_found(int, int, gfarm_ino_t, gfarm_uint64_t);
void gfsd_spool_check();

void cleanup(int);

#define fatal_metadb_proto(msg_no, diag, proto, e) \
	fatal_metadb_proto_full(msg_no, __FILE__, __LINE__, __func__, \
	    diag, proto, e)

void fatal_metadb_proto_full(int,
	const char *, int, const char *,
	const char *, const char *, gfarm_error_t);

#define fatal(msg_no, ...) \
	fatal_full(msg_no, LOG_ERR, __FILE__, __LINE__, __func__, __VA_ARGS__)

void fatal_full(int, int, const char *, int, const char *,
	const char *, ...) GFLOG_PRINTF_ARG(6, 7);

enum gfsd_type {
	type_listener, type_client, type_back_channel, type_replication,
	type_write_verify_controller, type_write_verify,
};

void fd_event_notified(int, int, const char *, const char *);
void fd_event_notify(int);
gfarm_error_t connect_gfm_server(const char *);
void free_gfm_server(void);
pid_t do_fork(enum gfsd_type);
int open_data(char *, int);
char *gfsd_make_path(const char *, const char *);
char *gfsd_skip_spool_root(char *);
void gfsd_local_path(gfarm_ino_t, gfarm_uint64_t, const char *, char **);
void gfsd_local_path2(gfarm_ino_t, gfarm_uint64_t, const char *, char **,
	gfarm_ino_t, gfarm_uint64_t, const char *, char **);
int gfsd_create_ancestor_dir(char *);
gfarm_error_t gfsd_copy_file(int, gfarm_ino_t, gfarm_uint64_t, const char *,
	char **);
gfarm_error_t gfm_client_replica_lost(gfarm_ino_t, gfarm_uint64_t);
gfarm_error_t calc_digest(int, const char *, char *, size_t *, gfarm_off_t *,
	char *, size_t, const char *, gfarm_ino_t, gfarm_uint64_t);
void replica_lost_move_to_lost_found(gfarm_ino_t, gfarm_uint64_t, int, off_t);
#define TIMEDWAIT_INFINITE -1
int timedwait_2fds(int, int, time_t, const char *);
int timedwait_fd(int, time_t, const char *);
int fd_is_ready(int, const char *);
int wait_3fds(int, int, int, const char *);
int wait_2fds(int, int, const char *);
void wait_fd_with_failover_pipe(int, const char *);


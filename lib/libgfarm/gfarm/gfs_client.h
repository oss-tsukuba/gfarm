/*
 * $Id$
 */

struct sockaddr;
struct timeval;
struct gfarm_eventqueue;
struct gfs_connection;
struct gfs_stat;
enum gfarm_auth_method;
struct gfs_client_connect_state;

void gfs_client_terminate(void);

int gfs_client_connection_fd(struct gfs_connection *);
enum gfarm_auth_method gfs_client_connection_auth_method(
	struct gfs_connection *);
const char *gfs_client_hostname(struct gfs_connection *);

gfarm_error_t gfs_client_connection_acquire(const char *, int,
	struct sockaddr *, struct gfs_connection **);
void gfs_client_connection_free(struct gfs_connection *);
void gfs_client_connection_gc(void);

#if 0 /* XXX FIXME - disable multiplexed version for now */
gfarm_error_t gfs_client_connect_request_multiplexed(struct gfarm_eventqueue *,
	const char *, struct sockaddr *,
	void (*)(void *), void *,
	struct gfs_client_connect_state **);
gfarm_error_t gfs_client_connect_result_multiplexed(
	struct gfs_client_connect_state *, struct gfs_connection **);
#endif

/* from client */

gfarm_error_t gfs_client_process_set(struct gfs_connection *,
	gfarm_int32_t, size_t, const char *, gfarm_pid_t);
gfarm_error_t gfs_client_open(struct gfs_connection *, gfarm_int32_t);
gfarm_error_t gfs_client_open_local(struct gfs_connection *, gfarm_int32_t,
	int *);
gfarm_error_t gfs_client_close(struct gfs_connection *, gfarm_int32_t);
gfarm_error_t gfs_client_pread(struct gfs_connection *,
		       gfarm_int32_t, void *, size_t, gfarm_off_t, size_t *);
gfarm_error_t gfs_client_pwrite(struct gfs_connection *,
			gfarm_int32_t, const void *, size_t, gfarm_off_t,
			size_t *);
gfarm_error_t gfs_client_fstat(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t *,
	gfarm_int64_t *, gfarm_int32_t *,
	gfarm_int64_t *, gfarm_int32_t *);
gfarm_error_t gfs_client_cksum_set(struct gfs_connection *, gfarm_int32_t,
	const char *, size_t, const char *);
gfarm_error_t gfs_client_lock(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfs_client_trylock(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfs_client_unlock(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfs_client_lock_info(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t, gfarm_off_t, gfarm_int32_t, gfarm_int32_t,
	gfarm_off_t *, gfarm_off_t *, gfarm_int32_t *, char**, gfarm_pid_t **);
gfarm_error_t gfs_client_replica_add(struct gfs_connection *, gfarm_int32_t);

#define GFS_CLIENT_COMMAND_FLAG_STDIN_EOF	0x01
#define GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND	0x02
#define GFS_CLIENT_COMMAND_FLAG_XENVCOPY	0x10
#define GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY	0x20 /* copies env, too */
#define GFS_CLIENT_COMMAND_FLAG_X11MASK		0x30
#define GFS_CLIENT_COMMAND_EXITFLAG_COREDUMP	1
gfarm_error_t gfs_client_command_request(struct gfs_connection *,
				 char *, char **, char **, int, int *);
int gfs_client_command_is_running(struct gfs_connection *);
gfarm_error_t gfs_client_command_io(struct gfs_connection *, struct timeval *);
gfarm_error_t gfs_client_command_send_signal(struct gfs_connection *, int);
gfarm_error_t gfs_client_command_result(struct gfs_connection *,
				int *, int *, int *);
gfarm_error_t gfs_client_command(struct gfs_connection *,
			 char *, char **, char **, int,
			 int *, int *, int *);

/* from gfmd */

gfarm_error_t gfs_client_fhstat(struct gfs_connection *, gfarm_ino_t,
	struct gfs_stat *);
gfarm_error_t gfs_client_fhremove(struct gfs_connection *, gfarm_ino_t);

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

int gfs_client_connection_fd(struct gfs_connection *);
enum gfarm_auth_method gfs_client_connection_auth_method(
	struct gfs_connection *);
const char *gfs_client_hostname(struct gfs_connection *);
char *gfs_client_connection(const char *, struct sockaddr *,
	struct gfs_connection **);
char *gfs_client_connect(char *, struct sockaddr *,
	struct gfs_connection **);
char *gfs_client_connect_request_multiplexed(struct gfarm_eventqueue *,
	char *, struct sockaddr *,
	void (*)(void *), void *,
	struct gfs_client_connect_state **);
char *gfs_client_connect_result_multiplexed(struct gfs_client_connect_state *,
	struct gfs_connection **);

char *gfs_client_disconnect(struct gfs_connection *);
char *gfs_client_create(struct gfs_connection *, char *, gfarm_int32_t, int *);
char *gfs_client_open(struct gfs_connection *,
		      char *, gfarm_int32_t, gfarm_int32_t, gfarm_int32_t *);
char *gfs_client_close(struct gfs_connection *, gfarm_int32_t);
char *gfs_client_seek(struct gfs_connection *,
		      gfarm_int32_t, file_offset_t, gfarm_int32_t,
		      file_offset_t *);
char *gfs_client_read(struct gfs_connection *,
		      gfarm_int32_t, void *, size_t, size_t *);
char *gfs_client_write(struct gfs_connection *,
		       gfarm_int32_t, const void *, size_t, size_t *);
char *gfs_client_unlink(struct gfs_connection *, char *);
char *gfs_client_chdir(struct gfs_connection *, char *);
char *gfs_client_mkdir(struct gfs_connection *, char *, gfarm_int32_t);
char *gfs_client_rmdir(struct gfs_connection *, char *);
char *gfs_client_chmod(struct gfs_connection *, char *, gfarm_int32_t);
char *gfs_client_chgrp(struct gfs_connection *, char *, char *);
char *gfs_client_stat(struct gfs_connection *, char *);
char *gfs_client_exist(struct gfs_connection *, char *);
char *gfs_client_digest(struct gfs_connection *, int, char *, size_t,
			size_t *, unsigned char *, file_offset_t *);
char *gfs_client_get_spool_root(struct gfs_connection *, char **);

char *gfs_client_copyin(struct gfs_connection *, int, int, long);
char *gfs_client_striping_copyin_request(struct gfs_connection *, int, int,
	file_offset_t, file_offset_t, int, file_offset_t);
char *gfs_client_striping_copyin_partial(struct gfs_connection *, int *);
char *gfs_client_striping_copyin_result(struct gfs_connection *);
char *gfs_client_striping_copyin(struct gfs_connection *, int, int,
	file_offset_t, file_offset_t, int, file_offset_t);
char *gfs_client_replicate_file(struct gfs_connection *,
	char *, gfarm_int32_t, file_offset_t, char *, char *);

#define GFS_CLIENT_COMMAND_FLAG_STDIN_EOF	0x01
#define GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND	0x02
#define GFS_CLIENT_COMMAND_FLAG_XENVCOPY	0x10
#define GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY	0x20 /* copies env, too */
#define GFS_CLIENT_COMMAND_FLAG_X11MASK		0x30
#define GFS_CLIENT_COMMAND_EXITFLAG_COREDUMP	1
char *gfs_client_command_request(struct gfs_connection *,
				 char *, char **, char **, int, int *);
int gfs_client_command_is_running(struct gfs_connection *);
char *gfs_client_command_io(struct gfs_connection *, struct timeval *);
char *gfs_client_command_send_signal(struct gfs_connection *, int);
char *gfs_client_command_result(struct gfs_connection *,
				int *, int *, int *);
char *gfs_client_command(struct gfs_connection *,
			 char *, char **, char **, int,
			 int *, int *, int *);

/*
 * gfsd service on UDP port.
 */

extern int gfs_client_datagram_timeouts[]; /* milli seconds */
extern int gfs_client_datagram_ntimeouts;

struct gfs_client_load {
	double loadavg_1min, loadavg_5min, loadavg_15min;
};

char *gfs_client_get_load_request(int, struct sockaddr *, int);
char *gfs_client_get_load_result(int, struct sockaddr *, int *,
	struct gfs_client_load *);

struct gfs_client_get_load_state;
char *gfs_client_get_load_request_multiplexed(struct gfarm_eventqueue *,
	struct sockaddr *,
	void (*)(void *), void *,
	struct gfs_client_get_load_state **);
char *gfs_client_get_load_result_multiplexed(
	struct gfs_client_get_load_state *, struct gfs_client_load *);

char *gfs_client_apply_all_hosts(
	char *(*)(struct gfs_connection *, void *), void *, char *, int *);

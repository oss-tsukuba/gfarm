/*
 * $Id$
 */

struct sockaddr;
struct timeval;
struct gfs_connection;
struct gfs_stat;

int gfs_client_connection_fd(struct gfs_connection *);
char *gfs_client_connection(const char *, struct sockaddr *,
	struct gfs_connection **);
char *gfs_client_connect(char *, struct sockaddr *,
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
char *gfs_client_stat(struct gfs_connection *, struct gfs_stat *);
char *gfs_client_digest(struct gfs_connection *, int, char *, size_t,
			size_t *, unsigned char *, file_offset_t *);
char *gfs_client_get_spool_root(struct gfs_connection *, char **);

char *gfs_client_copyin(struct gfs_connection *, int, int);
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

#define GFS_CLIENT_TIMEOUT_INDEFINITE	-1

struct gfs_client_load {
	double loadavg_1min, loadavg_5min, loadavg_15min;
};

char *gfs_client_get_load_request(int, struct sockaddr *, int);
char *gfs_client_get_load_result(int, struct sockaddr *, int *,
	struct gfs_client_load *);

struct gfs_client_udp_requests;

char *gfarm_client_init_load_requests(int, struct gfs_client_udp_requests **);
char *gfarm_client_wait_all_load_results(struct gfs_client_udp_requests *);
char *gfarm_client_add_load_request(struct gfs_client_udp_requests *,
	struct sockaddr *, void *,
	void (*)(void *, struct sockaddr *, struct gfs_client_load *, char *));

char *gfs_client_apply_all_hosts(
	char *(*)(struct gfs_connection *, void *), void *, char *, int *);

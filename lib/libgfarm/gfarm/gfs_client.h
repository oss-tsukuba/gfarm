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
char *gfs_client_link(struct gfs_connection *, char *, char *);
char *gfs_client_unlink(struct gfs_connection *, char *);
char *gfs_client_rename(struct gfs_connection *, char *, char *);
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

/* old interface. now mainly used for bootstrap. the followings */
char *gfs_client_bootstrap_replicate_file(struct gfs_connection *,
	char *, gfarm_int32_t, file_offset_t, char *, char *);

/* old interface. now mainly used for bootstrap. these are only used by gfsd */
char *gfs_client_copyin(struct gfs_connection *, int, int, long);
char *gfs_client_striping_copyin_request(struct gfs_connection *, int, int,
	file_offset_t, file_offset_t, int, file_offset_t);
char *gfs_client_striping_copyin_partial(struct gfs_connection *, int *);
char *gfs_client_striping_copyin_result(struct gfs_connection *);
char *gfs_client_striping_copyin(struct gfs_connection *, int, int,
	file_offset_t, file_offset_t, int, file_offset_t);

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
 * replicateion service
 */

#define GFS_CLIENT_REP_ALGORITHM_LATEST	1

int gfs_client_rep_limit_division(int, int, file_offset_t);

struct gfs_client_rep_transfer_state;

char *gfs_client_rep_transfer_state_alloc(file_offset_t, int, int, int,
	struct gfs_client_rep_transfer_state ***);
void gfs_client_rep_transfer_state_free(int,
	struct gfs_client_rep_transfer_state **);

int gfs_client_rep_transfer_finished(struct gfs_client_rep_transfer_state *);
size_t gfs_client_rep_transfer_length(struct gfs_client_rep_transfer_state *,
	size_t);
file_offset_t gfs_client_rep_transfer_offset(
	struct gfs_client_rep_transfer_state *);
void gfs_client_rep_transfer_progress(
	struct gfs_client_rep_transfer_state *, size_t);

int gfs_client_rep_transfer_stripe_finished(
	struct gfs_client_rep_transfer_state *);
int gfs_client_rep_transfer_stripe_offset(
	struct gfs_client_rep_transfer_state *);
void gfs_client_rep_transfer_stripe_progress(
	struct gfs_client_rep_transfer_state *, size_t);
void gfs_client_rep_transfer_stripe_next(
	struct gfs_client_rep_transfer_state *);

struct xxx_connection;
struct gfarm_stringlist;
struct gfs_client_rep_backend_state;
char *gfs_client_rep_backend_invoke(char *, char *, char *, char *,
	int, int, int, int, int, int, char *,
	struct xxx_connection **, struct xxx_connection **,
	struct gfs_client_rep_backend_state **);
char *gfs_client_rep_backend_kill(struct gfs_client_rep_backend_state *);
char *gfs_client_rep_filelist_send(char *, struct xxx_connection *,
	char *, int, struct gfarm_stringlist *, struct gfarm_stringlist *);
char *gfs_client_rep_filelist_receive(struct xxx_connection *,
	int *, struct gfarm_stringlist *, struct gfarm_stringlist *, char *);

char *gfarm_file_section_replicate_multiple_request(
	struct gfarm_stringlist *, struct gfarm_stringlist *, char *, char *,
	struct gfs_client_rep_backend_state **);
char *gfarm_file_section_replicate_multiple_result(
	struct gfs_client_rep_backend_state *, char **);
char *gfarm_file_section_replicate_multiple(
	struct gfarm_stringlist *, struct gfarm_stringlist *, char *, char *,
	char **);

/* really experimental. Don't assume this works well. */

struct gfs_client_rep_rate_info;
struct gfs_client_rep_rate_info *gfs_client_rep_rate_info_alloc(long);
void gfs_client_rep_rate_control(struct gfs_client_rep_rate_info *, long);
void gfs_client_rep_rate_info_free(struct gfs_client_rep_rate_info *);

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
	char *(*)(struct gfs_connection *, void *), void *, char *, int,
	int *);

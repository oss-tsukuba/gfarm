/*
 * $Id$
 */

#define GFS_CONN_RETRY_COUNT	1	/* retry for gfsd connection error */

struct sockaddr;
struct timeval;
struct gfarm_eventqueue;
struct gfs_connection;
struct gfs_stat;
enum gfarm_auth_method;
struct gfs_client_connect_state;

void gfs_client_terminate(void);

void gfs_client_add_hook_for_connection_error(
	gfarm_error_t (*)(struct gfs_connection *));
int gfs_client_is_connection_error(gfarm_error_t);

int gfs_client_connection_fd(struct gfs_connection *);
enum gfarm_auth_method gfs_client_connection_auth_method(
	struct gfs_connection *);
const char *gfs_client_hostname(struct gfs_connection *);
const char *gfs_client_username(struct gfs_connection *);
int gfs_client_port(struct gfs_connection *);
gfarm_pid_t gfs_client_pid(struct gfs_connection *);
void gfs_client_purge_from_cache(struct gfs_connection *);
int gfs_client_connection_failover_count(struct gfs_connection *);
void gfs_client_connection_set_failover_count(struct gfs_connection *, int);

gfarm_error_t gfs_client_connection_acquire(const char *, const char *,
	struct sockaddr *, struct gfs_connection **);
struct gfm_connection; /* XXX */
gfarm_error_t gfs_client_connection_acquire_by_host(
	struct gfm_connection *, const char *, /* XXX */
	int, struct gfs_connection **, const char *);
gfarm_error_t gfs_client_connection_and_process_acquire(
	struct gfm_connection **, const char *,
	int, struct gfs_connection **, const char *);
void gfs_client_connection_free(struct gfs_connection *);
gfarm_error_t gfs_client_connect(const char *, int, const char *,
	struct sockaddr *, struct gfs_connection **);
void gfs_client_connection_gc(void);
int gfs_client_sockaddr_is_local(struct sockaddr *);
int gfs_client_connection_is_local(struct gfs_connection *);

gfarm_error_t gfs_client_connection_enter_cache(struct gfs_connection *);
gfarm_error_t gfs_client_connection_enter_cache_tail(struct gfs_connection *);

struct gfarm_filesystem;
gfarm_error_t gfs_client_connect_request_multiplexed(
	struct gfarm_eventqueue *, const char *, int, const char *,
	struct sockaddr *, struct gfarm_filesystem *fs, int,
	void (*)(void *), void *, struct gfs_client_connect_state **);
gfarm_error_t gfs_client_connect_result_multiplexed(
	struct gfs_client_connect_state *,
	struct gfs_connection **);

/* from client */

gfarm_error_t gfs_client_process_set(struct gfs_connection *, gfarm_int32_t,
	const char *, size_t, gfarm_pid_t);
gfarm_error_t gfs_client_process_reset(struct gfs_connection *, gfarm_int32_t,
	const char *, size_t, gfarm_pid_t);
gfarm_error_t gfs_client_open(struct gfs_connection *, gfarm_int32_t);
gfarm_error_t gfs_client_open_local(struct gfs_connection *, gfarm_int32_t,
	int *);
gfarm_error_t gfs_client_close(struct gfs_connection *, gfarm_int32_t);
gfarm_error_t gfs_client_close_write(struct gfs_connection *,
	gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfs_client_pread(struct gfs_connection *,
			gfarm_int32_t, void *, size_t, gfarm_off_t, size_t *);
gfarm_error_t gfs_client_pwrite(struct gfs_connection *,
			gfarm_int32_t, const void *, size_t, gfarm_off_t,
			size_t *);
gfarm_error_t gfs_client_write(struct gfs_connection *,
			gfarm_int32_t, const void *, size_t,
			size_t *, gfarm_off_t *, gfarm_off_t *);
gfarm_error_t gfs_client_ftruncate(struct gfs_connection *,
	gfarm_int32_t, gfarm_off_t);
gfarm_error_t gfs_client_fsync(struct gfs_connection *,
	gfarm_int32_t, gfarm_int32_t);
gfarm_error_t gfs_client_fstat(struct gfs_connection *, gfarm_int32_t,
	gfarm_off_t *,
	gfarm_int64_t *, gfarm_int32_t *,
	gfarm_int64_t *, gfarm_int32_t *);
gfarm_error_t gfs_client_cksum(struct gfs_connection *,
	gfarm_int32_t, const char *, char *, size_t, size_t *);
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
gfarm_error_t gfs_client_replica_add_from(struct gfs_connection *,
	char *, gfarm_int32_t, gfarm_int32_t);
#ifdef GFARM_USE_OPENSSL /* this requires <openssl/evp.h> */
gfarm_error_t gfs_client_replica_recv_cksum_md(struct gfs_connection *,
	gfarm_int32_t *, gfarm_int32_t *,
	gfarm_ino_t, gfarm_uint64_t, gfarm_int64_t,
	const char *, size_t, const char *, gfarm_int32_t,
	size_t, size_t *, char *, gfarm_int32_t *,
	int, EVP_MD_CTX *);
gfarm_error_t gfs_client_replica_recv_md(struct gfs_connection *,
	gfarm_int32_t *, gfarm_int32_t *,
	gfarm_ino_t, gfarm_uint64_t, int, EVP_MD_CTX *);
#endif
gfarm_error_t gfs_client_statfs(struct gfs_connection *, char *,
	gfarm_int32_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);

struct gfs_client_statfs_state;
gfarm_error_t gfs_client_statfs_request_multiplexed(struct gfarm_eventqueue *,
	struct gfs_connection *, char *, int, void (*)(void *), void *,
	struct gfs_client_statfs_state **);
gfarm_error_t gfs_client_statfs_result_multiplexed(
	struct gfs_client_statfs_state *,
	gfarm_int32_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *,
	gfarm_off_t *, gfarm_off_t *, gfarm_off_t *);

#ifdef GFARM_USE_OPENSSL /* this requires <openssl/evp.h> */
gfarm_error_t gfs_client_sendfile(struct gfs_connection *,
	gfarm_int32_t, gfarm_off_t, int, gfarm_off_t, gfarm_off_t,
	EVP_MD_CTX *, gfarm_off_t *);
gfarm_error_t gfs_client_recvfile(struct gfs_connection *,
	gfarm_int32_t, gfarm_off_t, int, gfarm_off_t, gfarm_off_t,
	int, EVP_MD_CTX *, int *, gfarm_off_t *);

/* commonly used by both clients and gfsd */
struct gfp_xdr;
gfarm_error_t gfs_sendfile_common(struct gfp_xdr *, gfarm_int32_t *,
	int, gfarm_off_t, gfarm_off_t, EVP_MD_CTX *, gfarm_off_t *);
gfarm_error_t gfs_recvfile_common(struct gfp_xdr *, gfarm_int32_t *,
	int, gfarm_off_t, int, EVP_MD_CTX *, int *, gfarm_off_t *);
#endif

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
int gfs_client_connection_cache_change(int);

/* from gfmd */

gfarm_error_t gfs_client_fhstat(struct gfs_connection *, gfarm_ino_t,
	struct gfs_stat *);
gfarm_error_t gfs_client_fhremove(struct gfs_connection *, gfarm_ino_t);

/*
 * gfsd service on UDP port.
 */

extern int gfs_client_datagram_timeouts[]; /* milli seconds */
extern int gfs_client_datagram_ntimeouts;

struct gfs_client_load {
	double loadavg_1min, loadavg_5min, loadavg_15min;
};

gfarm_error_t gfs_client_get_load_request(int, struct sockaddr *, int);
gfarm_error_t gfs_client_get_load_result(int, struct sockaddr *, socklen_t *,
	struct gfs_client_load *);

struct gfs_client_get_load_state;
gfarm_error_t gfs_client_get_load_request_multiplexed(
	struct gfarm_eventqueue *, struct sockaddr *,
	void (*)(void *), void *, struct gfs_client_get_load_state **, int);
gfarm_error_t gfs_client_get_load_result_multiplexed(
	struct gfs_client_get_load_state *, struct gfs_client_load *);

void gfs_client_connection_lock(struct gfs_connection *gfs_server);
void gfs_client_connection_unlock(struct gfs_connection *gfs_server);

struct gfs_ib_rdma_state;
gfarm_error_t gfs_ib_rdma_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfs_connection *gfs_server, int, void (*continuation)(void *),
	void *closure, struct gfs_ib_rdma_state **statepp);
gfarm_error_t gfs_ib_rdma_result_multiplexed(struct gfs_ib_rdma_state *state);

gfarm_error_t gfs_client_exch_rdma_info(struct gfs_connection *);
struct rdma_context * gfs_ib_rdma_context(struct gfs_connection *);
gfarm_error_t gfs_ib_rdma_pread(struct gfs_connection *,
	gfarm_int32_t, void *, size_t, gfarm_off_t, size_t *, gfarm_uint32_t);
gfarm_error_t gfs_ib_rdma_pwrite(struct gfs_connection *, gfarm_int32_t,
	const void *, size_t, gfarm_off_t, size_t *, gfarm_uint32_t);


#define _POSIX_PII_SOCKET /* to use struct msghdr on Tru64 */
#include <pthread.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h> /* sprintf() */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <openssl/evp.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_iostat.h>

#if defined(SCM_RIGHTS) && \
		(!defined(sun) || (!defined(__svr4__) && !defined(__SVR4)))
#define HAVE_MSG_CONTROL 1
#endif

#include "gfutil.h"
#include "fdutil.h"
#include "gfevent.h"
#include "hash.h"
#include "lru_cache.h"
#include "thrsubr.h"

#include "context.h"
#include "liberror.h"
#include "sockutil.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "host_address.h"
#include "host.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "conn_cache.h"
#include "gfs_proto.h"
#define GFARM_USE_OPENSSL
#include "gfs_client.h"
#include "gfm_client.h"
#include "filesystem.h"
#include "gfs_failover.h"
#include "iostat.h"
#ifdef __KERNEL__
#undef HAVE_INFINIBAND
#include "nanosec.h"
#include "gfsk_fs.h"
#endif /* __KERNEL__ */

#include "gfs_rdma.h"

#define GFS_CLIENT_COMMAND_TIMEOUT	20 /* seconds */

#define XAUTH_NEXTRACT_MAXLEN	512

struct gfs_connection {
	struct gfp_cached_connection *cache_entry;

	struct gfp_xdr *conn;
	char *hostname;
	int port;
	enum gfarm_auth_method auth_method;

	int is_local;
	gfarm_pid_t pid; /* parallel process ID */

	int opened; /* reference counter */

	void *context; /* work area for RPC (esp. GFS_PROTO_COMMAND) */

	int failover_count; /* compare to gfm_connection.failover_count */

#ifdef HAVE_INFINIBAND
	struct rdma_context *rdma_ctx; /* for client-gfsd rdma */
#endif
};

#define staticp	(gfarm_ctxp->gfs_client_static)

struct gfs_client_static {
	struct gfp_conn_cache server_cache;
	gfarm_error_t (*hook_for_connection_error)(struct gfs_connection *);
};

#define SERVER_HASHTAB_SIZE	3079	/* prime number */

static gfarm_error_t gfs_client_connection_dispose(void *);
#ifdef HAVE_INFINIBAND
static void gfs_ib_rdma_connect(struct gfs_connection *gfs_server);
#endif
static void gfs_ib_rdma_free(struct gfs_connection *gfs_server);

gfarm_error_t
gfs_client_static_init(struct gfarm_context *ctxp)
{
	struct gfs_client_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	gfp_conn_cache_init(&s->server_cache,
		gfs_client_connection_dispose,
		"gfs_connection",
		SERVER_HASHTAB_SIZE,
		&ctxp->gfsd_connection_cache);
	s->hook_for_connection_error = NULL;

	ctxp->gfs_client_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_client_static_term(struct gfarm_context *ctxp)
{
	struct gfs_client_static *s = ctxp->gfs_client_static;

	if (s == NULL)
		return;

	gfp_conn_cache_term(&s->server_cache);

	free(s);
	ctxp->gfs_client_static = NULL;
}

/*
 * Currently this supports only one hook function.
 * And the function is always gfarm_schedule_host_cache_clear_auth() (or NULL).
 */
void
gfs_client_add_hook_for_connection_error(
	gfarm_error_t (*hook)(struct gfs_connection *))
{
	staticp->hook_for_connection_error = hook;
}

static void
gfs_client_execute_hook_for_connection_error(struct gfs_connection *gfs_server)
{
	if (staticp->hook_for_connection_error != NULL)
		(*staticp->hook_for_connection_error)(gfs_server);
}

int
gfs_client_is_connection_error(gfarm_error_t e)
{
	return (IS_CONNECTION_ERROR(e));
}

int
gfs_client_connection_fd(struct gfs_connection *gfs_server)
{
	return (gfp_xdr_fd(gfs_server->conn));
}

enum gfarm_auth_method
gfs_client_connection_auth_method(struct gfs_connection *gfs_server)
{
	return (gfs_server->auth_method);
}

const char *
gfs_client_hostname(struct gfs_connection *gfs_server)
{
	return (gfs_server->hostname);
}

const char *
gfs_client_username(struct gfs_connection *gfs_server)
{
	return (gfp_cached_connection_username(gfs_server->cache_entry));
}

int
gfs_client_port(struct gfs_connection *gfs_server)
{
	return (gfs_server->port);
}

int
gfs_client_connection_is_local(struct gfs_connection *gfs_server)
{
	return (gfs_server->is_local);
}

gfarm_pid_t
gfs_client_pid(struct gfs_connection *gfs_server)
{
	return (gfs_server->pid);
}

int
gfs_client_connection_failover_count(struct gfs_connection *gfs_server)
{
	return (gfs_server->failover_count);
}

void
gfs_client_connection_set_failover_count(
	struct gfs_connection *gfs_server, int count)
{
	gfs_server->failover_count = count;
}

#define gfs_client_connection_is_cached(gfs_server) \
	gfp_is_cached_connection((gfs_server)->cache_entry)

#define gfs_client_connection_used(gfs_server) \
	gfp_cached_connection_used(&staticp->server_cache, \
	    (gfs_server)->cache_entry)

void
gfs_client_purge_from_cache(struct gfs_connection *gfs_server)
{
	gfp_cached_connection_purge_from_cache(&staticp->server_cache,
	    gfs_server->cache_entry);
}

void
gfs_client_connection_gc(void)
{
	gfp_cached_connection_gc_all(&staticp->server_cache);
}

#ifndef __KERNEL__	/* gfs_client_connect_xxx :: in user mode */
static gfarm_error_t
gfs_client_connect_unix(struct sockaddr *peer_addr, socklen_t peer_addrlen,
	int port, int *sockp)
{
	int rv, sock, save_errno;
	struct sockaddr_un peer_un;
	socklen_t socklen;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
		gfs_client_connection_gc(); /* XXX FIXME: GC all descriptors */
		sock = socket(PF_UNIX, SOCK_STREAM, 0);
	}
	if (sock == -1) {
		save_errno = errno;
		gflog_debug(GFARM_MSG_1001172,
			"creation of UNIX socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	gfs_sockaddr_to_local_addr(peer_addr, peer_addrlen, port,
	    &peer_un, NULL);
	socklen = sizeof(peer_un);
	rv = connect(sock, (struct sockaddr *)&peer_un, socklen);
	if (rv == -1) {
		/*
		 * if errno == ENOENT,
		 * it's older gfsd which doesn't support UNIX connection
		 */
		save_errno = errno;
		close(sock);
		gflog_debug(GFARM_MSG_1001173,
		    "connect() with UNIX socket failed: %s",
		    strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	*sockp = sock;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connect_inet(const char *canonical_hostname,
	struct sockaddr *peer_addr, socklen_t peer_addr_len,
	const char *source_ip,
	int *connection_in_progress_p, int *sockp)
{
	return (gfarm_nonblocking_connect(peer_addr->sa_family, SOCK_STREAM, 0,
	    peer_addr, peer_addr_len, canonical_hostname, source_ip,
	    gfs_client_connection_gc /* XXX FIXME: GC all descriptors */,
	    connection_in_progress_p, sockp));
}

/* NOTE: port member of peer_addr should match the port argument */
static gfarm_error_t
gfs_client_connect_addr(const char *canonical_hostname, int port,
	struct sockaddr *peer_addr, socklen_t peer_addrlen,
	const char *source_ip,
	int *connection_in_progress_p, int *is_local_p, int *sockp)
{
	gfarm_error_t e;
	int connection_in_progress = 0, sock = -1;

#ifdef HAVE_GSI
	/* prevent to connect servers with expired client credential */
	e = gfarm_auth_check_gsi_auth_error();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
#endif
	if (gfarm_ctxp->direct_local_access &&
	    gfarm_sockaddr_is_local(peer_addr)) {
		e = gfs_client_connect_unix(peer_addr, peer_addrlen, port,
		    &sock);
		if (e == GFARM_ERR_NO_ERROR) {
			*connection_in_progress_p = 0;
			*is_local_p = 1;
			*sockp = sock;
			return (GFARM_ERR_NO_ERROR);
		}
		gflog_debug(GFARM_MSG_1001177,
		    "connect with unix socket failed: %s",
		    gfarm_error_string(e));
	}
	e = gfs_client_connect_inet(canonical_hostname,
	    peer_addr, peer_addrlen, source_ip,
	    &connection_in_progress, &sock);
	if (e == GFARM_ERR_NO_ERROR) {
		*connection_in_progress_p = connection_in_progress;
		*is_local_p = 0;
		*sockp = sock;
		return (GFARM_ERR_NO_ERROR);
	}
	gflog_debug(GFARM_MSG_1001178,
	    "connect with inet socket failed: %s",
	    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfs_client_connect_host(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port,
	const char *source_ip,
	struct sockaddr_storage *ss, socklen_t *ss_lenp,
	int *connection_in_progress_p, int *is_local_p, int *sockp)
{
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	int connection_in_progress = 0, is_local = 0, sock = -1, addr_count, i;
	struct gfarm_host_address **addr_array;

	e = gfm_host_address_get(gfm_server, canonical_hostname, port,
	    &addr_count, &addr_array);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	for (i = 0; i < addr_count; i++) {
		e = gfs_client_connect_addr(canonical_hostname, port,
		    &addr_array[i]->sa_addr, addr_array[i]->sa_addrlen,
		    source_ip,
		    &connection_in_progress, &is_local, &sock);
		if (e == GFARM_ERR_NO_ERROR) {
			memcpy(ss, &addr_array[i]->sa_addr,
			    addr_array[i]->sa_addrlen);
			*ss_lenp = addr_array[i]->sa_addrlen;
			break;
		}
	}
	gfarm_host_address_free(addr_count, addr_array);
	if (sock == -1)
		return (e);

	*connection_in_progress_p = connection_in_progress;
	*is_local_p = is_local;
	*sockp = sock;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connection_alloc(const char *canonical_hostname, int port,
	int failover_count, struct gfp_cached_connection *cache_entry,
	int is_local, int sock, struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfs_connection *gfs_server;

	GFARM_MALLOC(gfs_server);
	if (gfs_server == NULL) {
		gflog_debug(GFARM_MSG_1001179,
			"allocation of 'gfs_server' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfp_xdr_new_socket(sock, &gfs_server->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001180,
			"gfp_xdr_new_socket() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gfs_server->hostname = strdup(canonical_hostname);
	if (gfs_server->hostname == NULL) {
		e = gfp_xdr_free(gfs_server->conn);
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001181,
			"allocation of 'gfs_server->hostname' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

#ifdef HAVE_INFINIBAND
	/* RDMA */
	if (!is_local)
		gfs_rdma_init(0, &gfs_server->rdma_ctx);
	else
		gfs_server->rdma_ctx = NULL;
#endif

	gfs_server->port = port;
	gfs_server->is_local = is_local;
	gfs_server->pid = 0;
	gfs_server->context = NULL;
	gfs_server->opened = 0;
	gfs_server->failover_count = failover_count;

	gfs_server->cache_entry = cache_entry;
	gfp_cached_connection_set_data(cache_entry, gfs_server);

	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_connection_alloc_and_auth(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port, const char *user,
	const char *source_ip, int failover_count,
	struct gfp_cached_connection *cache_entry,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	int save_errno, connection_in_progress = 0, is_local = 0, sock = -1;
	struct gfs_connection *gfs_server;
	struct sockaddr_storage ss;
	socklen_t ss_len;

	e = gfs_client_connect_host(gfm_server, canonical_hostname, port,
	    source_ip, &ss, &ss_len,
	    &connection_in_progress, &is_local, &sock);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "gfs_client_connect_host() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	e = gfs_client_connection_alloc(canonical_hostname, port,
	    failover_count, cache_entry, is_local, sock, &gfs_server);
	if (e != GFARM_ERR_NO_ERROR) {
		close(sock);
		gflog_debug(GFARM_MSG_1001182,
			"gfs_client_connection_alloc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (connection_in_progress) {
		save_errno = gfarm_fd_wait(gfp_xdr_fd(gfs_server->conn), 0, 1,
		    gfarm_ctxp->gfsd_connection_timeout, canonical_hostname);
		e = gfarm_errno_to_error(save_errno);
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_auth_request(gfs_server->conn, GFS_SERVICE_TAG,
		    gfs_server->hostname, (struct sockaddr *)&ss,
		     gfarm_get_auth_id_type(), user, NULL,
		    &gfs_server->auth_method);
	if (e == GFARM_ERR_NO_ERROR) {
#ifdef HAVE_INFINIBAND
		if (!gfs_server->is_local) {
			/* not __KERNEL__, no need to lock */
			gfs_ib_rdma_connect(gfs_server);
		}
#endif
		*gfs_serverp = gfs_server;
	} else {
		free(gfs_server->hostname);
		gfp_xdr_free(gfs_server->conn);
		gfs_ib_rdma_free(gfs_server);
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001183,
			"connection or authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}
#else /* __KERNEL__ */
static gfarm_error_t
gfsk_client_connection_alloc_and_auth(struct gfarm_eventqueue *q,
	const char *canonical_hostname, const char *user,
	const char *source_ip, int failover_count,
	struct gfp_cached_connection *cache_entry,
	void **kevpp, struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	int	err;
	struct gfs_connection *gfs_server;
	int sock = -1, is_local = 0;
	int fd = -1;

	if (gfs_client_sockaddr_is_local(peer_addr)) {
		is_local = 1;
	}

	GFARM_MALLOC(gfs_server);
	if (gfs_server == NULL) {
		gflog_debug(GFARM_MSG_1001179,
			"allocation of 'gfs_server' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	gfs_server->hostname = strdup(canonical_hostname);
	if (gfs_server->hostname == NULL) {
		free(gfs_server);
		gflog_debug(GFARM_MSG_1001181,
			"allocation of 'gfs_server->hostname' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	if (kevpp)
		fd = gfarm_kern_eventqueue_getevfd(q);

	if ((err = gfsk_gfsd_connect(canonical_hostname, peer_addr,
		source_ip, user, &sock, kevpp, fd))) {
		err = -err;
		gflog_debug(GFARM_MSG_1003894,
			"gfsk_gfsd_connect error :%s", strerror(err));
		free(gfs_server->hostname);
		free(gfs_server);
		return (gfarm_errno_to_error(err));
	}
	if (!kevpp) {
		e = gfp_xdr_new_socket(sock, &gfs_server->conn);
		if (e != GFARM_ERR_NO_ERROR) {
			free(gfs_server->hostname);
			free(gfs_server);
			close(sock);
			gflog_debug(GFARM_MSG_1001180,
				"gfp_xdr_new_socket() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
	} else
		gfs_server->conn = NULL;
	/* XXX IPv6 */
	gfs_server->port = ntohs(((struct sockaddr_in *)peer_addr)->sin_port);
	gfs_server->is_local = is_local;
	gfs_server->pid = 0;
	gfs_server->context = NULL;
	gfs_server->opened = 0;
	gfs_server->failover_count = failover_count;

	gfs_server->cache_entry = cache_entry;
	gfp_cached_connection_set_data(cache_entry, gfs_server);

	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}
static gfarm_error_t
gfs_client_connection_alloc_and_auth(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port, const char *user,
	const char *source_ip, int failover_count,
	struct gfp_cached_connection *cache_entry,
	struct gfs_connection **gfs_serverp)
{
	return (gfsk_client_connection_alloc_and_auth(NULL, canonical_hostname,
	    port, user, source_ip, failover_count, cache_entry,
	    NULL, gfs_serverp));
}
#endif /* __KERNEL__ */

static gfarm_error_t
gfs_client_connection_dispose(void *connection_data)
{
	struct gfs_connection *gfs_server = connection_data;
	gfarm_error_t e = gfp_xdr_free(gfs_server->conn);

	gfp_uncached_connection_dispose(gfs_server->cache_entry);
	free(gfs_server->hostname);
	gfs_ib_rdma_free(gfs_server);
	/* XXX - gfs_server->context should be NULL here */
	free(gfs_server);
	return (e);
}

/*
 * gfs_client_connection_free() can be used for both
 * an uncached connection which was created by gfs_client_connect(), and
 * a cached connection which was created by gfs_client_connection_acquire().
 * The connection will be immediately closed in the former uncached case.
 *
 */
void
gfs_client_connection_free(struct gfs_connection *gfs_server)
{
	gfp_cached_or_uncached_connection_free(&staticp->server_cache,
	    gfs_server->cache_entry);
}

void
gfs_client_terminate(void)
{
	gfp_cached_connection_terminate(&staticp->server_cache);
}
void
gfs_client_connection_unlock(struct gfs_connection *gfs_server)
{
	gfp_connection_unlock(gfs_server->cache_entry);
}
void
gfs_client_connection_lock(struct gfs_connection *gfs_server)
{
	gfp_connection_lock(gfs_server->cache_entry);
}


/*
 * gfs_client_connection_acquire - create or lookup a cached connection
 *
 * NOTE: the caller should check gfmd failover
 */
gfarm_error_t
gfs_client_connection_acquire(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port, const char *source_ip,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;
	int created;
	const char *user = gfm_client_username(gfm_server);

#ifdef __KERNEL__
retry:
#endif
	/*
	 * lookup gfs_server_cache first,
	 * to eliminate hostname -> IP-address conversion in a cached case.
	 */
	e = gfp_cached_connection_acquire(&staticp->server_cache,
	    canonical_hostname, port, user, &cache_entry, &created);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001184,
			"acquirement of cached connection (%s) failed: %s",
			canonical_hostname,
			gfarm_error_string(e));
		return (e);
	}
	if (!created) {
		*gfs_serverp = gfp_cached_connection_get_data(cache_entry);
#ifdef __KERNEL__	/* workaround for race condition in MT */
		if (*gfs_serverp == NULL) {
			gflog_warning(GFARM_MSG_1003895,
				"gfs_client_connection_acquire:"
				"%s not connected", canonical_hostname);
			gfp_cached_or_uncached_connection_free(
				&staticp->server_cache, cache_entry);
			gfarm_nanosleep(10 * 1000 * 1000);
			goto retry;
#endif /* __KERNEL__ */
		return (GFARM_ERR_NO_ERROR);
	}
	e = gfs_client_connection_alloc_and_auth(gfm_server,
	    canonical_hostname, port, user, source_ip,
	    gfarm_filesystem_failover_count(
		gfarm_filesystem_get_by_connection(gfm_server)),
	    cache_entry, gfs_serverp);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_cached_connection_purge_from_cache(&staticp->server_cache,
		    cache_entry);
		gfp_uncached_connection_dispose(cache_entry);
		gflog_debug(GFARM_MSG_1001185,
			"allocation or authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

/*
 * It is possible for gfmd failover not to be detected
 * before acquiring gfs_connection.
 * If gfmd failover is not detected, pid is already
 * invalidated and pid must be reallocated.
 */
static gfarm_error_t
gfs_client_gfmd_failover_at_connect(
	struct gfm_connection **gfm_serverp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server_new, *gfm_server = *gfm_serverp;
	char *gfmd_host = strdup(gfm_client_hostname(gfm_server));
	int gfmd_port = gfm_client_port(gfm_server);
	char *gfmd_user = strdup(gfm_client_username(gfm_server));

	if (gfmd_host == NULL || gfmd_user == NULL) {
		gflog_debug(GFARM_MSG_1003897,
		    "strdup() failed for gfmd host/user");
		free(gfmd_host);
		free(gfmd_user);
		return (GFARM_ERR_NO_MEMORY);
	}
	if ((e = gfm_client_connection_failover(gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003898,
		    "gfm_client_connection_failover: %s",
		    gfarm_error_string(e));
		free(gfmd_host);
		free(gfmd_user);
		return (e);
	}
	e = gfm_client_connection_and_process_acquire(
	    gfmd_host, gfmd_port, gfmd_user, &gfm_server_new);
	free(gfmd_host);
	free(gfmd_user);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003899,
		    "gfm_client_connection_and_process_acquire: %s",
		    gfarm_error_string(e));
		return (e);
	}
	gfm_client_connection_free(gfm_server);
	/*
	 * if gfm_server is related to GFS_File,
	 * gfm_serverp is set to new gfm_conntion.
	 */
	if (gfm_server_new != gfm_server)
		*gfm_serverp = gfm_server_new;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfs_client_check_failovercount_or_reset_process(
	struct gfs_connection *gfs_server, struct gfm_connection *gfm_server)
{
	gfarm_error_t e;
	struct gfarm_filesystem *fs = gfarm_filesystem_get_by_connection(
		gfm_server);
	int fc = gfarm_filesystem_failover_count(fs);
	int old_fc = gfs_server->failover_count;

	if (old_fc == fc)
		return (GFARM_ERR_NO_ERROR);
	gflog_debug(GFARM_MSG_1003900,
	    "detected gfmd connection failover before acquring "
	    "gfsd connection");
	/*
	 * if cached gfs_connection is not related to GFS_File when failover
	 * occurred, gfs_connection is not failed over.
	 * such connection must be called gfarm_client_process_reset() before
	 * being related to GFS_File.
	 */
	gfs_server->failover_count = fc;
	/*
	 * gfs_server->failover_count must be new value because
	 * failover_count will be passed to gfsd in
	 * gfarm_client_process_reset()
	 */
	if ((e = gfarm_client_process_reset(gfs_server, gfm_server))
	    != GFARM_ERR_NO_ERROR) {
		gfs_server->failover_count = old_fc;
		gflog_debug(GFARM_MSG_1003901,
		    "gfarm_client_process_reset: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

/*
 * Callers of this function should
 * acquire (or addref) *gfm_serverp
 *	before calling this,
 * and free (or delref) against returned *gfm_serverp
 * 	(this may be different with the value which was acquired/addref-ed)
 * 	after calling this.
 */
gfarm_error_t
gfs_client_connection_and_process_acquire(
	struct gfm_connection **gfm_serverp,
	const char *canonical_hostname, int port,
	struct gfs_connection **gfs_serverp, const char *source_ip)
{
	gfarm_error_t e;
	struct gfs_connection *gfs_server;
	int gfsd_nretries = GFS_CONN_RETRY_COUNT;
	int failover_nretries = GFS_FAILOVER_RETRY_COUNT;

	for (;;) {
		for (;;) {
			e = gfs_client_connection_acquire(
			    *gfm_serverp, canonical_hostname, port, source_ip,
			    &gfs_server);
			if (e == GFARM_ERR_NO_ERROR)
				break;
			if (!gfm_client_connection_should_failover(
			    *gfm_serverp, e) || --failover_nretries < 0) {
				gflog_debug(GFARM_MSG_1003902,
				    "gfs_client_connection_acquire_by_host: "
				    "%s", gfarm_error_string(e));
				return (e);
			}
			e = gfs_client_gfmd_failover_at_connect(gfm_serverp);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}

		if (gfs_client_pid(gfs_server) == 0) /* new connection */
			e = gfarm_client_process_set(gfs_server, *gfm_serverp);
		else /* cached connection */
			e = gfs_client_check_failovercount_or_reset_process(
			    gfs_server, *gfm_serverp);

		if (e == GFARM_ERR_NO_ERROR) {
			*gfs_serverp = gfs_server;
			return (e);
		}
		gflog_debug(GFARM_MSG_1003903,
		    "gfarm_client_process_(re)set: %s", gfarm_error_string(e));
		gfs_client_connection_free(gfs_server);

		if (e == GFARM_ERR_NO_SUCH_PROCESS &&
		    --failover_nretries >= 0) {
			e = gfs_client_gfmd_failover_at_connect(gfm_serverp);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			continue;
		} else if (gfs_client_is_connection_error(e) &&
		    --gfsd_nretries >= 0) {
			gflog_debug(GFARM_MSG_1003904,
			    "retry process (re)set");
			continue;
		}
		return (e);
	}
}

/*
 * gfs_client_connect - create an uncached connection
 *
 * XXX FIXME
 * `hostname' to `addr' conversion really should be done in this function,
 * rather than a caller of this function.
 */
gfarm_error_t
gfs_client_connect(struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port, const char *user,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfp_cached_connection *cache_entry;

	e = gfp_uncached_connection_new(canonical_hostname, port, user,
		&cache_entry);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001188,
			"making new uncached connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfs_client_connection_alloc_and_auth(gfm_server,
	    canonical_hostname, port, user, NULL, 0, cache_entry, gfs_serverp);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_uncached_connection_dispose(cache_entry);
		gflog_debug(GFARM_MSG_1001189,
			"client authentication failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

/* convert from uncached connection to cached */
gfarm_error_t
gfs_client_connection_enter_cache(struct gfs_connection *gfs_server)
{
	if (gfs_client_connection_is_cached(gfs_server)) {
		gflog_fatal(GFARM_MSG_1000068,
		    "gfs_client_connection_enter_cache: "
		    "programming error");
	}
	return (gfp_uncached_connection_enter_cache(&staticp->server_cache,
	    gfs_server->cache_entry));
}
gfarm_error_t
gfs_client_connection_enter_cache_tail(struct gfs_connection *gfs_server)
{
	if (gfs_client_connection_is_cached(gfs_server)) {
		gflog_fatal(GFARM_MSG_1000068,
		    "gfs_client_connection_enter_cache_tail: "
		    "programming error");
	}
	return (gfp_uncached_connection_enter_cache_tail(&staticp->server_cache,
	    gfs_server->cache_entry));
}
int
gfs_client_connection_cache_change(int cnt)
{
	return (gfp_connection_cache_change(&staticp->server_cache, cnt));
}

/*
 * multiplexed version of gfs_client_connect() for parallel authentication
 * for parallel authentication
 */

struct gfs_client_connect_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable;
	struct sockaddr_storage peer_addr;
	socklen_t peer_addrlen;
	void (*continuation)(void *);
	void *closure;

	struct gfs_connection *gfs_server;

	struct gfarm_auth_request_state *auth_state;

	/* results */
	gfarm_error_t error;
};

#ifndef __KERNEL__
static void
gfs_client_connect_end_auth(void *closure)
{
	struct gfs_client_connect_state *state = closure;

	state->error = gfarm_auth_result_multiplexed(
	    state->auth_state,
	    &state->gfs_server->auth_method);
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_connect_start_auth(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_connect_state *state = closure;
	int error = gfarm_socket_get_errno(fd);

	if (error != 0) {
		state->error = gfarm_errno_to_error(error);
	} else { /* successfully connected */
		state->error = gfarm_auth_request_multiplexed(state->q,
		    state->gfs_server->conn, GFS_SERVICE_TAG,
		    state->gfs_server->hostname,
		    (struct sockaddr *)&state->peer_addr,
		    gfarm_get_auth_id_type(),
		    gfs_client_username(state->gfs_server), NULL,
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (state->error == GFARM_ERR_NO_ERROR) {
			/*
			 * call auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return;
		}
	}
	gflog_debug(GFARM_MSG_1001190,
		"starting client connection auth failed: %s",
		gfarm_error_string(state->error));
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}
#else /* __KERNEL__ */
static void
gfs_client_connect_kern_end(int events, int fd, void *kevp, void *closure)
{
	struct gfs_client_connect_state *state = closure;
	gfarm_error_t e;

	if (kevp) {
		gfsk_req_free(kevp);
	}
	if (state) {
		if (fd < 0) {
			state->error = gfarm_errno_to_error(-fd);
		} else {
			e = gfp_xdr_new_socket(fd, &state->gfs_server->conn);
			if (e != GFARM_ERR_NO_ERROR) {
				close(fd);
				gflog_debug(GFARM_MSG_1003905,
				"gfp_xdr_new_socket() failed: %s",
				gfarm_error_string(e));
				state->error = e;
			}
		}
		if (state->continuation != NULL)
			(*state->continuation)(state->closure);
	}
}
#endif /* __KERNEL__ */

gfarm_error_t
gfs_client_connect_addr_request_multiplexed(struct gfarm_eventqueue *q,
	const char *canonical_hostname, int port, const char *user,
	struct sockaddr *peer_addr, socklen_t peer_addrlen,
	struct gfarm_filesystem *fs,
	void (*continuation)(void *), void *closure,
	struct gfs_client_connect_state **statepp)
{
	gfarm_error_t e;
	int rv;
	struct gfp_cached_connection *cache_entry;
	struct gfs_connection *gfs_server;
	struct gfs_client_connect_state *state;
#ifndef __KERNEL__
	int connection_in_progress, is_local, sock;
#else /* __KERNEL__ */
	void *kevp;
#endif /* __KERNEL__ */

	/* clone of gfs_client_connect() */

	GFARM_MALLOC(state);
	if (state == NULL) {
		gflog_debug(GFARM_MSG_1001191,
			"allocation of client connect state failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	e = gfp_uncached_connection_new(canonical_hostname, port, user,
	    &cache_entry);
	if (e != GFARM_ERR_NO_ERROR) {
		free(state);
		gflog_debug(GFARM_MSG_1001192,
			"making new uncached connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}
#ifndef __KERNEL__
	e = gfs_client_connect_addr(canonical_hostname, port,
	    peer_addr, peer_addrlen, NULL,
	    &connection_in_progress, &is_local, &sock);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_client_connection_alloc(canonical_hostname, port,
		    gfarm_filesystem_failover_count(fs), cache_entry,
		    is_local, sock, &gfs_server);
		if (e != GFARM_ERR_NO_ERROR)
			close(sock);
	}
#else /* __KERNEL__ */
	e = gfsk_client_connection_alloc_and_auth(q, canonical_hostname,
	    port, user, NULL, gfarm_filesystem_failover_count(fs), cache_entry,
	    &kevp, &gfs_server);
#endif /* __KERNEL__ */
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_uncached_connection_dispose(cache_entry);
		free(state);
		gflog_debug(GFARM_MSG_1001193,
			"allocation of client connection failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->auth_state = NULL;
	state->error = GFARM_ERR_NO_ERROR;
#ifndef __KERNEL__
	if (connection_in_progress) {
		state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
		    gfp_xdr_fd(gfs_server->conn),
		    gfs_client_connect_start_auth, state);
		if (state->writable == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else if ((rv = gfarm_eventqueue_add_event(q, state->writable,
		    NULL)) == 0) {
			*statepp = state;
			/* go to gfs_client_connect_start_auth() */
			return (GFARM_ERR_NO_ERROR);
		} else {
			e = gfarm_errno_to_error(rv);
			gfarm_event_free(state->writable);
		}
	} else {
		state->writable = NULL;
		e = gfarm_auth_request_multiplexed(q,
		    gfs_server->conn, GFS_SERVICE_TAG,
		    gfs_server->hostname, (struct sockaddr *)&state->peer_addr,
		    gfarm_get_auth_id_type(), user, NULL,
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (e == GFARM_ERR_NO_ERROR) {
			*statepp = state;
			/*
			 * call gfarm_auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return (GFARM_ERR_NO_ERROR);
		}
	}
#else /* __KERNEL__ */
	{
		state->writable = gfarm_kern_event_alloc(kevp,
				gfs_client_connect_kern_end, state);
		if (state->writable == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else if ((rv = gfarm_eventqueue_add_event(q, state->writable,
		    NULL)) == 0) {
			*statepp = state;
			/* go to gfs_client_connect_start_auth() */
			return (GFARM_ERR_NO_ERROR);
		} else {
			e = gfarm_errno_to_error(rv);
			gfarm_event_free(state->writable);
		}
	}
#endif /* __KERNEL__ */
	free(state);
	gfs_client_connection_dispose(gfs_server);
	gflog_debug(GFARM_MSG_1001194,
		"request for multiplexed client connect failed: %s",
		gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_connect_addr_result_multiplexed(
	struct gfs_client_connect_state *state,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e = state->error;
	struct gfs_connection *gfs_server = state->gfs_server;

	if (state->writable != NULL)
		gfarm_event_free(state->writable);
	free(state);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_client_connection_dispose(gfs_server);
		gflog_debug(GFARM_MSG_1001195,
			"error in result of multiplexed client connect: %s",
			gfarm_error_string(e));
		return (e);
	}

	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

struct gfs_client_connect_host_state {
	struct gfarm_eventqueue *q;
	const char *canonical_hostname;
	int port;
	const char *user;
	struct gfarm_filesystem *fs;

	void (*continuation)(void *);
	void *closure;

	int addr_count, addr_index;
	struct gfarm_host_address **addr_array;

	struct gfs_client_connect_state *connect_addr_state;

	/* results */
	struct gfs_connection *gfs_server;
	gfarm_error_t e;
};

static void
gfs_client_connect_addr_connected(void *closure)
{
	struct gfs_client_connect_host_state *state = closure;

	state->e = gfs_client_connect_addr_result_multiplexed(
	    state->connect_addr_state, &state->gfs_server);
	if (IS_CONNECTION_ERROR(state->e)) {
		if (++state->addr_index < state->addr_count) {
			state->e = gfs_client_connect_addr_request_multiplexed(
			    state->q, state->canonical_hostname, state->port,
			    state->user,
			    &state->addr_array[state->addr_index]->sa_addr,
			    state->addr_array[state->addr_index]->sa_addrlen,
			    state->fs,
			    gfs_client_connect_addr_connected, state,
			    &state->connect_addr_state);
			if (state->e == GFARM_ERR_NO_ERROR)
				return;
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_connect_host_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfm_connection *gfm_server,
	const char *canonical_hostname, int port, const char *user,
	struct gfarm_filesystem *fs,
	void (*continuation)(void *), void *closure,
	struct gfs_client_connect_host_state **statepp)
{
	gfarm_error_t e;
	int addr_count;
	struct gfarm_host_address **addr_array;
	struct gfs_client_connect_host_state *state;

	e = gfm_host_address_get(gfm_server, canonical_hostname, port,
	    &addr_count, &addr_array);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	GFARM_MALLOC(state);
	if (state == NULL) {
		gfarm_host_address_free(addr_count, addr_array);
		return (GFARM_ERR_NO_MEMORY);
	}
	state->q = q;
	state->canonical_hostname = canonical_hostname;
	state->port = port;
	state->user = user;
	state->fs = fs;
	state->continuation = continuation;
	state->closure = closure;

	state->addr_count = addr_count;
	state->addr_array = addr_array;
	state->addr_index = 0;

	state->gfs_server = NULL;
	state->e = GFARM_ERR_UNKNOWN;

	e = gfs_client_connect_addr_request_multiplexed(
	    q, canonical_hostname, port, user,
	    &state->addr_array[state->addr_index]->sa_addr,
	    state->addr_array[state->addr_index]->sa_addrlen,
	    fs, gfs_client_connect_addr_connected, state,
	    &state->connect_addr_state);
	if (e == GFARM_ERR_NO_ERROR) {
		*statepp = state;
		/* try each host in state->addr_array[] */
		return (GFARM_ERR_NO_ERROR);
	}
	gfarm_host_address_free(addr_count, addr_array);
	free(state);
	gflog_debug(GFARM_MSG_UNFIXED,
	    "request for multiplexed client connect addr failed: %s",
	    gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_connect_host_result_multiplexed(
	struct gfs_client_connect_host_state *state,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e = state->e;

	if (e == GFARM_ERR_NO_ERROR)
		*gfs_serverp = state->gfs_server;
	gfarm_host_address_free(state->addr_count, state->addr_array);
	free(state);
	return (e);
}

/*
 * gfs_client RPC
 */

int
gfarm_fd_receive_message(int fd, void *buf, size_t size,
	int fdc, int *fdv)
{
	char *buffer = buf;
	int i, rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
	struct {
		struct cmsghdr hdr;
		char data[CMSG_SPACE(sizeof(*fdv) * GFSD_MAX_PASSING_FD)
			  - sizeof(struct cmsghdr)];
	} cmsg;

	if (fdc > GFSD_MAX_PASSING_FD) {
#if 0
		fprintf(stderr, "gfarm_fd_receive_message(%s): "
			"fd count %d > %d\n", fdc, GFSD_MAX_PASSING_FD);
#endif
		return (EINVAL);
	}
#endif

	while (size > 0) {
		iov[0].iov_base = buffer;
		iov[0].iov_len = size;
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
#ifndef HAVE_MSG_CONTROL
		if (fdc > 0) {
			msg.msg_accrights = (caddr_t)fdv;
			msg.msg_accrightslen = sizeof(*fdv) * fdc;
			for (i = 0; i < fdc; i++)
				fdv[i] = -1;
		} else {
			msg.msg_accrights = NULL;
			msg.msg_accrightslen = 0;
		}
#else /* 4.3BSD Reno or later */
		if (fdc > 0) {
			msg.msg_control = (caddr_t)&cmsg.hdr;
			msg.msg_controllen = CMSG_SPACE(sizeof(*fdv) * fdc);
			memset(msg.msg_control, 0, msg.msg_controllen);
			for (i = 0; i < fdc; i++)
				((int *)CMSG_DATA(&cmsg.hdr))[i] = -1;
		} else {
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
		}
#endif
		rv = recvmsg(fd, &msg, 0);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			return (errno); /* failure */
		} else if (rv == 0) {
			return (-1); /* EOF */
		}
#ifdef HAVE_MSG_CONTROL /* 4.3BSD Reno or later */
		if (fdc > 0) {
			if (msg.msg_controllen !=
			    CMSG_SPACE(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_len !=
			    CMSG_LEN(sizeof(*fdv) * fdc) ||
			    cmsg.hdr.cmsg_level != SOL_SOCKET ||
			    cmsg.hdr.cmsg_type != SCM_RIGHTS) {
#if 0
				fprintf(stderr,
					"gfarm_fd_receive_message():"
					" descriptor not passed"
					" msg_controllen: %d (%d),"
					" cmsg_len: %d (%d),"
					" cmsg_level: %d,"
					" cmsg_type: %d\n",
					msg.msg_controllen,
					CMSG_SPACE(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_len,
					CMSG_LEN(sizeof(*fdv) * fdc),
					cmsg.hdr.cmsg_level,
					cmsg.hdr.cmsg_type);
#endif
			}
			for (i = 0; i < fdc; i++)
				fdv[i] = ((int *)CMSG_DATA(&cmsg.hdr))[i];
		}
#endif
		fdc = 0; fdv = NULL;
		buffer += rv;
		size -= rv;
	}
	return (0); /* success */
}

gfarm_error_t
gfs_client_rpc_request(struct gfs_connection *gfs_server, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfp_xdr_vrpc_request(gfs_server->conn, command, &format, &ap);
	va_end(ap);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001196,
			"gfp_xdr_vrpc_request() failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfs_client_vrpc_result(struct gfs_connection *gfs_server, int just,
	gfarm_int32_t *errcodep, const char *format, va_list *app)
{
	gfarm_error_t e;

	gfs_client_connection_used(gfs_server);

	e = gfp_xdr_flush(gfs_server->conn);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001197,
			"gfp_xdr_flush() failed : %s",
			gfarm_error_string(e));
		return (e);
	}

	e = gfp_xdr_vrpc_result(gfs_server->conn, just, 1,
	    errcodep, &format, app);

	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001198,
			"gfp_xdr_vrpc_result() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	/* We just use gfarm_error_t as the errcode */
	if (*errcodep != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1001199,
		    "gfp_xdr_vrpc_result() failed errcode=%d", (int)*errcodep);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_rpc_result_w_errcode(struct gfs_connection *gfs_server, int just,
	gfarm_int32_t *errcodep, const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;

	va_start(ap, format);
	e = gfs_client_vrpc_result(gfs_server, just, errcodep, format, &ap);
	va_end(ap);

	return (e);
}

gfarm_error_t
gfs_client_rpc_result(struct gfs_connection *gfs_server, int just,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	gfarm_int32_t errcode;

	va_start(ap, format);
	e = gfs_client_vrpc_result(gfs_server, just, &errcode, format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	return (errcode);
}

static gfarm_error_t
gfs_client_vrpc(struct gfs_connection *gfs_server, int just, int do_timeout,
	int command, const char *format, va_list *app)
{
	gfarm_error_t e;
	int errcode;

	gfs_client_connection_used(gfs_server);

	gfs_client_connection_lock(gfs_server);
	e = gfp_xdr_vrpc(gfs_server->conn, just, do_timeout,
	    command, &errcode, &format, app);
	gfs_client_connection_unlock(gfs_server);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003561, "gfp_xdr_vrpc(%d) failed: %s",
		    command, gfarm_error_string(e));
		return (e);
	}
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		gflog_debug(GFARM_MSG_1003562, "gfp_xdr_vrpc(%d) errcode=%d",
		    command, errcode);
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_rpc(struct gfs_connection *gfs_server, int just,
	int command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfs_client_vrpc(gfs_server, just, 1, command, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfs_client_rpc_notimeout(struct gfs_connection *gfs_server, int just,
	int command, const char *format, ...)
{
	gfarm_error_t e;
	va_list ap;

	va_start(ap, format);
	e = gfs_client_vrpc(gfs_server, just, 0, command, format, &ap);
	va_end(ap);
	return (e);
}

gfarm_error_t
gfs_client_process_set(struct gfs_connection *gfs_server,
	gfarm_int32_t type, const char *key, size_t size, gfarm_pid_t pid)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PROCESS_SET, "ibl/",
	    type, size, key, pid);
	if (e == GFARM_ERR_NO_ERROR)
		gfs_server->pid = pid;
	else
		gflog_debug(GFARM_MSG_1001202,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_process_reset(struct gfs_connection *gfs_server,
	gfarm_int32_t type, const char *key, size_t size, gfarm_pid_t pid)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PROCESS_RESET,
		"ibli/", type, size, key, pid, gfs_server->failover_count);
	if (e == GFARM_ERR_NO_ERROR)
		gfs_server->pid = pid;
	else {
		gfs_server->pid = 0;
		gflog_debug(GFARM_MSG_1003377,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfs_client_open(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_OPEN, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		++gfs_server->opened;
	else
		gflog_debug(GFARM_MSG_1001203,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_open_local(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	int *fd_ret)
{
	gfarm_error_t e;
	int rv, local_fd;
	gfarm_int8_t dummy; /* needs at least 1 byte */

	if (!gfs_server->is_local) {
		gflog_debug(GFARM_MSG_1001204,
			"gfs server is local: %s",
			gfarm_error_string(GFARM_ERR_OPERATION_NOT_SUPPORTED));
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
	}

	/* we have to set `just' flag here */
	gfs_client_connection_lock(gfs_server);
	e = gfs_client_rpc_request(gfs_server, GFS_PROTO_OPEN_LOCAL, "i", fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_client_connection_unlock(gfs_server);
		gflog_debug(GFARM_MSG_1001205,
			"gfs_client_rpc_request() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	e = gfs_client_rpc_result(gfs_server, 1, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_client_connection_unlock(gfs_server);
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfs_client_rpc_result() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	/* layering violation, but... */
	rv = gfarm_fd_receive_message(gfp_xdr_fd(gfs_server->conn),
	    &dummy, sizeof(dummy), 1, &local_fd);
	if (rv == 0) {
		++gfs_server->opened;
	}
	gfs_client_connection_unlock(gfs_server);
	if (rv == -1) { /* EOF */
		gflog_debug(GFARM_MSG_1001206,
			"Unexpected EOF when receiving message: %s",
			gfarm_error_string(GFARM_ERR_UNEXPECTED_EOF));
		return (GFARM_ERR_UNEXPECTED_EOF);
	}
	if (rv != 0) {
		gflog_debug(GFARM_MSG_1001207,
			"receiving message failed: %s",
			gfarm_error_string(gfarm_errno_to_error(rv)));
		return (gfarm_errno_to_error(rv));
	}
	/* both `dummy' and `local_fd` are passed by using host byte order. */
#ifdef __KERNEL__
	if ((rv = gfsk_localfd_set(local_fd, S_IFREG)) < 0) {
		gflog_debug(GFARM_MSG_1003906,
			"gfsk_localfd_set failed: %s",
			gfarm_error_string(gfarm_errno_to_error(-rv)));
		return (gfarm_errno_to_error(-rv));
	}
	local_fd = rv;
#endif /* __KERNEL__ */
	*fd_ret = local_fd;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_close(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_CLOSE, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		--gfs_server->opened;
	else
		gflog_debug(GFARM_MSG_1001208,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_close_write(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_int32_t flags)
{
	gfarm_error_t e;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_CLOSE_WRITE, "ii/",
	    fd, flags);
	if (e == GFARM_ERR_NO_ERROR)
		--gfs_server->opened;
	else
		gflog_debug(GFARM_MSG_1003907,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
	return (e);
}

gfarm_error_t
gfs_client_pread(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, void *buffer, size_t size,
	gfarm_off_t off, size_t *np)
{
	gfarm_error_t e;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PREAD, "iil/b",
	    fd, (int)size, off,
	    size, np, buffer)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001209,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (*np > size) {
		gflog_debug(GFARM_MSG_1001210,
			"Protocol error in client pread (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PREAD_PROTOCOL);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_pwrite(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, const void *buffer, size_t size,
	gfarm_off_t off,
	size_t *np)
{
	gfarm_error_t e;
	gfarm_int32_t n; /* size_t may be 64bit */

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PWRITE, "ibl/i",
	    fd, size, buffer, off, &n)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001211,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*np = n;
	if (n > size) {
		gflog_debug(GFARM_MSG_1001212,
			"Protocol error in client pwrite (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PWRITE_PROTOCOL);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_write(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, const void *buffer, size_t size,
	size_t *np, gfarm_off_t *offp, gfarm_off_t *total_sizep)
{
	gfarm_error_t e;
	gfarm_int32_t n; /* size_t may be 64bit */

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_WRITE, "ib/ill",
	    fd, size, buffer, &n, offp, total_sizep)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003686,
			"gfs_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	*np = n;
	assert(n <= size);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_ftruncate(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_off_t size)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FTRUNCATE, "il/",
	    fd, size));
}

gfarm_error_t
gfs_client_fsync(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_int32_t op)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSYNC, "ii/",
	    fd, op));
}

gfarm_error_t
gfs_client_fstat(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t *size,
	gfarm_int64_t *atime_sec, gfarm_int32_t *atime_nsec,
	gfarm_int64_t *mtime_sec, gfarm_int32_t *mtime_nsec)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSTAT, "i/llili",
	    fd, size, atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfs_client_cksum(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	const char *type, char *cksum, size_t size, size_t *np)
{
	gfarm_error_t e;

	if ((e = gfs_client_rpc_notimeout(gfs_server, 0, GFS_PROTO_CKSUM,
	    "is/b", fd, type, size, np, cksum)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003724,
		    "gfs_client_cksum: %s", gfarm_error_string(e));
		return (e);
	}
	if (*np > size) {
		gflog_error(GFARM_MSG_1003725,
		    "Internal protocol error (%llu)>(%llu)",
		    (unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERR_PROTOCOL);
	}
	if (size > *np)
		cksum[*np] = '\0';
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_lock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_trylock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_TRYLOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_unlock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_UNLOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_lock_info(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence,
	gfarm_off_t *start_ret, gfarm_off_t *len_ret,
	gfarm_int32_t *type_ret, char **host_ret, gfarm_pid_t **pid_ret)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK_INFO,
	    "illii/llisl",
	    fd, start, len, type, whence,
	    start_ret, len_ret, type_ret, host_ret, pid_ret));
}

gfarm_error_t
gfs_client_replica_add_from(struct gfs_connection *gfs_server,
	char *host, gfarm_int32_t port, gfarm_int32_t fd)
{
	return (gfs_client_rpc_notimeout(gfs_server, 0,
	    GFS_PROTO_REPLICA_ADD_FROM, "sii/", host, port, fd));
}

gfarm_error_t
gfs_client_statfs(struct gfs_connection *gfs_server, char *path,
	gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_STATFS, "s/illllll",
	    path, bsizep, blocksp, bfreep, bavailp, filesp, ffreep, favailp));
}

/*
 * multiplexed version of gfs_client_statfs()
 */
struct gfs_client_statfs_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable, *readable;
	void (*continuation)(void *);
	void *closure;
	struct gfs_connection *gfs_server;
	char *path;

	/* results */
	gfarm_error_t error;
	gfarm_int32_t bsize;
	gfarm_off_t blocks, bfree, bavail;
	gfarm_off_t files, ffree, favail;
};

static void
gfs_client_statfs_send_request(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_statfs_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error = gfs_client_rpc_request(state->gfs_server,
	    GFS_PROTO_STATFS, "s", state->path);
	if (state->error == GFARM_ERR_NO_ERROR) {
		state->error = gfp_xdr_flush(state->gfs_server->conn);
		if (IS_CONNECTION_ERROR(state->error)) {
			gfs_client_execute_hook_for_connection_error(
			    state->gfs_server);
			gfs_client_purge_from_cache(state->gfs_server);
		}
		if (state->error == GFARM_ERR_NO_ERROR) {
			timeout.tv_sec = GFS_CLIENT_COMMAND_TIMEOUT;
			timeout.tv_usec = 0;
			if ((rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout)) == 0) {
				/* go to gfs_client_statfs_recv_result() */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}
	}
	gflog_debug(GFARM_MSG_1001213,
		"request for client statfs failed: %s",
		gfarm_error_string(state->error));
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_statfs_recv_result(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_statfs_state *state = closure;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
	} else {
		assert(events == GFARM_EVENT_READ);
		state->error = gfs_client_rpc_result(state->gfs_server, 0,
		    "illllll", &state->bsize,
		    &state->blocks, &state->bfree, &state->bavail,
		    &state->files, &state->ffree, &state->favail);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_statfs_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfs_connection *gfs_server, char *path,
	void (*continuation)(void *), void *closure,
	struct gfs_client_statfs_state **statepp)
{
	gfarm_error_t e;
	int rv;
	struct gfs_client_statfs_state *state;

	GFARM_MALLOC(state);
	if (state == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001214,
			"allocation of client statfs state failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_return;
	}

	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->path = path;
	state->error = GFARM_ERR_NO_ERROR;
	state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
	    gfs_client_connection_fd(gfs_server),
	    gfs_client_statfs_send_request, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001215,
			"allocation of state->writable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT,
	    gfs_client_connection_fd(gfs_server),
	    gfs_client_statfs_recv_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001216,
			"allocation of state->readable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_writable;
	}
	/* go to gfs_client_statfs_send_request() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001217,
			"adding event to event queue failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
error_return:
	return (e);
}

gfarm_error_t
gfs_client_statfs_result_multiplexed(struct gfs_client_statfs_state *state,
	gfarm_int32_t *bsizep,
	gfarm_off_t *blocksp, gfarm_off_t *bfreep, gfarm_off_t *bavailp,
	gfarm_off_t *filesp, gfarm_off_t *ffreep, gfarm_off_t *favailp)
{
	gfarm_error_t e = state->error;

	gfarm_event_free(state->writable);
	gfarm_event_free(state->readable);
	if (e == GFARM_ERR_NO_ERROR) {
		*bsizep = state->bsize;
		*blocksp = state->blocks;
		*bfreep = state->bfree;
		*bavailp = state->bavail;
		*filesp = state->files;
		*ffreep = state->ffree;
		*favailp = state->favail;
	}
	free(state);
	return (e);
}

/*--------------------------------------------------------*/
/*
 * multiplexed version of gfs_ib_rdma()
 */
struct gfs_ib_rdma_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable, *readable;
	void (*continuation)(void *);
	void *closure;
	struct gfs_connection *gfs_server;

	/* results */
	gfarm_error_t error;
	enum rdma_state {RDMA_EXCH_INFO, RDMA_HELLO, RDMA_FIN} state;
};

static void
gfs_ib_rdma_send_request(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_ib_rdma_state *state = closure;
	int rv;
	struct timeval timeout;
	struct rdma_context *ctx = gfs_ib_rdma_context(state->gfs_server);

	switch (state->state) {
	case RDMA_EXCH_INFO:
	{
		gfarm_uint32_t local_lid = gfs_rdma_get_local_lid(ctx);
		gfarm_uint32_t local_qpn = gfs_rdma_get_local_qpn(ctx);
		gfarm_uint32_t local_psn = gfs_rdma_get_local_psn(ctx);
		void *local_gid = gfs_rdma_get_local_gid(ctx);
		size_t  size = gfs_rdma_get_gid_size();

		state->error = gfs_client_rpc_request(state->gfs_server,
			GFS_PROTO_RDMA_EXCH_INFO, "iiib", local_lid, local_qpn,
			local_psn, size, local_gid);
		break;
	}
	case RDMA_HELLO:
	{
		gfarm_uint32_t rkey = gfs_rdma_get_rkey(gfs_rdma_get_mr(ctx));
		unsigned char *buf = gfs_rdma_get_buffer(ctx);
		int size = gfs_rdma_get_gid_size();
		gfarm_uint64_t addr;
		memcpy(buf, gfs_rdma_get_local_gid(ctx), size);
		addr = (uintptr_t)buf;
		state->error = gfs_client_rpc_request(state->gfs_server,
			GFS_PROTO_RDMA_HELLO, "iil", rkey, size, addr);
		break;
	}
	default:
		gflog_fatal(GFARM_MSG_1004537,
			"request state error: %d", state->state);
	}
	gflog_debug(GFARM_MSG_1004538,
		"gfs_ib_rdma_send_request:%s state=%d, %s",
		state->gfs_server->hostname, state->state,
		gfarm_error_string(state->error));
	if (state->error == GFARM_ERR_NO_ERROR) {
		state->error = gfp_xdr_flush(state->gfs_server->conn);
		if (IS_CONNECTION_ERROR(state->error)) {
			gfs_client_execute_hook_for_connection_error(
			    state->gfs_server);
			gfs_client_purge_from_cache(state->gfs_server);
		} else if (state->error == GFARM_ERR_NO_ERROR) {
			timeout.tv_sec = GFS_CLIENT_COMMAND_TIMEOUT;
			timeout.tv_usec = 0;
			if ((rv = gfarm_eventqueue_add_event(state->q,
			    state->readable, &timeout)) == 0) {
				/* go to gfs_client_statfs_recv_result() */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}
	}
	gflog_debug(GFARM_MSG_1004539,
		"request for client rdma failed: %s",
		gfarm_error_string(state->error));
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_ib_rdma_recv_result(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_ib_rdma_state *state = closure;
	struct rdma_context *ctx = gfs_ib_rdma_context(state->gfs_server);

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		state->error = GFARM_ERR_OPERATION_TIMED_OUT;
	} else {
		assert(events == GFARM_EVENT_READ);
		switch (state->state) {
		case RDMA_EXCH_INFO:
		{
			gfarm_uint32_t server_lid;
			gfarm_uint32_t server_qpn;
			gfarm_uint32_t server_psn;
			void *server_gid = gfs_rdma_get_remote_gid(ctx);
			size_t  size = gfs_rdma_get_gid_size();
			int success;

			state->error = gfs_client_rpc_result(state->gfs_server,
				0, "iiiib", &success, &server_lid, &server_qpn,
				&server_psn, size, &size, server_gid);
			if (state->error == GFARM_ERR_NO_ERROR && success) {
				gfs_rdma_set_remote_lid(ctx, server_lid);
				gfs_rdma_set_remote_qpn(ctx, server_qpn);
				gfs_rdma_set_remote_psn(ctx, server_psn);

				state->error = gfs_rdma_connect(ctx);

			} else if (state->error == GFARM_ERR_NO_ERROR)
				state->error = GFARM_ERR_DEVICE_NOT_CONFIGURED;
			break;
		}
		case RDMA_HELLO:
			state->error = gfs_client_rpc_result(state->gfs_server,
				0, "");
			break;
		default:
			gflog_fatal(GFARM_MSG_1004540,
			"result state error: %d", state->state);
		}
	}
	gflog_debug(GFARM_MSG_1004541,
		"gfs_ib_rdma_recv_result:%s state=%d, %s",
		state->gfs_server->hostname, state->state,
		gfarm_error_string(state->error));
	if (state->error == GFARM_ERR_NO_ERROR) {
		state->state++;
		if (state->state != RDMA_FIN) {
			int rv;
			if ((rv = gfarm_eventqueue_add_event(state->q,
			    state->writable, NULL)) == 0) {
				/* go to gfs_ib_rdma_recv_result() */
				return;
			}
			state->error = gfarm_errno_to_error(rv);
		}

	}
	if (state->error != GFARM_ERR_NO_ERROR) {
		gfs_rdma_disable(ctx);
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_ib_rdma_request_multiplexed(struct gfarm_eventqueue *q,
	struct gfs_connection *gfs_server, void (*continuation)(void *),
	void *closure, struct gfs_ib_rdma_state **statepp)
{
	gfarm_error_t e;
	int rv;
	struct gfs_ib_rdma_state *state;
	struct rdma_context *ctx = gfs_ib_rdma_context(gfs_server);

	if (!gfs_rdma_check(ctx)) {
		return (GFARM_ERR_DEVICE_NOT_CONFIGURED);
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1004542,
			"allocation of client rdma state failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_return;
	}

	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->error = GFARM_ERR_NO_ERROR;
	state->state = RDMA_EXCH_INFO;
	state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
	    gfs_client_connection_fd(gfs_server),
	    gfs_ib_rdma_send_request, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1004543,
			"allocation of state->writable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT,
	    gfs_client_connection_fd(gfs_server),
	    gfs_ib_rdma_recv_result, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1004544,
			"allocation of state->readable failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto error_free_writable;
	}
	/* go to gfs_ib_rdma_send_request() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1004545,
			"adding event to event queue failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
error_return:
	return (e);
}

gfarm_error_t
gfs_ib_rdma_result_multiplexed(struct gfs_ib_rdma_state *state)
{
	gfarm_error_t e = state->error;

	gfarm_event_free(state->writable);
	gfarm_event_free(state->readable);
	free(state);
	return (e);
}
/*---------------------------------------------*/
#ifndef __KERNEL__
#define GFS_STACK_BUFSIZE  GFS_PROTO_MAX_IOSIZE
#else /* __KERNEL__  */
#define GFS_STACK_BUFSIZE 1024
#endif /* __KERNEL__  */
/*
 * commonly used by both clients and gfsd
 *
 * len: if -1, read until EOF.
 * return value: connection related error
 * *src_errp: set even if an error happens.
 * *sentp: set even if an error happens.
 *
 * XXX not yet in gfarm v2:
 * XXX rate_limit parameter has to be passed to this function too.
 * XXX should use "netparam file_read_size" setting as well.
 */
gfarm_error_t
gfs_sendfile_common(struct gfp_xdr *conn, gfarm_int32_t *src_errp,
	int r_fd, gfarm_off_t r_off,
	gfarm_off_t len, EVP_MD_CTX *md_ctx, gfarm_off_t *sentp)
{
	gfarm_error_t e;
	gfarm_error_t e_conn = GFARM_ERR_NO_ERROR;
	gfarm_error_t e_read = GFARM_ERR_NO_ERROR;
	size_t to_read;
	ssize_t rv;
	off_t sent = 0;
	int mode_unknown = 1, mode_thread_safe = 1, until_eof = len < 0;
	char buffer[GFS_STACK_BUFSIZE];
#if 0 /* not yet in gfarm v2 */
	struct gfs_client_rep_rate_info *rinfo = NULL;

	if (rate_limit != 0) {
		rinfo = gfs_client_rep_rate_info_alloc(rate_limit);
		if (rinfo == NULL)
			fatal("%s: rate_info_alloc: no_memory", diag);
	}
#endif
	if (until_eof || len > 0) {
		for (;;) {
			to_read = until_eof ? GFS_STACK_BUFSIZE :
			    len < GFS_STACK_BUFSIZE ?
			    len : GFS_STACK_BUFSIZE;
			if (mode_unknown) {
				mode_unknown = 0;
				rv = pread(r_fd, buffer, to_read, r_off);
				if (rv == -1 && errno == ESPIPE &&
				    r_off <= 0) {
					mode_thread_safe = 0;
					rv = read(r_fd, buffer, to_read);
				}
			} else if (mode_thread_safe) {
				rv = pread(r_fd, buffer, to_read, r_off);
			} else {
				rv = read(r_fd, buffer, to_read);
			}
			if (rv == 0)
				break;
			if (rv == -1) {
				e_read = gfarm_errno_to_error(errno);
				break;
			}
			r_off += rv;
			if (!until_eof)
				len -= rv;
			gfarm_iostat_local_add(GFARM_IOSTAT_IO_RCOUNT, 1);
			gfarm_iostat_local_add(GFARM_IOSTAT_IO_RBYTES, rv);
			e = gfp_xdr_send(conn, "b", rv, buffer);
			if (e != GFARM_ERR_NO_ERROR) {
				e_conn = e;
				gflog_debug(GFARM_MSG_1002180,
				    "gfp_xdr_send() failed: %s",
				    gfarm_error_string(e));
				break;
			}
			sent += rv;
			/*
			 * sent == the length of calculated digest.
			 * NOTE: there is no guarantee that the contents are
			 * actually written in the remote side at this point.
			 */
			if (md_ctx != NULL)
				EVP_DigestUpdate(md_ctx, buffer, rv);

#if 0 /* not yet in gfarm v2 */
			if (rate_limit != 0)
				gfs_client_rep_rate_control(rinfo, rv);
#endif
		}
	}
#if 0 /* not yet in gfarm v2 */
	if (rinfo != NULL)
		gfs_client_rep_rate_info_free(rinfo);
#endif

	/* send EOF mark */
	e = gfp_xdr_send(conn, "b", 0, buffer);
	if (e_conn == GFARM_ERR_NO_ERROR)
		e_conn = e;
	if (src_errp != NULL)
		*src_errp = e_read;
	if (sentp != NULL)
		*sentp = sent;
	return (e_conn);
}

/*
 * commonly used by both clients and gfsd
 *
 * return value: connection related error
 * *dst_errp: set even if an error happens.
 * *md_abortedp: set even if an error happens.
 * *recvp: set even if an error happens.
 */
gfarm_error_t
gfs_recvfile_common(struct gfp_xdr *conn, gfarm_int32_t *dst_errp,
	int w_fd, gfarm_off_t w_off,
	int append_mode, EVP_MD_CTX *md_ctx, int *md_abortedp,
	gfarm_off_t *recvp)
{
	gfarm_error_t e; /* connection related error */
	gfarm_error_t e_write = GFARM_ERR_NO_ERROR;
	gfarm_off_t written = 0, written_offset;
	int md_aborted = 0;
	int mode_unknown = 1, mode_thread_safe = 1;

	if (append_mode) {
		mode_unknown = 0;
		mode_thread_safe = 0;
	}
	for (;;) {
		gfarm_int32_t size;
		int eof;

		/* XXX - FIXME layering violation */
		e = gfp_xdr_recv(conn, 0, &eof, "i", &size);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		if (eof) {
			e = GFARM_ERR_PROTOCOL;
			break;
		}
		if (size <= 0)
			break;
		do {
			int i, partial;
			ssize_t rv;
			char buffer[GFS_STACK_BUFSIZE];

			/* XXX - FIXME layering violation */
			e = gfp_xdr_recv_partial(conn, 0,
			    buffer, size, &partial);
			if (e != GFARM_ERR_NO_ERROR)
				break;
			if (partial <= 0) {
				e = GFARM_ERR_PROTOCOL;
				break;
			}
			size -= partial;
			if (e_write != GFARM_ERR_NO_ERROR) {
				/*
				 * write(2) returned an error.
				 * We should receive rest of data
				 * even in that case.
				 */
				continue;
			}
			for (i = 0; i < partial; i += rv) {
				if (mode_unknown) {
					mode_unknown = 0;
					rv = pwrite(w_fd,
					    buffer + i, partial - i, w_off);
					if (rv == -1 && errno == ESPIPE &&
					    w_off <= 0) {
						mode_thread_safe = 0;
						rv = write(w_fd,
						    buffer + i, partial - i);
					}
				} else if (mode_thread_safe) {
					rv = pwrite(w_fd,
					    buffer + i, partial - i, w_off);
				} else {
					rv = write(w_fd,
					    buffer + i, partial - i);
				}
				if (rv == 0) {
					/*
					 * pwrite(2) never returns 0,
					 * so this is just warm fuzzy.
					 */
					e_write = GFARM_ERR_NO_SPACE;
					break;
				}
				if (rv == -1) {
					e_write = gfarm_errno_to_error(errno);
					break;
				}
				if (append_mode) {
					assert(!mode_thread_safe);
					written_offset =
					    lseek(w_fd, 0, SEEK_CUR);
					/*
					 * if lseek(2) fails, this isn't called
					 * by gfsd for gfs_pio_sendfile(), but
					 * by a client for gfs_pio_recvfile().
					 * it's OK to continue to calculate
					 * cksum in that case, because the
					 * cksum won't be set as metadata.
					 */
					if (written_offset != -1 &&
					    written_offset - rv != w_off)
						md_aborted = 1;
				}
				w_off += rv;
				written += rv;
				gfarm_iostat_local_add(
				    GFARM_IOSTAT_IO_WCOUNT, 1);
				gfarm_iostat_local_add(
				    GFARM_IOSTAT_IO_WBYTES, rv);
			}
			if (md_ctx != NULL && !md_aborted)
				EVP_DigestUpdate(
				    md_ctx, buffer, partial);
		} while (size > 0);
		if (e != GFARM_ERR_NO_ERROR)
			break;
	}
	if (dst_errp != NULL)
		*dst_errp = e_write;
	if (md_abortedp != NULL)
		*md_abortedp = md_aborted;
	if (recvp != NULL)
		*recvp = written;
	return (e);
}

/*
 * *md_ctx: if an error happens in gfsd side, *md_ctx may not reflect
 *	the content which is actually written by the gfsd.
 *	thus, if an error happens, *md_ctx has to be invalidated.
 *	see that the last argument of gfs_sendfile_common() (sentp) is NULL.
 */
gfarm_error_t
gfs_client_sendfile(struct gfs_connection *gfs_server,
	gfarm_int32_t remote_w_fd, gfarm_off_t w_off,
	int local_r_fd, gfarm_off_t r_off,
	gfarm_off_t len, EVP_MD_CTX *md_ctx, gfarm_off_t *sentp)
{
	gfarm_error_t e, e2;
	gfarm_int32_t src_err = GFARM_ERR_NO_ERROR;
	gfarm_off_t written = 0;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_BULKWRITE, "il/",
	    remote_w_fd, w_off)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003908,
		    "gfs_client_sendfile: GFS_PROTO_BULKWRITE(%d, %lld): %s",
		    remote_w_fd, (long long)w_off, gfarm_error_string(e));
	} else {
		e = gfs_sendfile_common(gfs_server->conn, &src_err,
		    local_r_fd, r_off, len, md_ctx, NULL);
		if (IS_CONNECTION_ERROR(e)) {
			gfs_client_execute_hook_for_connection_error(
			    gfs_server);
			gfs_client_purge_from_cache(gfs_server);
		} else { /* read the rest, even if a local error happens */
			e2 = gfs_client_rpc_result(gfs_server, 0, "l",
			    &written);
			if (e == GFARM_ERR_NO_ERROR)
				e = e2;
		}
	}
	if (sentp != NULL)
		*sentp = written;
	return (e != GFARM_ERR_NO_ERROR ? e : src_err);
}

gfarm_error_t
gfs_client_recvfile(struct gfs_connection *gfs_server,
	gfarm_int32_t remote_r_fd, gfarm_off_t r_off,
	int local_w_fd, gfarm_off_t w_off,
	gfarm_off_t len, int append_mode, EVP_MD_CTX *md_ctx, int *md_abortedp,
	gfarm_off_t *recvp)
{
	gfarm_error_t e, e2;
	gfarm_int32_t src_err = GFARM_ERR_NO_ERROR;
	gfarm_int32_t dst_err = GFARM_ERR_NO_ERROR;
	gfarm_off_t written = 0;
	int eof;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_BULKREAD, "ill/",
	    remote_r_fd, len, r_off)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003909,
		    "gfs_client_recvfile: "
		    "GFS_PROTO_BULKREAD(%d, %lld, %lld): %s",
		    remote_r_fd, (long long)len, (long long)r_off,
		    gfarm_error_string(e));
	} else {
		e = gfs_recvfile_common(gfs_server->conn, &dst_err,
		    local_w_fd, w_off,
		    append_mode, md_ctx, md_abortedp, &written);
		if (IS_CONNECTION_ERROR(e)) {
			gfs_client_execute_hook_for_connection_error(
			    gfs_server);
			gfs_client_purge_from_cache(gfs_server);
			e2 = GFARM_ERR_NO_ERROR;
		} else { /* read the rest, even if a local error happens */
			e2 = gfp_xdr_recv(gfs_server->conn, 0, &eof, "i",
			    &src_err);
			if (e2 == GFARM_ERR_NO_ERROR && eof)
				e2 = GFARM_ERR_PROTOCOL;
		}
		if (IS_CONNECTION_ERROR(e) || IS_CONNECTION_ERROR(e2)) {
			gfs_client_execute_hook_for_connection_error(
			    gfs_server);
			gfs_client_purge_from_cache(gfs_server);
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = e2 != GFARM_ERR_NO_ERROR ? e2 :
			    src_err != GFARM_ERR_NO_ERROR ? src_err : dst_err;
	}
	if (recvp != NULL)
		*recvp = written;
	return (e);
}

#ifndef __KERNEL__ /* gfsd only */
/*
 * GFS_PROTO_REPLICA_RECV and GFS_PROTO_REPLICA_RECV_CKSUM are
 * only used by gfsd, but defined here for better maintainability.
 */

static gfarm_error_t
gfs_client_replica_recv_common(struct gfs_connection *gfs_server,
	gfarm_int32_t *src_errp, gfarm_int32_t *dst_errp,
	gfarm_ino_t ino, gfarm_uint64_t gen, gfarm_int64_t filesize,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t cksum_request_flags,
	size_t src_cksum_size, size_t *src_cksum_lenp, char *src_cksum,
	gfarm_int32_t *cksum_result_flagsp,
	int local_fd, EVP_MD_CTX *md_ctx, int cksum_protocol)
{
	gfarm_error_t e, e_rpc;


	if (cksum_protocol) {
		e = gfs_client_rpc_request(gfs_server,
		    GFS_PROTO_REPLICA_RECV_CKSUM, "lllsbi", ino, gen, filesize,
		    cksum_type, cksum_len, cksum, cksum_request_flags);
	} else {
		e = gfs_client_rpc_request(gfs_server,
		    GFS_PROTO_REPLICA_RECV, "ll", ino, gen);
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = gfp_xdr_flush(gfs_server->conn);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001218,
			"gfs_request_client_rpc() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	e = gfs_recvfile_common(gfs_server->conn, dst_errp,
	    local_fd, 0, 0, md_ctx, NULL, NULL);
	if (IS_CONNECTION_ERROR(e)) {
		gfs_client_execute_hook_for_connection_error(gfs_server);
		gfs_client_purge_from_cache(gfs_server);
	} else { /* read the rest, even if a local error happens */
		if (cksum_protocol) {
			e_rpc = gfs_client_rpc_result_w_errcode(gfs_server, 0,
			    src_errp, "bi",
			    src_cksum_size, src_cksum_lenp, src_cksum,
			    cksum_result_flagsp);
		} else {
			e_rpc = gfs_client_rpc_result_w_errcode(gfs_server, 0,
			    src_errp, "");
		}
		if (e == GFARM_ERR_NO_ERROR)
			e = e_rpc;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001219,
		    "receiving client replica failed: %s",
		    gfarm_error_string(e));
	}
	return (e);
}

gfarm_error_t
gfs_client_replica_recv_md(struct gfs_connection *gfs_server,
	gfarm_int32_t *src_errp, gfarm_int32_t *dst_errp,
	gfarm_ino_t ino, gfarm_uint64_t gen, int local_fd, EVP_MD_CTX *md_ctx)
{
	return (gfs_client_replica_recv_common(gfs_server, src_errp, dst_errp,
	    ino, gen, 0, NULL, 0, NULL, 0, 0, NULL, NULL, NULL,
	    local_fd, md_ctx, 0));
}

gfarm_error_t
gfs_client_replica_recv_cksum_md(struct gfs_connection *gfs_server,
	gfarm_int32_t *src_errp, gfarm_int32_t *dst_errp,
	gfarm_ino_t ino, gfarm_uint64_t gen, gfarm_int64_t filesize,
	const char *cksum_type, size_t cksum_len, const char *cksum,
	gfarm_int32_t cksum_request_flags,
	size_t src_cksum_size, size_t *src_cksum_lenp, char *src_cksum,
	gfarm_int32_t *cksum_result_flagsp,
	int local_fd, EVP_MD_CTX *md_ctx)
{
	return (gfs_client_replica_recv_common(gfs_server, src_errp, dst_errp,
	    ino, gen, filesize,
	    cksum_type, cksum_len, cksum, cksum_request_flags,
	    src_cksum_size, src_cksum_lenp, src_cksum, cksum_result_flagsp,
	    local_fd, md_ctx, 1));
}

#endif /* __KERNEL__ */

/*
 **********************************************************************
 * gfsd datagram service
 **********************************************************************
 */

int gfs_client_datagram_timeouts[] = { /* milli seconds */
	8000, 12000
};
int gfs_client_datagram_ntimeouts =
	GFARM_ARRAY_LENGTH(gfs_client_datagram_timeouts);

/*
 * `server_addr_size' should be socklen_t, but that requires <sys/socket.h>
 * for all source files which include "gfs_client.h".
 */
gfarm_error_t
gfs_client_get_load_request(int sock,
	struct sockaddr *server_addr, int server_addr_size)
{
	int rv;
	gfarm_int32_t command = GFS_UDP_PROTO_LOADAV_REQUEST;

	if (server_addr == NULL || server_addr_size == 0) {
		/* using connected UDP socket */
		rv = write(sock, &command, sizeof(command));
	} else {
		rv = sendto(sock, &command, sizeof(command), 0,
		    server_addr, server_addr_size);
	}
	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001247,
			"write or send operation on socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * `*server_addr_sizep' is an IN/OUT parameter.
 *
 * `*server_addr_sizep' should be socklen_t, but that requires <sys/socket.h>
 * for all source files which include "gfs_client.h".
 */
gfarm_error_t
gfs_client_get_load_result(int sock,
	struct sockaddr *server_addr, socklen_t *server_addr_sizep,
	struct gfs_client_load *result)
{
	int rv;
	double loadavg[3];
#ifndef WORDS_BIGENDIAN
	struct { char c[8]; } nloadavg[3];
#else
#	define nloadavg loadavg
#endif

	if (server_addr == NULL || server_addr_sizep == NULL) {
		/* caller doesn't need server_addr */
		rv = read(sock, nloadavg, sizeof(nloadavg));
	} else {
		rv = recvfrom(sock, nloadavg, sizeof(nloadavg), 0,
		    server_addr, server_addr_sizep);
	}
	if (rv == -1) {
		int save_errno = errno;
		gflog_debug(GFARM_MSG_1001248,
			"read or receive operation from socket failed: %s",
			strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}

	if (result != NULL) {
#ifndef WORDS_BIGENDIAN
		swab(&nloadavg[0], &loadavg[0], sizeof(loadavg[0]));
		swab(&nloadavg[1], &loadavg[1], sizeof(loadavg[1]));
		swab(&nloadavg[2], &loadavg[2], sizeof(loadavg[2]));
#endif
		result->loadavg_1min = loadavg[0];
		result->loadavg_5min = loadavg[1];
		result->loadavg_15min = loadavg[2];
	}
	return (GFARM_ERR_NO_ERROR);
}

/*
 * multiplexed version of gfs_client_get_load()
 */

struct gfs_client_get_load_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable, *readable;
	void (*continuation)(void *);
	void *closure;

	int sock;
	int try;

	/* results */
	gfarm_error_t error;

	int get_load;
	struct gfs_client_load load;
};

static void
gfs_client_get_load_send(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_get_load_state *state = closure;
	int rv;
	struct timeval timeout;

	state->error = gfs_client_get_load_request(state->sock, NULL, 0);
	if (state->error == GFARM_ERR_NO_ERROR) {
		timeout.tv_sec = timeout.tv_usec = 0;
		gfarm_timeval_add_microsec(&timeout,
		    gfs_client_datagram_timeouts[state->try] *
		    GFARM_MILLISEC_BY_MICROSEC);
		if ((rv = gfarm_eventqueue_add_event(state->q, state->readable,
		    &timeout)) == 0) {
			/* go to gfs_client_get_load_receive() */
			return;
		}
		state->error = gfarm_errno_to_error(rv);
	}
	close(state->sock);
	state->sock = -1;
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

static void
gfs_client_get_load_receive(int events, int fd, void *closure,
	const struct timeval *t)
{
	struct gfs_client_get_load_state *state = closure;
	int rv;

	if ((events & GFARM_EVENT_TIMEOUT) != 0) {
		assert(events == GFARM_EVENT_TIMEOUT);
		++state->try;
		if (state->try >= gfs_client_datagram_ntimeouts) {
			state->error = GFARM_ERR_OPERATION_TIMED_OUT;
		} else if ((rv = gfarm_eventqueue_add_event(state->q,
		    state->writable, NULL)) == 0) {
			/* go to gfs_client_get_load_send() */
			return;
		} else {
			state->error = gfarm_errno_to_error(rv);
		}
	} else {
		assert(events == GFARM_EVENT_READ);
		state->error = gfs_client_get_load_result(
		    state->sock, NULL, NULL,
		    state->get_load ? &state->load : NULL);
	}
	close(state->sock);
	state->sock = -1;
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_get_load_request_multiplexed(struct gfarm_eventqueue *q,
	struct sockaddr *peer_addr, socklen_t peer_addrlen,
	void (*continuation)(void *), void *closure,
	struct gfs_client_get_load_state **statepp, int get_load)
{
	gfarm_error_t e;
	int rv, sock;
	struct gfs_client_get_load_state *state;

	/* use different socket for each peer, to identify error code */
	sock = socket(peer_addr->sa_family, SOCK_DGRAM, 0);
	if (sock == -1) {
		e = gfarm_errno_to_error(errno);
		goto error_return;
	}
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/*
	 * workaround linux UDP behavior
	 * that select(2)/poll(2)/epoll(2) returns
	 * that the socket is readable, but it may be not.
	 *
	 * from http://stackoverflow.com/questions/4381430/
	 * What are the WONTFIX bugs on GNU/Linux and how to
	 * work around them?
	 *
	 * The Linux UDP select bug: select (and related interfaces)
	 * flag a UDP socket file descriptor ready
	 * for reading as soon as a packet has been received,
	 * without confirming the checksum. On subsequent
	 * recv/read/etc., if the checksum was invalid, the
	 * call will block.
	 * Working around this requires always setting UDP sockets to
	 * non-blocking mode and dealing with
	 * the EWOULDBLOCK condition.
	 */
	if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) /* should success always */
		gflog_warning(GFARM_MSG_UNFIXED,
		    "gfs_client_get_load_request_multiplexed(): "
		    "set nonblock: %s", strerror(errno));

	/* connect UDP socket, to get error code */
	if (connect(sock, peer_addr, peer_addrlen) == -1) {
		e = gfarm_errno_to_error(errno);
		goto error_close_sock;
	}

	GFARM_MALLOC(state);
	if (state == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001249,
			"allocation of client get load state failed: %s",
			gfarm_error_string(e));
		goto error_close_sock;
	}

	state->writable = gfarm_fd_event_alloc(
	    GFARM_EVENT_WRITE, sock,
	    gfs_client_get_load_send, state);
	if (state->writable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001250,
			"allocation of client get load state->writable "
			"failed: %s",
			gfarm_error_string(e));
		goto error_free_state;
	}
	/*
	 * We cannot use two independent events (i.e. a fd_event with
	 * GFARM_EVENT_READ flag and a timer_event) here, because
	 * it's possible that both event handlers are called at once.
	 */
	state->readable = gfarm_fd_event_alloc(
	    GFARM_EVENT_READ|GFARM_EVENT_TIMEOUT, sock,
	    gfs_client_get_load_receive, state);
	if (state->readable == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001251,
			"allocation of client get load state->readable "
			"failed: %s",
			gfarm_error_string(e));
		goto error_free_writable;
	}
	/* go to gfs_client_get_load_send() */
	rv = gfarm_eventqueue_add_event(q, state->writable, NULL);
	if (rv != 0) {
		e = gfarm_errno_to_error(rv);
		gflog_debug(GFARM_MSG_1001252,
			"adding event to event queue failed: %s",
			gfarm_error_string(e));
		goto error_free_readable;
	}

	state->get_load = get_load;
	state->q = q;
	state->continuation = continuation;
	state->closure = closure;
	state->sock = sock;
	state->try = 0;
	state->error = GFARM_ERR_NO_ERROR;
	*statepp = state;
	return (GFARM_ERR_NO_ERROR);

error_free_readable:
	gfarm_event_free(state->readable);
error_free_writable:
	gfarm_event_free(state->writable);
error_free_state:
	free(state);
error_close_sock:
	close(sock);
error_return:
	return (e);
}

gfarm_error_t
gfs_client_get_load_result_multiplexed(
	struct gfs_client_get_load_state *state, struct gfs_client_load *loadp)
{
	gfarm_error_t error = state->error;

	if (state->sock >= 0) { /* sanity */
		close(state->sock);
		state->sock = -1;
	}
	if (error == GFARM_ERR_NO_ERROR)
		*loadp = state->load;
	gfarm_event_free(state->readable);
	gfarm_event_free(state->writable);
	free(state);
	return (error);
}

#ifdef HAVE_INFINIBAND
gfarm_error_t
gfs_ib_rdma_exch_info(struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	struct rdma_context *ctx = gfs_ib_rdma_context(gfs_server);
	int success;
	gfarm_uint32_t server_lid;
	gfarm_uint32_t server_qpn;
	gfarm_uint32_t server_psn;
	void	*server_gid = gfs_rdma_get_remote_gid(ctx);
	gfarm_uint32_t local_lid = gfs_rdma_get_local_lid(ctx);
	gfarm_uint32_t local_qpn = gfs_rdma_get_local_qpn(ctx);
	gfarm_uint32_t local_psn = gfs_rdma_get_local_psn(ctx);
	void	*local_gid = gfs_rdma_get_local_gid(ctx);
	size_t	size = gfs_rdma_get_gid_size();

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_RDMA_EXCH_INFO,
		"iiib/iiiib",
		local_lid, local_qpn, local_psn, size, local_gid,
		&success, &server_lid, &server_qpn, &server_psn,
		size, &size, server_gid);

	if (e == GFARM_ERR_NO_ERROR && success) {
		gfs_rdma_set_remote_lid(ctx, server_lid);
		gfs_rdma_set_remote_qpn(ctx, server_qpn);
		gfs_rdma_set_remote_psn(ctx, server_psn);

		gfs_rdma_enable(ctx);

		gflog_debug(GFARM_MSG_1004546,
			"gfs_ib_rdma_exch_info:%s success",
				gfs_server->hostname);
	} else {
		gflog_debug(GFARM_MSG_1004547,
			"gfs_ib_rdma_exch_info:%s failed, %s",
				gfs_server->hostname, gfarm_error_string(e));
		if (e == GFARM_ERR_NO_ERROR)
			e = GFARM_ERR_DEVICE_NOT_CONFIGURED;

		gfs_rdma_disable(ctx);
	}

	return (e);
}
gfarm_error_t
gfs_ib_rdma_hello(struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	struct rdma_context *ctx = gfs_ib_rdma_context(gfs_server);
	gfarm_uint32_t rkey = gfs_rdma_get_rkey(gfs_rdma_get_mr(ctx));
	unsigned char *buf;
	gfarm_uint64_t addr;
	int size;

	buf = gfs_rdma_get_buffer(ctx);
	size = gfs_rdma_get_gid_size();
	memcpy(buf, gfs_rdma_get_local_gid(ctx), size);
	addr = (uintptr_t)buf;

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_RDMA_HELLO, "iil/",
				rkey, size, addr);
	if (e == GFARM_ERR_NO_ERROR) {
		gfs_rdma_enable(ctx);
		gflog_debug(GFARM_MSG_1004548,
				"gfs_ib_rdma_hello: success");
	} else {
		gflog_debug(GFARM_MSG_1004549,
			"gfs_ib_rdma_hello: failed, %s",
			gfarm_error_string(e));
		gfs_rdma_disable(ctx);
	}
	return (e);
}

gfarm_error_t
gfs_ib_rdma_pread(struct gfs_connection *gfs_server,
		gfarm_int32_t fd, void *buffer, size_t size,
		gfarm_off_t off, size_t *np, gfarm_uint32_t rkey)
{
	gfarm_error_t e;
	gfarm_uint64_t addr = (uintptr_t)buffer;
	gfarm_uint32_t n;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_RDMA_PREAD, "iilil/i",
		fd, (int)size, off, rkey, addr, &n)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004550,
					"gfs_client_rpc() failed: %s",
					gfarm_error_string(e));
		return (e);
	}

	*np = n;

	if (*np > size) {
		gflog_debug(GFARM_MSG_1004551,
			"Protocol error in client rdma_pread (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PREAD_PROTOCOL);
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_ib_rdma_pwrite(struct gfs_connection *gfs_server,
			gfarm_int32_t fd, const void *buffer, size_t size,
			gfarm_off_t off, size_t *np, gfarm_uint32_t rkey)
{
	gfarm_error_t e;
	gfarm_uint64_t addr = (uintptr_t)buffer;
	gfarm_uint32_t n;

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_RDMA_PWRITE, "iilil/i",
			fd, size, off, rkey, addr, &n)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004552, "gfs_client_rpc() failed: %s",
				gfarm_error_string(e));
		return (e);
	}

	*np = n;

	if (*np > size) {
		gflog_debug(GFARM_MSG_1004553,
			"Protocol error in client rdma_pwrite (%llu)>(%llu)",
			(unsigned long long)*np, (unsigned long long)size);
		return (GFARM_ERRMSG_GFS_PROTO_PWRITE_PROTOCOL);
	}

	return (GFARM_ERR_NO_ERROR);
}

struct rdma_context *
gfs_ib_rdma_context(struct gfs_connection *gfs_server)
{
	return (gfs_server->rdma_ctx);
}

static void
gfs_ib_rdma_free(struct gfs_connection *gfs_server)
{
	if (gfs_server->rdma_ctx) {
		gfs_rdma_finish(gfs_server->rdma_ctx);
		gfs_server->rdma_ctx = NULL;
	}
}

static void
gfs_ib_rdma_connect(struct gfs_connection *gfs_server)
{
	struct rdma_context *ctx = gfs_ib_rdma_context(gfs_server);

	if (!gfs_rdma_check(ctx))
		return;
	if (gfs_ib_rdma_exch_info(gfs_server) != GFARM_ERR_NO_ERROR)
		return;
	if (gfs_rdma_connect(ctx) != GFARM_ERR_NO_ERROR)
		return;
	gfs_ib_rdma_hello(gfs_server);
}
#else
struct rdma_context *
gfs_ib_rdma_context(struct gfs_connection *gfs_server)
{
	return (NULL);
}
static void
gfs_ib_rdma_free(struct gfs_connection *gfs_server)
{
}
#endif

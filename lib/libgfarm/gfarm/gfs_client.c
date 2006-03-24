/*
 * $Id$
 */

#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <netinet/in.h>
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
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfevent.h"
#include "hash.h"

#include "liberror.h"
#include "iobuffer.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "host.h"
#include "param.h"
#include "sockopt.h"
#include "auth.h"
#include "config.h"
#include "conn_hash.h"
#include "gfs_proto.h"
#include "gfs_client.h"

#define XAUTH_NEXTRACT_MAXLEN	512

struct gfs_connection {
	struct gfs_connection *next, *prev; /* doubly linked circular list */

	struct gfp_xdr *conn;
	enum gfarm_auth_method auth_method;
	struct gfarm_hash_entry *hash_entry;

	int is_local;

	/* reference counters */
	int acquired;
	int opened;

	void *context; /* work area for RPC (esp. GFS_PROTO_COMMAND) */
};

/* doubly linked circular list head to see LRU connection */
static struct gfs_connection connection_list_head = {
	&connection_list_head, &connection_list_head
};

static int free_connections = 0;

#define MAXIMUM_FREE_CONNECTIONS 10

#define SERVER_HASHTAB_SIZE	3079	/* prime number */

static struct gfarm_hash_table *gfs_server_hashtab = NULL;

void
gfs_client_terminate(void)
{
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	struct gfs_connection *gfs_server;

	if (gfs_server_hashtab == NULL)
		return;
	for (gfarm_hash_iterator_begin(gfs_server_hashtab, &it);
	     !gfarm_hash_iterator_is_end(&it); ) {
		entry = gfarm_hash_iterator_access(&it);
		gfs_server = gfarm_hash_entry_data(entry);

		gfp_xdr_free(gfs_server->conn);
		gfp_conn_hash_iterator_purge(&it);
	}
	gfarm_hash_table_free(gfs_server_hashtab);
	gfs_server_hashtab = NULL;
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
	return (gfp_conn_hash_hostname(gfs_server->hash_entry));
}

static void
gfs_client_connection_gc_internal(int free_target)
{
	struct gfs_connection *gfs_server;

	/* search least recently used connection */
	for (gfs_server = connection_list_head.prev;
	    free_connections > free_target;
	    gfs_server = gfs_server->prev) {
		/* sanity check */
		if (gfs_server == &connection_list_head) {
			fprintf(stderr, "free connections/target = %d/%d\n",
			    free_connections, free_target);
			fprintf(stderr, "But no free connection is found.\n");
			fprintf(stderr, "This shouldn't happen\n");
			abort();
		}

		if (gfs_server->acquired <= 0) {
			/* free this connection */
			gfs_server->next->prev = gfs_server->prev;
			gfs_server->prev->next = gfs_server->next;
			gfp_xdr_free(gfs_server->conn);
			gfp_conn_hash_purge(gfs_server_hashtab,
			    gfs_server->hash_entry);
			--free_connections;
		}
	}
}

void
gfs_client_connection_gc(void)
{
	gfs_client_connection_gc_internal(0);
}

static gfarm_error_t
gfs_client_connection0(const char *canonical_hostname,
	struct sockaddr *peer_addr, int port,
	struct gfs_connection *gfs_server)
{
	gfarm_error_t e;
	char *host_fqdn;
	int sock;

	if (gfarm_canonical_hostname_is_local(canonical_hostname)) {
		struct sockaddr_un peer_un;
		socklen_t socklen;

		sock = socket(PF_UNIX, SOCK_STREAM, 0);
		if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
			gfs_client_connection_gc();
			sock = socket(PF_UNIX, SOCK_STREAM, 0);
		}
		if (sock == -1)
			return (gfarm_errno_to_error(errno));
		fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

		memset(&peer_un, 0, sizeof(peer_un));
		socklen = snprintf(peer_un.sun_path, sizeof(peer_un.sun_path),
		    GFSD_LOCAL_SOCKET_NAME, port);
		peer_un.sun_family = AF_UNIX;
#ifdef SUN_LEN /* derived from 4.4BSD */
		socklen = SUN_LEN(&peer_un);
#else
		socklen += sizeof(peer_un) - sizeof(peer_un.sun_path);
#endif
		if (connect(sock, (struct sockaddr *)&peer_un, socklen) < 0) {
			e = gfarm_errno_to_error(errno);
			close(sock);
			return (e);
		}
		gfs_server->is_local = 1;
	} else {
		sock = socket(PF_INET, SOCK_STREAM, 0);
		if (sock == -1 && (errno == ENFILE || errno == EMFILE)) {
			gfs_client_connection_gc();
			sock = socket(PF_INET, SOCK_STREAM, 0);
		}
		if (sock == -1)
			return (gfarm_errno_to_error(errno));
		fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

		/* XXX - how to report setsockopt(2) failure ? */
		gfarm_sockopt_apply_by_name_addr(sock, canonical_hostname,
		    peer_addr);

		if (connect(sock, peer_addr, sizeof(*peer_addr)) < 0) {
			e = gfarm_errno_to_error(errno);
			close(sock);
			return (e);
		}
		gfs_server->is_local = 0;
	}
	e = gfp_xdr_new_fd(sock, &gfs_server->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		close(sock);
		return (e);
	}
	gfs_server->context = NULL;
	/*
	 * the reason why we call strdup() is because
	 * gfarm_auth_request() may break static work area of `*hp'.
	 * XXX - now it's not hp->h_name, but canonical_hostname.
	 */
	host_fqdn = strdup(canonical_hostname);
	if (host_fqdn == NULL) {
		gfp_xdr_free(gfs_server->conn);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfarm_auth_request(gfs_server->conn,
	    GFS_SERVICE_TAG, host_fqdn, peer_addr, gfarm_get_auth_id_type(),
	    &gfs_server->auth_method);
	free(host_fqdn);
	if (e != GFARM_ERR_NO_ERROR) {
		gfp_xdr_free(gfs_server->conn);
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

void
gfs_client_connection_free(struct gfs_connection *gfs_server)
{
	if (--gfs_server->acquired > 0)
		return; /* shouln't be freed */

	/* sanity check */
	if (gfs_server->opened > 0) {
		gflog_error("gfs_server->acquired = %d, but\n",
		    gfs_server->acquired);
		gflog_error("gfs_server->opened = %d.\n",
		    gfs_server->opened);
		gflog_error("This shouldn't happen\n");
		abort();
	}

	++free_connections;

	gfs_client_connection_gc_internal(MAXIMUM_FREE_CONNECTIONS);
}

/* update the LRU list to mark this gfs_server recently used */
static void
gfs_client_connection_used(struct gfs_connection *gfs_server)
{
	gfs_server->next->prev = gfs_server->prev;
	gfs_server->prev->next = gfs_server->next;

	gfs_server->next = connection_list_head.next;
	gfs_server->prev = &connection_list_head;
	connection_list_head.next->prev = gfs_server;
	connection_list_head.next = gfs_server;
}

/*
 * XXX FIXME
 * `hostname' to `addr' conversion really should be done in this function,
 * rather than a caller of this function.
 */
gfarm_error_t
gfs_client_connection_acquire(const char *canonical_hostname, int port,
	struct sockaddr *peer_addr, struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e;
	struct gfarm_hash_entry *entry;
	struct gfs_connection *gfs_server;
	int created;

	e = gfp_conn_hash_enter(&gfs_server_hashtab, SERVER_HASHTAB_SIZE,
	    sizeof(*gfs_server),
	    canonical_hostname, port, gfarm_get_global_username(),
	    &entry, &created);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	gfs_server = gfarm_hash_entry_data(entry);
	if (created) {
		e = gfs_client_connection0(canonical_hostname, peer_addr, port,
		    gfs_server);
		if (e != GFARM_ERR_NO_ERROR) {
			gfp_conn_hash_purge(gfs_server_hashtab, entry);
			return (e);
		}
		gfs_server->hash_entry = entry;
		gfs_server->next = connection_list_head.next;
		gfs_server->prev = &connection_list_head;
		connection_list_head.next->prev = gfs_server;
		connection_list_head.next = gfs_server;
		gfs_server->acquired = 1;
	} else {
		if (gfs_server->acquired == 0) /* now this isn't free */
			--free_connections;
		gfs_server->acquired++;
		gfs_client_connection_used(gfs_server);
	}
	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

int
gfs_client_connection_is_local(struct gfs_connection *gfs_server)
{
	return (gfs_server->is_local);
}

#if 0 /* XXX FIXME - disable multiplexed version for now */

/*
 * multiplexed version of gfs_client_connect() for parallel authentication
 */

struct gfs_client_connect_state {
	struct gfarm_eventqueue *q;
	struct gfarm_event *writable;
	struct sockaddr peer_addr;
	void (*continuation)(void *);
	void *closure;

	struct gfs_connection *gfs_server;

	struct gfarm_auth_request_state *auth_state;

	/* results */
	gfarm_error_t error;
};

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
	int error;
	socklen_t error_size = sizeof(error);
	int rv = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size);

	if (rv == -1) { /* Solaris, see UNP by rstevens */
		state->error = gfarm_errno_to_error(errno);
	} else if (error != 0) {
		state->error = gfarm_errno_to_error(error);
	} else { /* successfully connected */
		state->error = gfarm_auth_request_multiplexed(state->q,
		    state->gfs_server->conn, GFS_SERVICE_TAG,
		    state->gfs_server->hostname, &state->peer_addr,
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (state->error == NULL) {
			/*
			 * call auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return;
		}
	}
	if (state->continuation != NULL)
		(*state->continuation)(state->closure);
}

gfarm_error_t
gfs_client_connect_request_multiplexed(struct gfarm_eventqueue *q,
	const char *canonical_hostname, struct sockaddr *peer_addr,
	void (*continuation)(void *), void *closure,
	struct gfs_client_connect_state **statepp)
{
	gfarm_error_t e;
	int rv, sock;
	struct gfs_connection *gfs_server;
	struct gfs_client_connect_state *state;
	int connection_in_progress;

	/* clone of gfs_client_connection0() */

	/* XXX FIXME: use PF_UNIX, if possible */
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		return (gfarm_errno_to_error(errno));
	fcntl(sock, F_SETFD, 1); /* automatically close() on exec(2) */

	/* XXX - how to report setsockopt(2) failure ? */
	gfarm_sockopt_apply_by_name_addr(sock, canonical_hostname, peer_addr);

	fcntl(sock, F_SETFL, O_NONBLOCK);
	if (connect(sock, peer_addr, sizeof(*peer_addr)) < 0) {
		if (errno != EINPROGRESS) {
			e = gfarm_errno_to_error(errno);
			close(sock);
			return (e);
		}
		connection_in_progress = 1;
	} else {
		connection_in_progress = 0;
	}
	fcntl(sock, F_SETFL, 0); /* clear O_NONBLOCK */

	gfs_server = malloc(sizeof(*gfs_server));
	if (gfs_server == NULL) {
		close(sock);
		return (GFARM_ERR_NO_MEMORY);
	}
	gfs_server->is_local = 0;

	e = gfp_xdr_fd_connection_new(sock, &gfs_server->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		free(gfs_server);
		close(sock);
		return (e);
	}
	gfs_server->context = NULL;
	gfs_server->hostname = strdup(canonical_hostname);
	if (gfs_server->hostname == NULL) {
		gfp_xdr_free(gfs_server->conn);
		free(gfs_server);
		return (GFARM_ERR_NO_MEMORY);
	}

	state = malloc(sizeof(*state));
	if (state == NULL) {
		free(gfs_server->hostname);
		gfp_xdr_free(gfs_server->conn);
		free(gfs_server);
		return (GFARM_ERR_NO_MEMORY);
	}
	state->q = q;
	state->peer_addr = *peer_addr;
	state->continuation = continuation;
	state->closure = closure;
	state->gfs_server = gfs_server;
	state->auth_state = NULL;
	state->error = NULL;
	if (connection_in_progress) {
		state->writable = gfarm_fd_event_alloc(GFARM_EVENT_WRITE,
		    sock, gfs_client_connect_start_auth, state);
		if (state->writable == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else if ((rv = gfarm_eventqueue_add_event(q, state->writable,
		    NULL)) == 0) {
			*statepp = state;
			/* go to gfs_client_connect_start_auth() */
			return (NULL);
		} else {
			e = gfarm_errno_to_error(rv);
			gfarm_event_free(state->writable);
		}
	} else {
		state->writable = NULL;
		e = gfarm_auth_request_multiplexed(q,
		    gfs_server->conn, GFS_SERVICE_TAG,
		    gfs_server->hostname, &state->peer_addr,
		    gfs_client_connect_end_auth, state,
		    &state->auth_state);
		if (e == GFARM_ERR_NO_ERROR) {
			*statepp = state;
			/*
			 * call gfarm_auth_request,
			 * then go to gfs_client_connect_end_auth()
			 */
			return (NULL);
		}
	}
	free(state);
	free(gfs_server->hostname);
	gfp_xdr_free(gfs_server->conn);
	free(gfs_server);
	return (e);
}

gfarm_error_t
gfs_client_connect_result_multiplexed(struct gfs_client_connect_state *state,
	struct gfs_connection **gfs_serverp)
{
	gfarm_error_t e = state->error;
	struct gfs_connection *gfs_server = state->gfs_server;

	if (state->writable != NULL)
		gfarm_event_free(state->writable);
	free(state);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_client_disconnect(gfs_server);
		return (e);
	}
	*gfs_serverp = gfs_server;
	return (GFARM_ERR_NO_ERROR);
}

#endif /* XXX FIXME - disable multiplexed version for now */

/*
 * gfs_client RPC
 */

int
gfarm_fd_receive_message(int fd, void *buffer, size_t size,
	int fdc, int *fdv)
{
	int i, rv;
	struct iovec iov[1];
	struct msghdr msg;
#ifdef SCM_RIGHTS /* 4.3BSD Reno or later */
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
#ifndef SCM_RIGHTS
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
#ifdef SCM_RIGHTS /* 4.3BSD Reno or later */
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
	return (e);
}

gfarm_error_t
gfs_client_rpc_result(struct gfs_connection *gfs_server, int just,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	va_start(ap, format);
	e = gfp_xdr_vrpc_result(gfs_server->conn, just,
				  &errcode, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_rpc(struct gfs_connection *gfs_server, int just, int command,
	const char *format, ...)
{
	va_list ap;
	gfarm_error_t e;
	int errcode;

	va_start(ap, format);
	e = gfp_xdr_vrpc(gfs_server->conn, just,
	    command, &errcode, &format, &ap);
	va_end(ap);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (errcode != 0) {
		/*
		 * We just use gfarm_error_t as the errcode,
		 * Note that GFARM_ERR_NO_ERROR == 0.
		 */
		return (errcode);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_process_set(struct gfs_connection *gfs_server,
	gfarm_int32_t type, size_t length, const char *key, gfarm_pid_t pid)
{
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_PROCESS_SET, "ibl/",
	    type, length, key, pid));
}

gfarm_error_t
gfs_client_open(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	gfs_client_connection_used(gfs_server);

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_OPEN, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		++gfs_server->opened;
	return (e);
}

gfarm_error_t
gfs_client_open_local(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	int *fd_ret)
{
	gfarm_error_t e;
	int rv, local_fd;

	if (!gfs_server->is_local)
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	gfs_client_connection_used(gfs_server);

	e = gfs_client_rpc_request(gfs_server, GFS_PROTO_OPEN_LOCAL, "i", fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* layering violation, but... */
	rv = gfarm_fd_receive_message(gfp_xdr_fd(gfs_server->conn),
	    &e, sizeof(e), 1, &local_fd);
	if (rv != sizeof(e))
		return (GFARM_ERR_PROTOCOL);
	/* both `e' and `local_fd` are passed by using host byte order. */
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	*fd_ret = local_fd;
	++gfs_server->opened;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_close(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfarm_error_t e;

	gfs_client_connection_used(gfs_server);

	e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_CLOSE, "i/", fd);
	if (e == GFARM_ERR_NO_ERROR)
		--gfs_server->opened;
	return (e);
}

gfarm_error_t
gfs_client_pread(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, void *buffer, size_t size,
	gfarm_off_t off, size_t *np)
{
	gfarm_error_t e;

	gfs_client_connection_used(gfs_server);

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PREAD, "iil/b",
	    fd, (int)size, off,
	    size, np, buffer)) != GFARM_ERR_NO_ERROR)
		return (e);
	if (*np > size)
		return (GFARM_ERRMSG_GFS_PROTO_PREAD_PROTOCOL);
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

	gfs_client_connection_used(gfs_server);

	if ((e = gfs_client_rpc(gfs_server, 0, GFS_PROTO_PWRITE, "ibl/i",
	    fd, size, buffer, off, &n)) != GFARM_ERR_NO_ERROR)
		return (e);
	*np = n;
	if (n > size)
		return (GFARM_ERRMSG_GFS_PROTO_PWRITE_PROTOCOL);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_ftruncate(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_off_t size)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FTRUNCATE, "il/",
	    fd, size));
}

gfarm_error_t
gfs_client_fsync(struct gfs_connection *gfs_server,
	gfarm_int32_t fd, gfarm_int32_t op)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSYNC, "ii/",
	    fd, op));
}

gfarm_error_t
gfs_client_fstat(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t *size,
	gfarm_int64_t *atime_sec, gfarm_int32_t *atime_nsec,
	gfarm_int64_t *mtime_sec, gfarm_int32_t *mtime_nsec)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_FSTAT, "i/llili",
	    fd, size, atime_sec, atime_nsec, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfs_client_cksum_set(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	const char *cksum_type, size_t length, const char *cksum)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_CKSUM_SET, "isb/",
	    fd, cksum_type, length, cksum));
}

gfarm_error_t
gfs_client_lock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_trylock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_TRYLOCK, "illii/",
	    fd, start, len, type, whence));
}

gfarm_error_t
gfs_client_unlock(struct gfs_connection *gfs_server, gfarm_int32_t fd,
	gfarm_off_t start, gfarm_off_t len,
	gfarm_int32_t type, gfarm_int32_t whence)
{
	gfs_client_connection_used(gfs_server);

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
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_LOCK_INFO,
	    "illii/llisl",
	    fd, start, len, type, whence,
	    start_ret, len_ret, type_ret, host_ret, pid_ret));
}

gfarm_error_t
gfs_client_replica_add(struct gfs_connection *gfs_server, gfarm_int32_t fd)
{
	gfs_client_connection_used(gfs_server);

	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_REPLICA_ADD, "i/",
	    fd));
}


/*
 * GFS_PROTO_REPLICA_RECV is only used by gfsd,
 * thus, we define the client protocol at gfsd instead of here.
 */

/*
 **********************************************************************
 * Implementation of gfs_client_command()
 **********************************************************************
 */

struct gfs_client_command_context {
	struct gfarm_iobuffer *iobuffer[NFDESC];

	enum { GFS_COMMAND_SERVER_STATE_NEUTRAL,
		       GFS_COMMAND_SERVER_STATE_OUTPUT,
		       GFS_COMMAND_SERVER_STATE_EXITED,
		       GFS_COMMAND_SERVER_STATE_ABORTED }
		server_state;
	int server_output_fd;
	int server_output_residual;
	enum { GFS_COMMAND_CLIENT_STATE_NEUTRAL,
		       GFS_COMMAND_CLIENT_STATE_OUTPUT }
		client_state;
	int client_output_residual;

	int pid;
	int pending_signal;
};

void
gfs_client_command_set_stdin(struct gfs_connection *gfs_server,
	int (*rf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_read(cc->iobuffer[FDESC_STDIN], rf, cookie, fd);
}

void
gfs_client_command_set_stdout(struct gfs_connection *gfs_server,
	int (*wf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void (*wcf)(struct gfarm_iobuffer *, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_write(cc->iobuffer[FDESC_STDOUT], wf, cookie, fd);
	gfarm_iobuffer_set_write_close(cc->iobuffer[FDESC_STDOUT], wcf);

}

void
gfs_client_command_set_stderr(struct gfs_connection *gfs_server,
	int (*wf)(struct gfarm_iobuffer *, void *, int, void *, int),
	void (*wcf)(struct gfarm_iobuffer *, void *, int),
	void *cookie, int fd)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_write(cc->iobuffer[FDESC_STDERR], wf, cookie, fd);
	gfarm_iobuffer_set_write_close(cc->iobuffer[FDESC_STDERR], wcf);
}

gfarm_error_t
gfs_client_command_request(struct gfs_connection *gfs_server,
	char *path, char **argv, char **envp,
	int flags, int *pidp)
{
	struct gfs_client_command_context *cc;
	int na = argv == NULL ? 0 : gfarm_strarray_length(argv);
	int ne = envp == NULL ? 0 : gfarm_strarray_length(envp);
	int conn_fd = gfp_xdr_fd(gfs_server->conn);
	int i, xenv_copy, xauth_copy;
	gfarm_int32_t pid;
	socklen_t siz;
	gfarm_error_t e;

	static char *xdisplay_name_cache = NULL;
	static char *xdisplay_env_cache = NULL;
	static int xauth_cached = 0;
	static char *xauth_cache = NULL;
	char *dpy;

	if ((dpy = getenv("DISPLAY")) != NULL && xdisplay_name_cache == NULL &&
	    (flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) != 0) {
		/*
		 * get $DISPLAY to `xdisplay_name_cache',
		 * and set "DISPLAY=$DISPLAY" to `xdisplay_env_cache'.
		 */
		static char xdisplay_env_format[] = "DISPLAY=%s";
		static char local_prefix[] = "unix:";
		char *prefix;

		if (*dpy == ':') {
			prefix = gfarm_host_get_self_name();
		} else if (memcmp(dpy, local_prefix,
				  sizeof(local_prefix) - 1) == 0) {
			prefix = gfarm_host_get_self_name();
			dpy += sizeof(local_prefix) - 1 - 1;
		} else {
			prefix = "";
		}
		xdisplay_name_cache = malloc(strlen(prefix) + strlen(dpy) + 1);
		if (xdisplay_name_cache == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(xdisplay_name_cache, "%s%s", prefix, dpy);
		xdisplay_env_cache = malloc(sizeof(xdisplay_env_format) +
					    strlen(xdisplay_name_cache));
		if (xdisplay_env_cache == NULL) {
			free(xdisplay_name_cache);
			xdisplay_name_cache = NULL;
			return (GFARM_ERR_NO_MEMORY);
		}
		sprintf(xdisplay_env_cache, xdisplay_env_format,
			xdisplay_name_cache);

	}
	if ((flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) ==
	    GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY &&
	    xdisplay_name_cache != NULL && !xauth_cached) {
		/*
		 * get xauth data to `xauth_cache'
		 */
		static char xauth_command_format[] =
			"%s nextract - %s 2>/dev/null";
		char *xauth_command;
		FILE *fp;
		char *s, line[XAUTH_NEXTRACT_MAXLEN];

		xauth_command =
			malloc(sizeof(xauth_command_format) +
			       strlen(XAUTH_COMMAND) +
			       strlen(xdisplay_name_cache));
		if (xauth_command == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(xauth_command, xauth_command_format,
			XAUTH_COMMAND, xdisplay_name_cache);
		if ((fp = popen(xauth_command, "r")) == NULL) {
			free(xauth_command);
			return (GFARM_ERR_NO_MEMORY);
		}
		s = fgets(line, sizeof line, fp);
		pclose(fp);
		free(xauth_command);
		if (s != NULL) {
			xauth_cache = strdup(line);
			if (xauth_cache == NULL) {
				free(xdisplay_name_cache);
				return (GFARM_ERR_NO_MEMORY);
			}
		}
		xauth_cached = 1;
	}

	xenv_copy =
		(flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) != 0 &&
		xdisplay_env_cache != NULL;
	xauth_copy =
		(flags & GFS_CLIENT_COMMAND_FLAG_X11MASK) ==
		GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY &&
		xauth_cache != NULL;

	/*
	 * don't pass
	 * GFS_CLIENT_COMMAND_FLAG_STDIN_EOF and
	 * GFS_CLIENT_COMMAND_FLAG_XENV_COPY flag via network
	 */
	e = gfs_client_rpc_request(gfs_server,
		GFS_PROTO_COMMAND, "siii",
		path, na,
		ne + (xenv_copy ? 1 : 0),
		((flags & GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) ?
		GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND : 0) |
		(xauth_copy ? GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY : 0));
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/*
	 * argv
	 */
	for (i = 0; i < na; i++) {
		e = gfp_xdr_send(gfs_server->conn, "s", argv[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	/*
	 * envp
	 */
	for (i = 0; i < ne; i++) {
		e = gfp_xdr_send(gfs_server->conn, "s", envp[i]);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (xenv_copy) {
		e = gfp_xdr_send(gfs_server->conn, "s", xdisplay_env_cache);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (xauth_copy) {
		/*
		 * xauth
		 */
		e = gfp_xdr_send(gfs_server->conn, "s", xauth_cache);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	/* we have to set `just' flag here */
	e = gfs_client_rpc_result(gfs_server, 1, "i", &pid);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	cc = gfs_server->context =
		malloc(sizeof(struct gfs_client_command_context));
	if (gfs_server->context == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/*
	 * Now, we set the connection file descriptor non-blocking mode.
	 */
	if (fcntl(conn_fd, F_SETFL, O_NONBLOCK) == -1) {
		free(gfs_server->context);
		gfs_server->context = NULL;
		return (gfarm_errno_to_error(errno));
	}

	/*
	 * initialize gfs_server->context by default values
	 */
	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_SNDBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDIN] = gfarm_iobuffer_alloc(i);

	siz = sizeof(i);
	if (getsockopt(conn_fd, SOL_SOCKET, SO_RCVBUF, &i, &siz))
		i = GFARM_DEFAULT_COMMAND_IOBUF_SIZE;
	cc->iobuffer[FDESC_STDOUT] = gfarm_iobuffer_alloc(i);
	cc->iobuffer[FDESC_STDERR] = gfarm_iobuffer_alloc(i);

	gfarm_iobuffer_set_nonblocking_read_fd(
		cc->iobuffer[FDESC_STDIN], FDESC_STDIN);
	gfarm_iobuffer_set_nonblocking_write_xxx(
		cc->iobuffer[FDESC_STDIN], gfs_server->conn);

	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDOUT], gfs_server->conn);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDOUT], FDESC_STDOUT);

	gfarm_iobuffer_set_nonblocking_read_xxx(
		cc->iobuffer[FDESC_STDERR], gfs_server->conn);
	gfarm_iobuffer_set_nonblocking_write_fd(
		cc->iobuffer[FDESC_STDERR], FDESC_STDERR);

	if ((flags & GFS_CLIENT_COMMAND_FLAG_STDIN_EOF) != 0)
		gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);

	cc->server_state = GFS_COMMAND_SERVER_STATE_NEUTRAL;
	cc->client_state = GFS_COMMAND_CLIENT_STATE_NEUTRAL;
	cc->pending_signal = 0;

	*pidp = cc->pid = pid;

	return (e);
}

int
gfs_client_command_is_running(struct gfs_connection *gfs_server)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	return (cc->server_state != GFS_COMMAND_SERVER_STATE_EXITED &&
		cc->server_state != GFS_COMMAND_SERVER_STATE_ABORTED);
}

gfarm_error_t
gfs_client_command_fd_set(struct gfs_connection *gfs_server,
			  fd_set *readable, fd_set *writable, int *max_fdp)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	int conn_fd = gfp_xdr_fd(gfs_server->conn);
	int i, fd;

	gfs_client_connection_used(gfs_server);

	/*
	 * The following test condition should just match with
	 * the i/o condition in gfs_client_command_io_fd_set(),
	 * otherwise unneeded busy wait happens.
	 */

	if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL ||
	    (cc->server_state == GFS_COMMAND_SERVER_STATE_OUTPUT &&
	     gfarm_iobuffer_is_readable(cc->iobuffer[cc->server_output_fd]))) {
		FD_SET(conn_fd, readable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}
	if ((cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL &&
	     (cc->pending_signal ||
	      gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDIN]))) ||
	    cc->client_state == GFS_COMMAND_CLIENT_STATE_OUTPUT) {
		FD_SET(conn_fd, writable);
		if (*max_fdp < conn_fd)
			*max_fdp = conn_fd;
	}

	if (gfarm_iobuffer_is_readable(cc->iobuffer[FDESC_STDIN])) {
		fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[FDESC_STDIN]);
		if (fd < 0) {
			gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN], NULL);
			/* XXX - if the callback sets an error? */
		} else {
			FD_SET(fd, readable);
			if (*max_fdp < fd)
				*max_fdp = fd;
		}
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		if (gfarm_iobuffer_is_writable(cc->iobuffer[i])) {
			fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[i]);
			if (fd < 0) {
				gfarm_iobuffer_write(cc->iobuffer[i], NULL);
				/* XXX - if the callback sets an error? */
			} else {
				FD_SET(fd, writable);
				if (*max_fdp < fd)
					*max_fdp = fd;
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_command_io_fd_set(struct gfs_connection *gfs_server,
			     fd_set *readable, fd_set *writable)
{
	gfarm_error_t e;
	struct gfs_client_command_context *cc = gfs_server->context;
	int i, fd, conn_fd = gfp_xdr_fd(gfs_server->conn);

	fd = gfarm_iobuffer_get_read_fd(cc->iobuffer[FDESC_STDIN]);
	if (fd >= 0 && FD_ISSET(fd, readable)) {
		gfarm_iobuffer_read(cc->iobuffer[FDESC_STDIN], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[FDESC_STDIN]);
		if (e != GFARM_ERR_NO_ERROR) {
			/* treat this as eof */
			gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);
			/* XXX - how to report this error? */
			gfarm_iobuffer_set_error(cc->iobuffer[FDESC_STDIN],
			    GFARM_ERR_NO_ERROR);
		}
	}

	for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
		fd = gfarm_iobuffer_get_write_fd(cc->iobuffer[i]);
		if (fd < 0 || !FD_ISSET(fd, writable))
			continue;
		gfarm_iobuffer_write(cc->iobuffer[i], NULL);
		e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
		if (e == GFARM_ERR_NO_ERROR)
			continue;
		/* XXX - just purge the content */
		gfarm_iobuffer_purge(cc->iobuffer[i], NULL);
		/* XXX - how to report this error? */
		gfarm_iobuffer_set_error(cc->iobuffer[i], GFARM_ERR_NO_ERROR);
	}

	if (FD_ISSET(conn_fd, readable)) {
		if (cc->server_state == GFS_COMMAND_SERVER_STATE_NEUTRAL) {
			gfarm_int32_t cmd, fd, len;
			int eof;
			gfarm_error_t e;

			e = gfp_xdr_recv(gfs_server->conn, 1, &eof,
					   "i", &cmd);
			if (e != GFARM_ERR_NO_ERROR || eof) {
				if (e == GFARM_ERR_NO_ERROR)
					e = GFARM_ERRMSG_GFSD_ABORTED;
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				return (e);
			}
			switch (cmd) {
			case GFS_PROTO_COMMAND_EXITED:
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_EXITED;
				break;
			case GFS_PROTO_COMMAND_FD_OUTPUT:
				e = gfp_xdr_recv(gfs_server->conn, 1, &eof,
						   "ii", &fd, &len);
				if (e != GFARM_ERR_NO_ERROR || eof) {
					if (e == GFARM_ERR_NO_ERROR)
						e = GFARM_ERRMSG_GFSD_ABORTED;
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					return (e);
				}
				if (fd != FDESC_STDOUT && fd != FDESC_STDERR) {
					/* XXX - something wrong */
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					return (GFARM_ERRMSG_GFSD_DESCRIPTOR_ILLEGAL);
				}
				if (len <= 0) {
					/* notify closed */
					gfarm_iobuffer_set_read_eof(
					    cc->iobuffer[fd]);
				} else {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_OUTPUT;
					cc->server_output_fd = fd;
					cc->server_output_residual = len;
				}
				break;
			default:
				/* XXX - something wrong */
				cc->server_state =
				    GFS_COMMAND_SERVER_STATE_ABORTED;
				return (GFARM_ERRMSG_GFSD_REPLY_UNKNOWN);
			}
		} else if (cc->server_state==GFS_COMMAND_SERVER_STATE_OUTPUT) {
			gfarm_iobuffer_read(cc->iobuffer[cc->server_output_fd],
				&cc->server_output_residual);
			if (cc->server_output_residual == 0)
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[cc->server_output_fd]);
			if (e != GFARM_ERR_NO_ERROR) {
				/* treat this as eof */
				gfarm_iobuffer_set_read_eof(
				    cc->iobuffer[cc->server_output_fd]);
				gfarm_iobuffer_set_error(
				    cc->iobuffer[cc->server_output_fd],
				    GFARM_ERR_NO_ERROR);
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				return (e);
			}
			if (gfarm_iobuffer_is_read_eof(
					cc->iobuffer[cc->server_output_fd])) {
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				return (GFARM_ERRMSG_GFSD_ABORTED);
			}
		}
	}
	if (FD_ISSET(conn_fd, writable) &&
	    gfs_client_command_is_running(gfs_server)) {
		if (cc->client_state == GFS_COMMAND_CLIENT_STATE_NEUTRAL) {
			if (cc->pending_signal) {
				e = gfp_xdr_send(gfs_server->conn, "ii",
					GFS_PROTO_COMMAND_SEND_SIGNAL,
					cc->pending_signal);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(gfs_server->conn))
				    != GFARM_ERR_NO_ERROR) {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					return (e);
				}
			} else if (gfarm_iobuffer_is_writable(
			    cc->iobuffer[FDESC_STDIN])) {
				/*
				 * cc->client_output_residual may be 0,
				 * if stdin reaches EOF.
				 */
				cc->client_output_residual =
					gfarm_iobuffer_avail_length(
					    cc->iobuffer[FDESC_STDIN]);
				e = gfp_xdr_send(gfs_server->conn, "iii",
					GFS_PROTO_COMMAND_FD_INPUT,
					FDESC_STDIN,
					cc->client_output_residual);
				if (e != GFARM_ERR_NO_ERROR ||
				    (e = gfp_xdr_flush(gfs_server->conn))
				    != GFARM_ERR_NO_ERROR) {
					cc->server_state =
					    GFS_COMMAND_SERVER_STATE_ABORTED;
					return (e);
				}
				cc->client_state =
				    GFS_COMMAND_CLIENT_STATE_OUTPUT;
			}
		} else if (cc->client_state==GFS_COMMAND_CLIENT_STATE_OUTPUT) {
			gfarm_iobuffer_write(cc->iobuffer[FDESC_STDIN],
				&cc->client_output_residual);
			if (cc->client_output_residual == 0)
				cc->client_state =
					GFS_COMMAND_CLIENT_STATE_NEUTRAL;
			e = gfarm_iobuffer_get_error(
			    cc->iobuffer[FDESC_STDIN]);
			if (e != GFARM_ERR_NO_ERROR) {
				cc->server_state =
					GFS_COMMAND_SERVER_STATE_ABORTED;
				gfarm_iobuffer_set_error(
				    cc->iobuffer[FDESC_STDIN],
				    GFARM_ERR_NO_ERROR);
				return (e);
			}
		}
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfs_client_command_io(struct gfs_connection *gfs_server,
		      struct timeval *timeout)
{
	int nfound, max_fd;
	fd_set readable, writable;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!gfs_client_command_is_running(gfs_server))
		return (GFARM_ERR_NO_ERROR);

	max_fd = -1;
	FD_ZERO(&readable);
	FD_ZERO(&writable);

	gfs_client_command_fd_set(gfs_server, &readable, &writable, &max_fd);
	if (max_fd >= 0) {
		nfound = select(max_fd + 1,
				&readable, &writable, NULL, timeout);
		if (nfound > 0) {
			e = gfs_client_command_io_fd_set(gfs_server,
							 &readable, &writable);
		} else if (nfound == -1 && errno != EINTR) {
			e = gfarm_errno_to_error(errno);
		}
	}

	return (e);
}

int
gfs_client_command_send_stdin(struct gfs_connection *gfs_server,
			      void *data, int len)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	char *p = data;
	int residual = len, rv;

	while (residual > 0 && gfs_client_command_is_running(gfs_server)) {
		rv = gfarm_iobuffer_put(cc->iobuffer[FDESC_STDIN],
					p, residual);
		p += rv;
		residual -= rv;
		gfs_client_command_io(gfs_server, NULL);
		/* XXX - how to report this error? */
	}
	return (len - residual);
}

void
gfs_client_command_close_stdin(struct gfs_connection *gfs_server)
{
	struct gfs_client_command_context *cc = gfs_server->context;

	gfarm_iobuffer_set_read_eof(cc->iobuffer[FDESC_STDIN]);
}

gfarm_error_t
gfs_client_command_send_signal(struct gfs_connection *gfs_server, int sig)
{
	gfarm_error_t e;
	struct gfs_client_command_context *cc = gfs_server->context;

	while (gfs_client_command_is_running(gfs_server) &&
	       cc->pending_signal != 0) {
		e = gfs_client_command_io(gfs_server, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	if (!gfs_client_command_is_running(gfs_server))
		return (gfarm_errno_to_error(ESRCH));
	cc->pending_signal = sig;
	/* make a chance to send the signal immediately */
	return (gfs_client_command_io(gfs_server, NULL));
}

gfarm_error_t
gfs_client_command_result(struct gfs_connection *gfs_server,
	int *term_signal, int *exit_status, int *exit_flag)
{
	struct gfs_client_command_context *cc = gfs_server->context;
	gfarm_error_t e;

	while (gfs_client_command_is_running(gfs_server)) {
		gfs_client_command_io(gfs_server, NULL);
		/* XXX - how to report this error? */
	}

	/*
	 * flush stdout/stderr
	 */
	while (gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDOUT]) ||
	       gfarm_iobuffer_is_writable(cc->iobuffer[FDESC_STDERR])) {
		int i, nfound, fd, max_fd = -1;
		fd_set writable;

		FD_ZERO(&writable);
		for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
			if (gfarm_iobuffer_is_writable(cc->iobuffer[i])) {
				fd = gfarm_iobuffer_get_write_fd(
				    cc->iobuffer[i]);
				if (fd < 0) {
					gfarm_iobuffer_write(cc->iobuffer[i],
					    NULL);
					/*
					 * XXX - if the callback sets an error?
					 */
				} else {
					FD_SET(fd, &writable);
					if (max_fd < fd)
						max_fd = fd;
				}
			}
		}

		if (max_fd < 0)
			continue;

		nfound = select(max_fd + 1, NULL, &writable, NULL, NULL);
		if (nfound == -1 && errno != EINTR)
			break;

		if (nfound > 0) {
			for (i = FDESC_STDOUT; i <= FDESC_STDERR; i++) {
				fd = gfarm_iobuffer_get_write_fd(
				    cc->iobuffer[i]);
				if (fd < 0 || !FD_ISSET(fd, &writable))
					continue;
				gfarm_iobuffer_write(cc->iobuffer[i], NULL);
				e = gfarm_iobuffer_get_error(cc->iobuffer[i]);
				if (e == GFARM_ERR_NO_ERROR)
					continue;
				/* XXX - just purge the content */
				gfarm_iobuffer_purge(cc->iobuffer[i], NULL);
				/* XXX - how to report this error? */
				gfarm_iobuffer_set_error(cc->iobuffer[i],
				    GFARM_ERR_NO_ERROR);
			}
		}
	}

	/*
	 * context isn't needed anymore
	 */
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDIN]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDOUT]);
	gfarm_iobuffer_free(cc->iobuffer[FDESC_STDERR]);
	free(gfs_server->context);
	gfs_server->context = NULL;
	/*
	 * Now, we recover the connection file descriptor blocking mode.
	 */
	if (fcntl(gfp_xdr_fd(gfs_server->conn), F_SETFL, 0) == -1) {
		return (gfarm_errno_to_error(errno));
	}
	return (gfs_client_rpc(gfs_server, 0, GFS_PROTO_COMMAND_EXIT_STATUS,
			       "/iii",
			       term_signal, exit_status, exit_flag));
}

gfarm_error_t
gfs_client_command(struct gfs_connection *gfs_server,
	char *path, char **argv, char **envp, int flags,
	int *term_signal, int *exit_status, int *exit_flag)
{
	gfarm_error_t e, e2;
	int pid;

	e = gfs_client_command_request(gfs_server, path, argv, envp, flags,
				       &pid);
	if (e)
		return (e);
	while (gfs_client_command_is_running(gfs_server))
		e = gfs_client_command_io(gfs_server, NULL);
	e2 = gfs_client_command_result(gfs_server,
				       term_signal, exit_status, exit_flag);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

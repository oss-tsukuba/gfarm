#include <pthread.h>

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "auth.h"

#include "user.h"
#include "host.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "job.h"

struct peer {
	struct gfp_xdr *conn;

	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	struct user *user;
	struct host *host;
	struct process *process;
	int protocol_error;

	gfarm_int32_t fd_current, fd_saved;
	int flags;
#define PEER_FLAGS_FD_CURRENT_EXTERNALIZED	1
#define PEER_FLAGS_FD_SAVED_EXTERNALIZED	2

	union {
		struct {
			/* only used by "gfrun" client */
			struct job_table_entry *jobs;
		} client;
	} u;
};

static struct peer *peer_table;
static int peer_table_size;
static pthread_mutex_t peer_table_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
peer_table_lock(void)
{
	int err = pthread_mutex_lock(&peer_table_mutex);

	if (err != 0)
		gflog_warning("peer_table_lock: %s", strerror(err));
}

static void
peer_table_unlock(void)
{
	int err = pthread_mutex_unlock(&peer_table_mutex);

	if (err != 0)
		gflog_warning("peer_table_lock: %s", strerror(err));
}

void
peer_init(int max_peers)
{
	int i;
	struct peer *peer;

	GFARM_MALLOC_ARRAY(peer_table, max_peers);
	if (peer_table == NULL)
		gflog_fatal("peer table: %s", strerror(ENOMEM));
	peer_table_size = max_peers;

	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		peer->conn = NULL;
		peer->username = NULL;
		peer->hostname = NULL;
		peer->user = NULL;
		peer->host = NULL;
		peer->process = NULL;
		peer->protocol_error = 0;
		peer->fd_current = -1;
		peer->fd_saved = -1;
		peer->flags = 0;
		peer->u.client.jobs = NULL;
	}
}

gfarm_error_t
peer_alloc(int fd, struct peer **peerp)
{
	gfarm_error_t e;
	struct peer *peer;
	int sockopt;

	if (fd < 0)
		return (GFARM_ERR_INVALID_ARGUMENT);
	if (fd >= peer_table_size)
		return (GFARM_ERR_TOO_MANY_OPEN_FILES);
	peer_table_lock();
	peer = &peer_table[fd];
	if (peer->conn != NULL) { /* must be an implementation error */
		peer_table_unlock();
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	/* XXX FIXME gfp_xdr requires too much memory */
	e = gfp_xdr_new_socket(fd, &peer->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		peer_table_unlock();
		return (e);
	}
	peer->username = NULL;
	peer->hostname = NULL;
	peer->user = NULL;
	peer->host = NULL;
	peer->process = NULL;
	peer->protocol_error = 0;
	peer->fd_current = -1;
	peer->fd_saved = -1;
	peer->flags = 0;
	peer->u.client.jobs = NULL;

	/* deal with reboots or network problems */
	sockopt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &sockopt, sizeof(sockopt))
	    == -1)
		gflog_warning_errno("SO_KEEPALIVE");

	*peerp = peer;
	peer_table_unlock();
	return (GFARM_ERR_NO_ERROR);
}

/* caller should allocate the storage for username and hostname */
void
peer_authorized(struct peer *peer,
	enum gfarm_auth_id_type id_type, char *username, char *hostname,
	struct sockaddr *addr, enum gfarm_auth_method auth_method)
{
	peer->id_type = id_type;
	if (id_type == GFARM_AUTH_ID_TYPE_USER) {
		peer->user = user_lookup(username);
		if (peer->user != NULL) {
			free(username);
			peer->username = NULL;
		} else {
			peer->username = username;
		}
	} else {
		peer->user = NULL;
		peer->username = username;
	}
	peer->host = host_addr_lookup(hostname, addr);
	if (peer->host != NULL) {
		free(hostname);
		peer->hostname = NULL;
	} else {
		peer->hostname = hostname;
	}
	if (id_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST) {
		if (peer->host == NULL)
			gflog_warning("unknown host: %s", hostname);
		else
			gflog_debug("gfsd connected from %s",
				    host_name(peer->host));
	}
	/* We don't record auth_method for now */
}

void
peer_free(struct peer *peer)
{
	char *username, *hostname;

	peer_table_lock();

	username = peer_get_username(peer);
	hostname = peer_get_hostname(peer);

	/*XXX XXX*/
	while (peer->u.client.jobs != NULL)
		job_table_remove(job_get_id(peer->u.client.jobs), username,
		    &peer->u.client.jobs);
	peer->u.client.jobs = NULL;

	gflog_notice("(%s@%s) disconnected", username, hostname);

	peer->protocol_error = 0;
	if (peer->process != NULL) {
		process_detach_peer(peer->process, peer);
		peer->process = NULL;
	}

	peer->user = NULL;
	peer->host = NULL;
	if (peer->username != NULL) {
		free(peer->username); peer->username = NULL;
	}
	if (peer->hostname != NULL) {
		free(peer->hostname); peer->hostname = NULL;
	}

	gfp_xdr_free(peer->conn); peer->conn = NULL;

	peer_table_unlock();
}

void
peer_shutdown_all(void)
{
	int i;
	struct peer *peer;

	/* We never unlock this mutex any more */
	peer_table_lock();

	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		if (peer->process == NULL)
			continue;

		gflog_notice("(%s@%s) shutting down",
		    peer->username, peer->hostname);
		process_detach_peer(peer->process, peer);
		peer->process = NULL;
	}
}

#if 0

struct peer *
peer_by_fd(int fd)
{
	peer_table_lock();
	if (fd < 0 || fd >= peer_table_size || peer_table[fd].conn == NULL)
		return (NULL);
	peer_table_unlock();
	return (&peer_table[fd]);
}

gfarm_error_t
peer_free_by_fd(int fd)
{
	struct peer *peer = peer_by_fd(fd);

	if (peer == NULL)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	peer_free(peer);
	return (GFARM_ERR_NO_ERROR);
}

#endif /* 0 */

struct gfp_xdr *
peer_get_conn(struct peer *peer)
{
	return (peer->conn);
}

#if 0
int
peer_get_fd(struct peer *peer)
{
	int fd = peer - peer_table;

	if (fd < 0 || fd >= peer_table_size)
		gflog_fatal("peer_get_fd: invalid peer pointer");
	return (fd);
}
#endif

/*
 * This funciton is experimentally introduced to accept a host in
 * private networks.
 */
gfarm_error_t
peer_set_host(struct peer *peer, char *hostname)
{
	if (peer->id_type != GFARM_AUTH_ID_TYPE_SPOOL_HOST)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (peer->host != NULL) /* already set */
		return (GFARM_ERR_NO_ERROR);

	peer->host = host_lookup(hostname);
	if (peer->host == NULL)
		return (GFARM_ERR_UNKNOWN_HOST);

	if (peer->hostname != NULL) {
		free(peer->hostname);
		peer->hostname = NULL;
	}
	gflog_debug("gfsd connected from %s", host_name(peer->host));
	return (GFARM_ERR_NO_ERROR);
}

char *
peer_get_username(struct peer *peer)
{
	return (peer->user != NULL ? user_name(peer->user) : peer->username);
}

char *
peer_get_hostname(struct peer *peer)
{
	return (peer->host != NULL ? host_name(peer->host) : peer->hostname);
}

struct user *
peer_get_user(struct peer *peer)
{
	return (peer->user);
}

void
peer_set_user(struct peer *peer, struct user *user)
{
	if (peer->user != NULL)
		gflog_fatal("peer_set_user: overriding user");
	peer->user = user;
}

struct host *
peer_get_host(struct peer *peer)
{
	return (peer->host);
}

struct process *
peer_get_process(struct peer *peer)
{
	return (peer->process);
}

void
peer_set_process(struct peer *peer, struct process *process)
{
	if (peer->process != NULL)
		gflog_fatal("peer_set_process: overriding process");
	peer->process = process;
	process_attach_peer(process, peer);
}

void
peer_unset_process(struct peer *peer)
{
	if (peer->process == NULL)
		gflog_fatal("peer_unset_process: already unset");

	peer_fdpair_clear(peer);

	process_detach_peer(peer->process, peer);
	peer->process = NULL;
}

void
peer_record_protocol_error(struct peer *peer)
{
	peer->protocol_error = 1;
}

int
peer_had_protocol_error(struct peer *peer)
{
	return (peer->protocol_error);
}

struct job_table_entry **
peer_get_jobs_ref(struct peer *peer)
{
	return (&peer->u.client.jobs);
}

void
peer_fdpair_clear(struct peer *peer)
{
	if (peer->process == NULL) {
		assert(peer->fd_current == -1 && peer->fd_saved == -1);
		return;
	}
	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current);
	}
	if (peer->fd_saved != -1 &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0) {
		process_close_file(peer->process, peer, peer->fd_saved);
	}
	peer->fd_current = -1;
	peer->fd_saved = -1;
	peer->flags &= ~(
	    PEER_FLAGS_FD_CURRENT_EXTERNALIZED |
	    PEER_FLAGS_FD_SAVED_EXTERNALIZED);
}

gfarm_error_t
peer_fdpair_externalize_current(struct peer *peer)
{
	if (peer->fd_current == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	peer->flags |= PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	if (peer->fd_current == peer->fd_saved)
		peer->flags |= PEER_FLAGS_FD_SAVED_EXTERNALIZED;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_close_current(struct peer *peer)
{
	if (peer->fd_current == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	if (peer->fd_current == peer->fd_saved) {
		peer->flags &= ~PEER_FLAGS_FD_SAVED_EXTERNALIZED;
		peer->fd_saved = -1;
	}
	peer->flags &= ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	peer->fd_current = -1;
	return (GFARM_ERR_NO_ERROR);
}

void
peer_fdpair_set_current(struct peer *peer, gfarm_int32_t fd)
{
	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current);
	}
	peer->flags &= ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	peer->fd_current = fd;
}

gfarm_error_t
peer_fdpair_get_current(struct peer *peer, gfarm_int32_t *fdp)
{
	if (peer->fd_current == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	*fdp = peer->fd_current;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_get_saved(struct peer *peer, gfarm_int32_t *fdp)
{
	if (peer->fd_saved == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	*fdp = peer->fd_saved;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_save(struct peer *peer)
{
	if (peer->fd_current == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	if (peer->fd_saved != -1 &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0 &&
	    peer->fd_saved != peer->fd_current) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_saved);
	}
	peer->fd_saved = peer->fd_current;
	peer->flags = (peer->flags & ~PEER_FLAGS_FD_SAVED_EXTERNALIZED) |
	    ((peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) ?
	     PEER_FLAGS_FD_SAVED_EXTERNALIZED : 0);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_restore(struct peer *peer)
{
	if (peer->fd_saved == -1)
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);

	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current);
	}
	peer->fd_current = peer->fd_saved;
	peer->flags = (peer->flags & ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED) |
	    ((peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) ?
	     PEER_FLAGS_FD_CURRENT_EXTERNALIZED : 0);
	return (GFARM_ERR_NO_ERROR);
}

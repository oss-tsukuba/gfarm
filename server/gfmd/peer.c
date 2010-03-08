/*
 * $Id: peer.c 4457 2010-02-23 01:53:23Z ookuma$
 */

#include <gfarm/gfarm_config.h>

#include <pthread.h>

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <signal.h> /* for sig_atomic_t */
#ifdef HAVE_EPOLL_WAIT
#include <sys/epoll.h>
#else
#include <poll.h>
#endif

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "io_fd.h"
#include "auth.h"

#include "subr.h"
#include "thrsubr.h"
#include "thrpool.h"
#include "user.h"
#include "host.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "job.h"

#include "protocol_state.h"

struct peer_closing_queue {
	pthread_mutex_t mutex;
	pthread_cond_t ready_to_close;

	struct peer *head, **tail;
} peer_closing_queue = {
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	NULL,
	&peer_closing_queue.head
};

struct peer {
	struct peer *next_close;
	int refcount;

	struct gfp_xdr *conn;
	gfp_xdr_async_peer_t async;	/* used by back_channel only */

	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	struct user *user;
	struct host *host;

	struct process *process;
	int protocol_error;
	void *(*protocol_handler)(void *);
	struct thread_pool *handler_thread_pool;

	volatile sig_atomic_t control;
#define PEER_AUTHORIZED 1
#define PEER_WATCHING	2

	struct protocol_state pstate;

	gfarm_int32_t fd_current, fd_saved;
	int flags;
#define PEER_FLAGS_FD_CURRENT_EXTERNALIZED	1
#define PEER_FLAGS_FD_SAVED_EXTERNALIZED	2

	void *findxmlattrctx;

	/* only one pending GFM_PROTO_GENERATION_UPDATED per peer is allowed */
	struct inode *pending_new_generation;

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

#ifdef HAVE_EPOLL_WAIT
struct {
	int fd;
	struct epoll_event *events;
	int nevents;
} peer_epoll;
#else
static struct pollfd *peer_poll_fds;
#endif

static struct thread_pool *peer_default_thread_pool;
static void *(*peer_default_protocol_handler)(void *);

static void
peer_table_lock(void)
{
	int err = pthread_mutex_lock(&peer_table_mutex);

	if (err != 0)
		gflog_warning(GFARM_MSG_1000273,
		    "peer_table_lock: %s", strerror(err));
}

static void
peer_table_unlock(void)
{
	int err = pthread_mutex_unlock(&peer_table_mutex);

	if (err != 0)
		gflog_warning(GFARM_MSG_1000274,
		    "peer_table_unlock: %s", strerror(err));
}

#ifdef HAVE_EPOLL_WAIT
static void
peer_epoll_ctl_fd(int op, int fd)
{
	struct epoll_event ev = { 0, { 0 }};

	ev.data.fd = fd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(peer_epoll.fd, op, fd, &ev) == -1)
		gflog_fatal(GFARM_MSG_1000275,
		    "epoll_ctl: %s\n", strerror(errno));
}

static void
peer_epoll_add_fd(int fd)
{
	peer_epoll_ctl_fd(EPOLL_CTL_ADD, fd);
}

static void
peer_epoll_del_fd(int fd)
{
	peer_epoll_ctl_fd(EPOLL_CTL_DEL, fd);
}
#endif

#define PEER_WATCH_INTERVAL 10 /* 10ms: XXX FIXME */

void *
peer_watcher(void *arg)
{
	struct peer *peer;
	int i, rv, skip, nfds;
#ifdef HAVE_EPOLL_WAIT
	int efd;
#else
	struct pollfd *fd;
#endif

	for (;;) {
#ifdef HAVE_EPOLL_WAIT
		rv = nfds = epoll_wait(peer_epoll.fd, peer_epoll.events,
				peer_epoll.nevents, PEER_WATCH_INTERVAL);
#else
		nfds = 0;
		peer_table_lock();
		for (i = 0; i < peer_table_size; i++) {
			peer = &peer_table[i];
			if (peer->conn == NULL ||
			    peer->control != (PEER_AUTHORIZED|PEER_WATCHING))
				continue;
			fd = &peer_poll_fds[nfds++];
			fd->fd = i;
			fd->events = POLLIN;
			fd->revents = 0;
		}
		peer_table_unlock();

		rv = poll(peer_poll_fds, nfds, PEER_WATCH_INTERVAL);
#endif
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
#ifdef HAVE_EPOLL_WAIT
			gflog_fatal(GFARM_MSG_1000276,
			    "peer_watcher: epoll_wait: %s\n",
			    strerror(errno));
#else
			gflog_fatal(GFARM_MSG_1000277,
			    "peer_watcher: poll: %s\n",
			    strerror(errno));
#endif

		for (i = 0; i < nfds; i++) {
#ifdef HAVE_EPOLL_WAIT
			efd = peer_epoll.events[i].data.fd;
			peer = &peer_table[efd];
#else
			if (rv == 0)
				break; /* all processed */
			fd = &peer_poll_fds[i];
			peer = &peer_table[fd->fd];
#endif
			giant_lock();
			peer_table_lock();
			skip = peer->conn == NULL ||
			    peer->control != (PEER_AUTHORIZED|PEER_WATCHING);
			peer_table_unlock();
			giant_unlock();
			if (skip)
				continue;
#ifdef HAVE_EPOLL_WAIT
			if (peer_epoll.events[i].events & EPOLLIN) {
#else
			if (fd->revents & POLLIN) {
#endif
				/*
				 * This peer is not running at this point,
				 * so it's ok to modify peer->control.
				 */
				peer->control &= ~PEER_WATCHING;
#ifdef HAVE_EPOLL_WAIT
				peer_epoll_del_fd(efd);
#endif
				/*
				 * We shouldn't have giant_lock or
				 * peer_table_lock here.
				 */
				thrpool_add_job(peer->handler_thread_pool,
				    peer->protocol_handler, peer);
				rv--;
			}
		}
	}
}

void
peer_add_ref(struct peer *peer)
{
	static const char diag[] = "peer_add_ref";

	mutex_lock(&peer_closing_queue.mutex, diag, "peer_closing_queue");
	++peer->refcount;
	mutex_unlock(&peer_closing_queue.mutex, diag, "peer_closing_queue");
}

int
peer_del_ref(struct peer *peer)
{
	int referenced;
	static const char diag[] = "peer_del_ref";

	mutex_lock(&peer_closing_queue.mutex, diag, "peer_closing_queue");

	if (--peer->refcount > 0) {
		referenced = 1;
	} else {
		referenced = 0;
		cond_signal(&peer_closing_queue.ready_to_close, diag,
		    "ready to close");
	}

	mutex_unlock(&peer_closing_queue.mutex, diag, "peer_closing_queue");

	return (referenced);
}

void *
peer_closer(void *arg)
{
	struct peer *peer, **prev;
	static const char diag[] = "peer_closer";

	mutex_lock(&peer_closing_queue.mutex, diag, "peer_closing_queue");

	for (;;) {
		while (peer_closing_queue.head == NULL)
			cond_wait(&peer_closing_queue.ready_to_close,
			    &peer_closing_queue.mutex,
			    diag, "queue is not empty");

		for (prev = &peer_closing_queue.head;
		    (peer = *prev) != NULL; prev = &peer->next_close) {
			if (peer->refcount == 0) {
				*prev = peer->next_close;
				if (peer_closing_queue.tail ==
				    &peer->next_close)
					peer_closing_queue.tail = prev;
				break;
			}
		}
		if (peer == NULL) {
			cond_wait(&peer_closing_queue.ready_to_close,
			    &peer_closing_queue.mutex,
			    diag, "ready to close");
			continue;
		}

		mutex_unlock(&peer_closing_queue.mutex, diag, "before giant");

		giant_lock();
		/*
		 * NOTE: this shouldn't need db_begin()/db_end()
		 * at least for now,
		 * because only externalized descriptor needs the calls.
		 */
		peer_free(peer);
		giant_unlock();

		mutex_lock(&peer_closing_queue.mutex, diag, "after giant");
	}

	mutex_unlock(&peer_closing_queue.mutex, diag, "peer_closing_queue");
}

void
peer_free_request(struct peer *peer)
{
	int fd = peer_get_fd(peer), rv;
	static const char diag[] = "peer_free_request";

	mutex_lock(&peer_closing_queue.mutex, diag, "peer_closing_queue");

	/*
	 * wake up threads which may be sleeping at read() or write(), because
	 * they may be holding host_sender_lock() or host_receiver_lock(), but
	 * without closing the descriptor, because that leads race condition.
	 */
	rv = shutdown(fd, SHUT_RDWR);
	if (rv == -1)
		gflog_warning(GFARM_MSG_UNFIXED, 
		    "back_channel: shutdown(%d): %s", fd, strerror(errno));

	*peer_closing_queue.tail = peer;
	peer->next_close = NULL;
	peer_closing_queue.tail = &peer->next_close;

	mutex_unlock(&peer_closing_queue.mutex, diag, "peer_closing_queue");
}

void
peer_init(int max_peers,
	struct thread_pool *thrpool, void *(*protocol_handler)(void *))
{
	int i;
	struct peer *peer;
	gfarm_error_t e;

	GFARM_MALLOC_ARRAY(peer_table, max_peers);
	if (peer_table == NULL)
		gflog_fatal(GFARM_MSG_1000278,
		    "peer table: %s", strerror(ENOMEM));
	peer_table_size = max_peers;

	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		peer->next_close = NULL;
		peer->refcount = 0;
		peer->conn = NULL;
		peer->async = NULL;
		peer->username = NULL;
		peer->hostname = NULL;
		peer->user = NULL;
		peer->host = NULL;
		peer->process = NULL;
		peer->protocol_error = 0;
		peer->control = 0;
		peer->fd_current = -1;
		peer->fd_saved = -1;
		peer->flags = 0;
		peer->findxmlattrctx = NULL;
		peer->pending_new_generation = NULL;
		peer->u.client.jobs = NULL;
	}

#ifdef HAVE_EPOLL_WAIT
	peer_epoll.fd = epoll_create(max_peers);
	if (peer_epoll.fd == -1)
		gflog_fatal(GFARM_MSG_1000279,
		    "epoll_create: %s\n", strerror(errno));
	GFARM_MALLOC_ARRAY(peer_epoll.events, max_peers);
	if (peer_epoll.events == NULL)
		gflog_fatal(GFARM_MSG_1000280,
		    "peer epoll event table: %s", strerror(ENOMEM));
	peer_epoll.nevents = max_peers;
#else
	GFARM_MALLOC_ARRAY(peer_poll_fds, max_peers);
	if (peer_poll_fds == NULL)
		gflog_fatal(GFARM_MSG_1000281,
		    "peer pollfd table: %s", strerror(ENOMEM));
#endif
	peer_default_protocol_handler = protocol_handler;
	peer_default_thread_pool = thrpool;
	e = create_detached_thread(peer_watcher, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000282,
		    "create_detached_thread(peer_watcher): %s",
			    gfarm_error_string(e));

	e = create_detached_thread(peer_closer, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000282,
		    "create_detached_thread(peer_closer): %s",
			    gfarm_error_string(e));
}

gfarm_error_t
peer_alloc(int fd, struct peer **peerp)
{
	gfarm_error_t e;
	struct peer *peer;
	int sockopt;

	if (fd < 0) {
		gflog_debug(GFARM_MSG_1001580,
			"invalid argument 'fd'");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (fd >= peer_table_size) {
		gflog_debug(GFARM_MSG_1001581,
			"too many open files: fd >= peer_table_size");
		return (GFARM_ERR_TOO_MANY_OPEN_FILES);
	}
	peer_table_lock();
	peer = &peer_table[fd];
	if (peer->conn != NULL) { /* must be an implementation error */
		peer_table_unlock();
		gflog_debug(GFARM_MSG_1001582,
			"bad file descriptor: conn is NULL");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	peer->next_close = NULL;
	peer->refcount = 0;

	/* XXX FIXME gfp_xdr requires too much memory */
	e = gfp_xdr_new_socket(fd, &peer->conn);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001583,
			"gfp_xdr_new_socket() failed: %s",
			gfarm_error_string(e));
		peer_table_unlock();
		return (e);
	}

	peer->async = NULL; /* synchronous protocol by default */
	peer->username = NULL;
	peer->hostname = NULL;
	peer->user = NULL;
	peer->host = NULL;
	peer->process = NULL;
	peer->protocol_error = 0;
	peer->protocol_handler = peer_default_protocol_handler;
	peer->handler_thread_pool = peer_default_thread_pool;
	peer->control = 0;
	peer->fd_current = -1;
	peer->fd_saved = -1;
	peer->flags = 0;
	peer->findxmlattrctx = NULL;
	peer->u.client.jobs = NULL;

	/* deal with reboots or network problems */
	sockopt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &sockopt, sizeof(sockopt))
	    == -1)
		gflog_warning_errno(GFARM_MSG_1000283, "SO_KEEPALIVE");

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
		if (user_is_active(peer->user)) {
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
			gflog_warning(GFARM_MSG_1000284,
			    "unknown host: %s", hostname);
		else
			gflog_debug(GFARM_MSG_1000285,
			    "gfsd connected from %s",
				    host_name(peer->host));
	}
	/* We don't record auth_method for now */

	peer->control = PEER_AUTHORIZED;
	if (gfp_xdr_recv_is_ready(peer_get_conn(peer)))
		thrpool_add_job(peer->handler_thread_pool,
		    peer->protocol_handler, peer);
	else
		peer_watch_access(peer);
}

/* NOTE: caller of this function should acquire giant_lock as well */
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

	gflog_notice(GFARM_MSG_1000286,
	    "(%s@%s) disconnected", username, hostname);

	peer_unset_pending_new_generation(peer);

	peer->control = 0;

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
	peer->findxmlattrctx = NULL;

	if (peer->async != NULL) {
		gfp_xdr_async_peer_free(peer->async, NULL, NULL);
		peer->async = NULL;
	}

	gfp_xdr_free(peer->conn); peer->conn = NULL;
	peer->next_close = NULL;
	peer->refcount = 0;

	peer_table_unlock();
}

/* NOTE: caller of this function should acquire giant_lock as well */
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

		gflog_notice(GFARM_MSG_1000287, "(%s@%s) shutting down",
		    peer->username, peer->hostname);
#if 0		/* we don't really have to do this at shutdown */
		peer_unset_pending_new_generation(peer);
#endif
		process_detach_peer(peer->process, peer);
		peer->process = NULL;
	}
#ifdef HAVE_EPOLL_WAIT
	close(peer_epoll.fd);
#endif
}

void
peer_watch_access(struct peer *peer)
{
	peer->control |= PEER_WATCHING;
#ifdef HAVE_EPOLL_WAIT
	peer_epoll_add_fd(peer_get_fd(peer));
#endif
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

/* NOTE: caller of this function should acquire giant_lock as well */
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

int
peer_get_fd(struct peer *peer)
{
	int fd = peer - peer_table;

	if (fd < 0 || fd >= peer_table_size)
		gflog_fatal(GFARM_MSG_1000288,
		    "peer_get_fd: invalid peer pointer");
	return (fd);
}

void
peer_set_async(struct peer *peer, gfp_xdr_async_peer_t async)
{
	peer->async = async;
}

gfp_xdr_async_peer_t
peer_get_async(struct peer *peer)
{
	return (peer->async);
}

/*
 * This funciton is experimentally introduced to accept a host in
 * private networks.
 */
gfarm_error_t
peer_set_host(struct peer *peer, char *hostname)
{
	if (peer->id_type != GFARM_AUTH_ID_TYPE_SPOOL_HOST) {
		gflog_debug(GFARM_MSG_1001584,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (peer->host != NULL) { /* already set */
		gflog_debug(GFARM_MSG_1001585,
			"peer host is already set");
		return (GFARM_ERR_NO_ERROR);
	}

	peer->host = host_lookup(hostname);
	if (peer->host == NULL) {
		gflog_debug(GFARM_MSG_1001586,
			"host does not exist");
		return (GFARM_ERR_UNKNOWN_HOST);
	}

	if (peer->hostname != NULL) {
		free(peer->hostname);
		peer->hostname = NULL;
	}
	gflog_debug(GFARM_MSG_1000289,
	    "gfsd connected from %s", host_name(peer->host));
	return (GFARM_ERR_NO_ERROR);
}

enum gfarm_auth_id_type
peer_get_auth_id_type(struct peer *peer)
{
	return (peer->id_type);
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
		gflog_fatal(GFARM_MSG_1000290,
		    "peer_set_user: overriding user");
	peer->user = user;
}

struct host *
peer_get_host(struct peer *peer)
{
	return (peer->host);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_set_pending_new_generation(struct peer *peer, struct inode *inode)
{
	peer->pending_new_generation = inode;
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_reset_pending_new_generation(struct peer *peer)
{
	peer->pending_new_generation = NULL;
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_unset_pending_new_generation(struct peer *peer)
{
	if (peer->pending_new_generation != NULL)
		inode_new_generation_done(peer->pending_new_generation, peer,
		    GFARM_ERR_PROTOCOL);
}

struct process *
peer_get_process(struct peer *peer)
{
	return (peer->process);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_set_process(struct peer *peer, struct process *process)
{
	if (peer->process != NULL)
		gflog_fatal(GFARM_MSG_1000291,
		    "peer_set_process: overriding process");
	peer->process = process;
	process_attach_peer(process, peer);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_unset_process(struct peer *peer)
{
	if (peer->process == NULL)
		gflog_fatal(GFARM_MSG_1000292,
		    "peer_unset_process: already unset");

	peer_unset_pending_new_generation(peer);

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

void
peer_set_protocol_handler(struct peer *peer,
	struct thread_pool *thrpool, void *(*protocol_handler)(void *))
{
	peer->protocol_handler = protocol_handler;
	peer->handler_thread_pool = thrpool;
}

struct protocol_state *
peer_get_protocol_state(struct peer *peer)
{
	return (&peer->pstate); /* we only provide storage space here */
}

struct job_table_entry **
peer_get_jobs_ref(struct peer *peer)
{
	return (&peer->u.client.jobs);
}

/* NOTE: caller of this function should acquire giant_lock as well */
/*
 * NOTE: this shouldn't need db_begin()/db_end() calls at least for now,
 * because only externalized descriptor needs the calls.
 */
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
	if (peer->fd_current == -1) {
		gflog_debug(GFARM_MSG_1001587,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	peer->flags |= PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	if (peer->fd_current == peer->fd_saved)
		peer->flags |= PEER_FLAGS_FD_SAVED_EXTERNALIZED;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_close_current(struct peer *peer)
{
	if (peer->fd_current == -1) {
		gflog_debug(GFARM_MSG_1001588,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (peer->fd_current == peer->fd_saved) {
		peer->flags &= ~PEER_FLAGS_FD_SAVED_EXTERNALIZED;
		peer->fd_saved = -1;
	}
	peer->flags &= ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	peer->fd_current = -1;
	return (GFARM_ERR_NO_ERROR);
}

/* NOTE: caller of this function should acquire giant_lock as well */
/*
 * NOTE: this shouldn't need db_begin()/db_end() calls at least for now,
 * because only externalized descriptor needs the calls.
 */
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
	if (peer->fd_current == -1) {
		gflog_debug(GFARM_MSG_1001589,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	*fdp = peer->fd_current;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_fdpair_get_saved(struct peer *peer, gfarm_int32_t *fdp)
{
	if (peer->fd_saved == -1) {
		gflog_debug(GFARM_MSG_1001590,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	*fdp = peer->fd_saved;
	return (GFARM_ERR_NO_ERROR);
}

/* NOTE: caller of this function should acquire giant_lock as well */
/*
 * NOTE: this shouldn't need db_begin()/db_end() calls at least for now,
 * because only externalized descriptor needs the calls.
 */
gfarm_error_t
peer_fdpair_save(struct peer *peer)
{
	if (peer->fd_current == -1) {
		gflog_debug(GFARM_MSG_1001591,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

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

/* NOTE: caller of this function should acquire giant_lock as well */
/*
 * NOTE: this shouldn't need db_begin()/db_end() calls at least for now,
 * because only externalized descriptor needs the calls.
 */
gfarm_error_t
peer_fdpair_restore(struct peer *peer)
{
	if (peer->fd_saved == -1) {
		gflog_debug(GFARM_MSG_1001592,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

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

void
peer_findxmlattrctx_set(struct peer *peer, void *ctx)
{
	peer->findxmlattrctx = ctx;
}

void *
peer_findxmlattrctx_get(struct peer *peer)
{
	return peer->findxmlattrctx;
}

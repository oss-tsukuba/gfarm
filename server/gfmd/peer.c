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
#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#else
#include <poll.h>
#endif

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "thrsubr.h"

#include "gfp_xdr.h"
#include "io_fd.h"
#include "auth.h"
#include "config.h" /* gfarm_simultaneous_replication_receivers */

#include "subr.h"
#include "thrpool.h"
#include "user.h"
#include "abstract_host.h"
#include "host.h"
#include "mdhost.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "job.h"

#include "protocol_state.h"

#define BACK_CHANNEL_DIAG(peer) (peer_get_auth_id_type(peer) == \
	GFARM_AUTH_ID_TYPE_SPOOL_HOST ? "back_channel" : "gfmd_channel")

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
	gfp_xdr_async_peer_t async; /* used by {back|gfmd}_channel */
	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	struct user *user;
	struct abstract_host *host;

	struct process *process;
	int protocol_error;
	void *(*protocol_handler)(void *);
	struct thread_pool *handler_thread_pool;

	volatile sig_atomic_t control;
#define PEER_WATCHING	1
#define PEER_INVOKING	2 /* block peer_free till protocol_handler is called */
#define PEER_CLOSING	4 /* prevent protocol_handler from being called */
	pthread_mutex_t control_mutex;

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

	/* the followings are only used for gfsd back channel */
	pthread_mutex_t replication_mutex;
	int simultaneous_replication_receivers;
	struct file_replicating replicating_inodes; /* dummy header */
};

static struct peer *peer_table;
static int peer_table_size;
static pthread_mutex_t peer_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char peer_table_diag[] = "peer_table";

#ifdef HAVE_EPOLL
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
static void (*peer_async_free)(struct peer *, gfp_xdr_async_peer_t) = NULL;

void
peer_set_free_async(void (*async_free)(struct peer *, gfp_xdr_async_peer_t))
{
	peer_async_free = async_free;
}

void
file_replicating_set_handle(struct file_replicating *fr, gfarm_int64_t handle)
{
	fr->handle = handle;
}

gfarm_int64_t
file_replicating_get_handle(struct file_replicating *fr)
{
	return (fr->handle);
}

struct peer *
file_replicating_get_peer(struct file_replicating *fr)
{
	return (fr->peer);
}

/* only host_replicating_new() is allowed to call this routine */
gfarm_error_t
peer_replicating_new(struct peer *peer, struct host *dst,
	struct file_replicating **frp)
{
	struct file_replicating *fr;
	static const char diag[] = "peer_replicating_new";
	static const char replication_diag[] = "replication";

	GFARM_MALLOC(fr);
	if (fr == NULL)
		return (GFARM_ERR_NO_MEMORY);

	fr->peer = peer;
	fr->dst = dst;
	fr->handle = -1;

	/* the followings should be initialized by inode_replicating() */
	fr->prev_host = fr;
	fr->next_host = fr;

	gfarm_mutex_lock(&peer->replication_mutex, diag, replication_diag);
	if (peer->simultaneous_replication_receivers >=
	    gfarm_simultaneous_replication_receivers) {
		free(fr);
		fr = NULL;
	} else {
		++peer->simultaneous_replication_receivers;
		fr->prev_inode = &peer->replicating_inodes;
		fr->next_inode = peer->replicating_inodes.next_inode;
		peer->replicating_inodes.next_inode = fr;
		fr->next_inode->prev_inode = fr;
	}
	gfarm_mutex_unlock(&peer->replication_mutex, diag, replication_diag);

	if (fr == NULL)
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	*frp = fr;
	return (GFARM_ERR_NO_ERROR);
}

/* only file_replicating_free() is allowed to call this routine */
void
peer_replicating_free(struct file_replicating *fr)
{
	struct peer *peer = fr->peer;
	static const char diag[] = "peer_replicating_free";
	static const char replication_diag[] = "replication";

	gfarm_mutex_lock(&peer->replication_mutex, diag, replication_diag);
	--peer->simultaneous_replication_receivers;
	fr->prev_inode->next_inode = fr->next_inode;
	fr->next_inode->prev_inode = fr->prev_inode;
	gfarm_mutex_unlock(&peer->replication_mutex, diag, replication_diag);
	free(fr);
}

gfarm_error_t
peer_replicated(struct peer *peer,
	struct host *host, gfarm_ino_t ino, gfarm_int64_t gen,
	gfarm_int64_t handle,
	gfarm_int32_t src_errcode, gfarm_int32_t dst_errcode, gfarm_off_t size)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	static const char diag[] = "peer_replicated";
	static const char replication_diag[] = "replication";

	gfarm_mutex_lock(&peer->replication_mutex, diag, replication_diag);

	if (handle == -1) {
		for (fr = peer->replicating_inodes.next_inode;
		    fr != &peer->replicating_inodes; fr = fr->next_inode) {
			if (fr->igen == gen &&
			    inode_get_number(fr->inode) == ino)
				break;
		}
	} else {
		for (fr = peer->replicating_inodes.next_inode;
		    fr != &peer->replicating_inodes; fr = fr->next_inode) {
			if (fr->handle == handle &&
			    fr->igen == gen &&
			    inode_get_number(fr->inode) == ino)
				break;
		}
	}
	if (fr == &peer->replicating_inodes)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = GFARM_ERR_NO_ERROR;

	gfarm_mutex_unlock(&peer->replication_mutex, diag, replication_diag);

	if (e == GFARM_ERR_NO_ERROR)
		e = inode_replicated(fr, src_errcode, dst_errcode, size);
	else
		gflog_error(GFARM_MSG_1002410,
		    "orphan replication (%s, %lld:%lld): s=%d d=%d size:%lld "
		    "maybe the connection had a problem?",
		    host_name(host), (long long)ino, (long long)gen,
		    src_errcode, dst_errcode, (long long)size);
	return (e);
}

static void
peer_replicating_free_all(struct peer *peer)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	static const char diag[] = "peer_replicating_free_all";

	gfarm_mutex_lock(&peer->replication_mutex, diag, "loop");

	while ((fr = peer->replicating_inodes.next_inode) !=
	    &peer->replicating_inodes) {
		gfarm_mutex_unlock(&peer->replication_mutex, diag, "settle");
		e = inode_replicated(fr, GFARM_ERR_NO_ERROR,
		     GFARM_ERR_CONNECTION_ABORTED, -1);
		/* assert(e == GFARM_ERR_INVALID_FILE_REPLICA); */
		gfarm_mutex_lock(&peer->replication_mutex, diag, "settle");
	}

	gfarm_mutex_unlock(&peer->replication_mutex, diag, "loop");
}

#ifdef HAVE_EPOLL
static void
peer_epoll_ctl_fd(int op, int fd)
{
	struct epoll_event ev = { 0, { 0 }};

	ev.data.fd = fd;
	ev.events = EPOLLIN; /* level triggered, since we use blocking mode */
	if (epoll_ctl(peer_epoll.fd, op, fd, &ev) == -1) {
		if (op == EPOLL_CTL_DEL) {
			/*
			 * this is expected.  see the comment in peer_watcher()
			 * about calling peer_epoll_del_fd() and
			 * https://sourceforge.net/apps/trac/gfarm/ticket/80
			 * https://sourceforge.net/apps/trac/gfarm/ticket/113
			 */
			gflog_info(GFARM_MSG_1002426,
			    "epoll_ctl(%d, %d, %d): "
			    "probably called against a closed file: %s",
			    peer_epoll.fd, op, fd, strerror(errno));
		} else {
			gflog_fatal(GFARM_MSG_1002427,
			    "epoll_ctl(%d, %d, %d): %s\n",
			    peer_epoll.fd, op, fd, strerror(errno));
		}
	}
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
#ifdef HAVE_EPOLL
	int efd;
#else
	struct pollfd *fd;
#endif
	static const char diag[] = "peer_watcher";

	for (;;) {
#ifdef HAVE_EPOLL
		rv = nfds = epoll_wait(peer_epoll.fd, peer_epoll.events,
				peer_epoll.nevents, PEER_WATCH_INTERVAL);
#else
		nfds = 0;
		gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);
		for (i = 0; i < peer_table_size; i++) {
			peer = &peer_table[i];

			gfarm_mutex_lock(&peer->control_mutex,
			    "peer_watcher start", "peer:control_mutex");
			skip = peer->conn == NULL ||
			    (peer->control & PEER_WATCHING) == 0 ||
			    (peer->control & PEER_CLOSING);
			gfarm_mutex_unlock(&peer->control_mutex,
			    "peer_watcher start", "peer:control_mutex");
			if (skip)
				continue;

			fd = &peer_poll_fds[nfds++];
			fd->fd = i;
			fd->events = POLLIN;
			fd->revents = 0;
		}
		gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);

		rv = poll(peer_poll_fds, nfds, PEER_WATCH_INTERVAL);
#endif
		if (rv == -1 && errno == EINTR)
			continue;
		if (rv == -1)
#ifdef HAVE_EPOLL
			gflog_fatal(GFARM_MSG_1000276,
			    "peer_watcher: epoll_wait: %s\n",
			    strerror(errno));
#else
			gflog_fatal(GFARM_MSG_1000277,
			    "peer_watcher: poll: %s\n",
			    strerror(errno));
#endif

		for (i = 0; i < nfds; i++) {
#ifdef HAVE_EPOLL
			efd = peer_epoll.events[i].data.fd;
			peer = &peer_table[efd];
#else
			if (rv == 0)
				break; /* all processed */
			fd = &peer_poll_fds[i];
			peer = &peer_table[fd->fd];
#endif
			giant_lock();
			gfarm_mutex_lock(&peer_table_mutex,
			    diag, peer_table_diag);
			/*
			 * don't use peer_control_mutex_lock(), because
			 * collision with peer_watch_access() is expected here.
			 */
			gfarm_mutex_lock(&peer->control_mutex,
			    "peer_watcher checking", "peer:control_mutex");

#ifdef HAVE_EPOLL
			if ((peer_epoll.events[i].events & EPOLLIN) == 0)
#else
			if ((fd->revents & POLLIN) == 0)
#endif
			{
				skip = 1;
			} else {
#ifdef HAVE_EPOLL
				/* efd may be closed during epoll */
				peer_epoll_del_fd(efd);
#endif
				rv--;
				if (peer->conn == NULL ||
				    (peer->control & PEER_WATCHING) == 0) {
					skip = 1;
					gflog_debug(GFARM_MSG_1002323,
					    "peer_watcher: fd:%d must be "
					    "closed during (e)poll: %p, 0x%x",
					    peer_get_fd(peer),
					    peer->conn, (int)peer->control);
				} else if (peer->control & PEER_CLOSING) {
					skip = 1;
					peer->control &= ~PEER_WATCHING;
					gflog_debug(GFARM_MSG_1002428,
					    "peer_watcher: fd:%d will be "
					    "closed and input will be ignored",
					    peer_get_fd(peer));
				} else {
					skip = 0;

					peer->control &= ~PEER_WATCHING;
					/*
					 * needs peer_table_mutex here
					 * to protect this from peer_free()
					 */
					peer->control |= PEER_INVOKING;
				}
			}

			gfarm_mutex_unlock(&peer->control_mutex,
			    "peer_watcher checking", "peer:control_mutex");
			gfarm_mutex_unlock(&peer_table_mutex,
			    diag, peer_table_diag);
			giant_unlock();

			if (!skip) {
				/*
				 * We shouldn't have giant_lock or
				 * peer_table_mutex here.
				 */
				thrpool_add_job(peer->handler_thread_pool,
				    peer->protocol_handler, peer);
			}
		}
	}
}

void
peer_add_ref(struct peer *peer)
{
	static const char diag[] = "peer_add_ref";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
	++peer->refcount;
	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

int
peer_del_ref(struct peer *peer)
{
	int referenced;
	static const char diag[] = "peer_del_ref";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	if (--peer->refcount > 0) {
		referenced = 1;
	} else {
		referenced = 0;
		gfarm_cond_signal(&peer_closing_queue.ready_to_close,
		    diag, "ready to close");
	}

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	return (referenced);
}

void *
peer_closer(void *arg)
{
	struct peer *peer, **prev;
	int do_close;
	static const char diag[] = "peer_closer";

	gfarm_mutex_lock(&peer_closing_queue.mutex, diag,
	    "peer_closing_queue");

	for (;;) {
		while (peer_closing_queue.head == NULL)
			gfarm_cond_wait(&peer_closing_queue.ready_to_close,
			    &peer_closing_queue.mutex,
			    diag, "queue is not empty");

		for (prev = &peer_closing_queue.head;
		    (peer = *prev) != NULL; prev = &peer->next_close) {
			if (peer->refcount == 0) {
				gfarm_mutex_lock(&peer->control_mutex,
				    diag, "peer:control_mutex");
				if ((peer->control & PEER_INVOKING) == 0) {
					peer->control |= PEER_CLOSING;
					do_close = 1;
				} else {
					do_close = 0;
				}
				gfarm_mutex_unlock(&peer->control_mutex,
				    diag, "peer:control_mutex");
				if (do_close) {
					*prev = peer->next_close;
					if (peer_closing_queue.tail ==
					    &peer->next_close)
						peer_closing_queue.tail = prev;
					break;
				}
			}
		}
		if (peer == NULL) {
			gfarm_cond_wait(&peer_closing_queue.ready_to_close,
			    &peer_closing_queue.mutex,
			    diag, "waiting for host_sender/receiver_unlock");
			continue;
		}

		gfarm_mutex_unlock(&peer_closing_queue.mutex,
		    diag, "before giant");

		giant_lock();
		/*
		 * NOTE: this shouldn't need db_begin()/db_end()
		 * at least for now,
		 * because only externalized descriptor needs the calls.
		 */
		peer_free(peer);
		giant_unlock();

		gfarm_mutex_lock(&peer_closing_queue.mutex,
		    diag, "after giant");
	}

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

void
peer_free_request(struct peer *peer)
{
	int fd = peer_get_fd(peer), rv;
	static const char diag[] = "peer_free_request";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	/*
	 * wake up threads which may be sleeping at read() or write(), because
	 * they may be holding host_sender_lock() or host_receiver_lock(), but
	 * without closing the descriptor, because that leads race condition.
	 */
	rv = shutdown(fd, SHUT_RDWR);
	if (rv == -1)
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s(%s) : shutdown(%d): %s", BACK_CHANNEL_DIAG(peer),
		    peer_get_hostname(peer), fd, strerror(errno));

	*peer_closing_queue.tail = peer;
	peer->next_close = NULL;
	peer_closing_queue.tail = &peer->next_close;

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
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
		gfarm_mutex_init(&peer->control_mutex,
		    "peer_init", "peer:control_mutex");

		peer->fd_current = -1;
		peer->fd_saved = -1;
		peer->flags = 0;
		peer->findxmlattrctx = NULL;
		peer->pending_new_generation = NULL;
		peer->u.client.jobs = NULL;

		gfarm_mutex_init(&peer->replication_mutex,
		    "peer_init", "replication");
		peer->simultaneous_replication_receivers = 0;
		/* make circular list `replicating_inodes' empty */
		peer->replicating_inodes.prev_inode =
		peer->replicating_inodes.next_inode =
		    &peer->replicating_inodes;
	}

#ifdef HAVE_EPOLL
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

static gfarm_error_t
peer_alloc0(int fd, struct peer **peerp, struct gfp_xdr *conn)
{
	gfarm_error_t e;
	struct peer *peer;
	int sockopt;
	static const char diag[] = "peer_alloc";

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
	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);
	peer = &peer_table[fd];
	if (peer->conn != NULL) { /* must be an implementation error */
		gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
		gflog_debug(GFARM_MSG_1001582,
			"bad file descriptor: conn is NULL");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	peer->next_close = NULL;
	peer->refcount = 0;

	/* XXX FIXME gfp_xdr requires too much memory */
	if (conn == NULL) {
		e = gfp_xdr_new_socket(fd, &peer->conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001583,
			    "gfp_xdr_new_socket() failed: %s",
			    gfarm_error_string(e));
			gfarm_mutex_unlock(&peer_table_mutex, diag,
			    peer_table_diag);
			return (e);
		}
	} else
		peer->conn = conn;

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
	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
peer_alloc(int fd, struct peer **peerp)
{
	return (peer_alloc0(fd, peerp, NULL));
}

gfarm_error_t
peer_alloc_with_connection(struct peer **peerp, struct gfp_xdr *conn,
	struct abstract_host *host, int id_type)
{
	gfarm_error_t e;

	if ((e = peer_alloc0(gfp_xdr_fd(conn), peerp, conn))
	    == GFARM_ERR_NO_ERROR) {
		(*peerp)->host = host;
		(*peerp)->id_type = GFARM_AUTH_ID_TYPE_METADATA_HOST;
	}
	return (e);
}

const char *
peer_get_service_name(struct peer *peer)
{
	return (peer == NULL ? "" :
	    ((peer)->id_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST ?  "gfsd" :
	    ((peer)->id_type == GFARM_AUTH_ID_TYPE_METADATA_HOST ?
	    "gfmd" : "")));
}

/* caller should allocate the storage for username and hostname */
void
peer_authorized(struct peer *peer,
	enum gfarm_auth_id_type id_type, char *username, char *hostname,
	struct sockaddr *addr, enum gfarm_auth_method auth_method)
{
	struct host *h;
	struct mdhost *m;

	peer->id_type = id_type;
	peer->user = NULL;
	peer->username = username;

	switch (id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		peer->user = user_lookup(username);
		if (user_is_active(peer->user)) {
			free(username);
			peer->username = NULL;
		} else
			peer->username = username;
		/*FALLTHROUGH*/

	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		h = host_addr_lookup(hostname, addr);
		if (h == NULL) {
			peer->host = NULL;
		} else {
			peer->host = host_to_abstract_host(h);
		}
		break;

	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		m = mdhost_lookup(hostname);
		if (m == NULL) {
			peer->host = NULL;
		} else {
			peer->host = mdhost_to_abstract_host(m);
		}
		break;
	}

	if (peer->host != NULL) {
		free(hostname);
		peer->hostname = NULL;
	} else {
		peer->hostname = hostname;
	}

	switch (id_type) {
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		if (peer->host == NULL)
			gflog_warning(GFARM_MSG_1000284,
			    "unknown host: %s", hostname);
		else
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s connected from %s",
			    peer_get_service_name(peer),
			    abstract_host_get_name(peer->host));
		break;
	default:
		break;
	}
	/* We don't record auth_method for now */

	gfarm_mutex_lock(&peer->control_mutex,
	    "peer_authorized", "peer:control_mutex");
	peer->control = 0;
	gfarm_mutex_unlock(&peer->control_mutex,
	    "peer_authorized", "peer:control_mutex");

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
	char *username;
	const char *hostname;
	static const char diag[] = "peer_free";

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);

	if (peer->async != NULL && peer_async_free != NULL) {
		(*peer_async_free)(peer, peer->async);
		peer->async = NULL;
	}

	/* this must be called after (*peer_async_free)() */
	peer_replicating_free_all(peer);

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

	gfp_xdr_free(peer->conn); peer->conn = NULL;
	peer->next_close = NULL;
	peer->refcount = 0;

	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_shutdown_all(void)
{
	int i;
	struct peer *peer;
	static const char diag[] = "peer_shutdown_all";

	/* We never unlock this mutex any more */
	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);

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
#ifdef HAVE_EPOLL
	close(peer_epoll.fd);
#endif
}

void
peer_invoked(struct peer *peer)
{
	gfarm_mutex_lock(&peer->control_mutex,
	    "peer_watch_access", "peer:control_mutex");
	peer->control &= ~PEER_INVOKING;
	gfarm_mutex_unlock(&peer->control_mutex,
	    "peer_watch_access", "peer:control_mutex");

	gfarm_cond_signal(&peer_closing_queue.ready_to_close,
	    "peer_invoked", "connection can be freed");
}

void
peer_watch_access(struct peer *peer)
{
	gfarm_mutex_lock(&peer->control_mutex,
	    "peer_watch_access", "peer:control_mutex");
	peer->control |= PEER_WATCHING;
	gfarm_mutex_unlock(&peer->control_mutex,
	    "peer_watch_access", "peer:control_mutex");
#ifdef HAVE_EPOLL
	peer_epoll_add_fd(peer_get_fd(peer));
#endif
}

#if 0

struct peer *
peer_by_fd(int fd)
{
	static const char diag[] = "peer_by_fd";

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);
	if (fd < 0 || fd >= peer_table_size || peer_table[fd].conn == NULL)
		return (NULL);
	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
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
	struct host *h;
	struct mdhost *m;

	switch (peer->id_type) {
	case GFARM_AUTH_ID_TYPE_SPOOL_HOST:
		if (peer->host != NULL) { /* already set */
			gflog_debug(GFARM_MSG_1001585,
				"peer host is already set");
			return (GFARM_ERR_NO_ERROR);
		}
		if ((h = host_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"host %s does not exist", hostname);
			return (GFARM_ERR_UNKNOWN_HOST);
		}
		peer->host = host_to_abstract_host(h);
		break;
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		if (peer->host != NULL) { /* already set */
			gflog_debug(GFARM_MSG_UNFIXED,
				"peer metadata-host is already set");
			return (GFARM_ERR_NO_ERROR);
		}
		if ((m = mdhost_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"metadata-host %s does not exist", hostname);
			return (GFARM_ERR_UNKNOWN_HOST);
		}
		peer->host = mdhost_to_abstract_host(m);
		break;
	default:
		gflog_debug(GFARM_MSG_1001584,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	if (peer->hostname != NULL) {
		free(peer->hostname);
		peer->hostname = NULL;
	}

	gflog_debug(GFARM_MSG_UNFIXED,
	    "%s connected from %s",
	    peer_get_service_name(peer), abstract_host_get_name(peer->host));
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

const char *
peer_get_hostname(struct peer *peer)
{
	return (peer->host != NULL ?
	    abstract_host_get_name(peer->host) : NULL);
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

struct abstract_host *
peer_get_abstract_host(struct peer *peer)
{
	return (peer->host);
}

struct host *
peer_get_host(struct peer *peer)
{
	return (abstract_host_to_host(peer->host));
}

struct mdhost *
peer_get_mdhost(struct peer *peer)
{
	return (abstract_host_to_mdhost(peer->host));
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

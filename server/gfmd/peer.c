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
#include <netinet/in.h>
#include <netdb.h> /* for NI_MAXHOST, NI_NUMERICHOST, etc */

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_iostat.h>

#include "gfutil.h"
#include "thrsubr.h"
#include "queue.h"
#include "gfnetdb.h"

#include "gfp_xdr.h"
#include "io_fd.h"
#include "auth.h"
#include "config.h" /* gfarm_simultaneous_replication_receivers */

#include "subr.h"
#include "thrpool.h"
#include "watcher.h"
#include "db_access.h"
#include "user.h"
#include "abstract_host.h"
#include "host.h"
#include "mdhost.h"
#include "gfmd_channel.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "job.h"
#include "iostat.h"

#include "protocol_state.h"


/*
 * peer_watcher
 */

struct peer_watcher {
	struct watcher *w;
	struct thread_pool *thrpool;
	void *(*readable_handler)(void *);
};

static int peer_watcher_nfd_hint_default;

void
peer_watcher_set_default_nfd(int nfd_hint_default)
{
	peer_watcher_nfd_hint_default = nfd_hint_default;
}

/* this function never fails, but aborts. */
struct peer_watcher *
peer_watcher_alloc(int thrpool_size, int thrqueue_length, 
	void *(*readable_handler)(void *),
	const char *diag)
{
	gfarm_error_t e;
	struct peer_watcher *pw;

	GFARM_MALLOC(pw);
	if (pw == NULL)
		gflog_fatal(GFARM_MSG_1002763, "peer_watcher %s: no memory",
		    diag);

	e = watcher_alloc(peer_watcher_nfd_hint_default, &pw->w);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1002764, "watcher(%d) %s: no memory",
		    peer_watcher_nfd_hint_default, diag);

	pw->thrpool = thrpool_new(thrpool_size, thrqueue_length, diag);
	if (pw->thrpool == NULL)
		gflog_fatal(GFARM_MSG_1002765, "thrpool(%d, %d) %s: no memory",
		    thrpool_size, thrqueue_length, diag);

	pw->readable_handler = readable_handler;

	return (pw);
}

struct thread_pool *
peer_watcher_get_thrpool(struct peer_watcher *pw)
{
	return (pw->thrpool);
}

/*
 * peer
 */

#define BACK_CHANNEL_DIAG(peer) (peer_get_auth_id_type(peer) == \
	GFARM_AUTH_ID_TYPE_SPOOL_HOST ? "back_channel" : "gfmd_channel")
#define PROTOCOL_ERROR_MUTEX_DIAG "protocol_error_mutex"

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

struct cookie {
	gfarm_uint64_t id;
	GFARM_HCIRCLEQ_ENTRY(cookie) hcircleq;
};

struct peer {
	/*
	 * protected by peer_closing_queue.mutex
	 */
	struct peer *next_close;
	int refcount;
	int replication_refcount;

	/*
	 * connection structure
	 */
	struct gfp_xdr *conn;
	gfp_xdr_async_peer_t async; /* used by {back|gfmd}_channel */
	struct peer_watcher *watcher;
	struct watcher_event *readable_event;

	/*
	 * followings (except protocol_error) are protected by giant lock
	 */

	enum gfarm_auth_id_type id_type;
	char *username, *hostname;
	struct user *user;
	struct abstract_host *host;

	/*
	 * only used by foreground channel
	 */

	struct process *process;
	int protocol_error;
	pthread_mutex_t protocol_error_mutex;

	struct protocol_state pstate;

	gfarm_int32_t fd_current, fd_saved;
	int flags;
#define PEER_FLAGS_FD_CURRENT_EXTERNALIZED	1
#define PEER_FLAGS_FD_SAVED_EXTERNALIZED	2

	void *findxmlattrctx;

	/* only one pending GFM_PROTO_GENERATION_UPDATED per peer is allowed */
	struct inode *pending_new_generation;
	/* GFM_PROTO_GENERATION_UPDATED_BY_COOKIE */
	GFARM_HCIRCLEQ_HEAD(cookie) cookies;

	union {
		struct {
			/* only used by "gfrun" client */
			struct job_table_entry *jobs;
		} client;
	} u;

	struct gfarm_iostat_items *iostatp;

	/*
	 * only used by gfsd back channel
	 */
	pthread_mutex_t replication_mutex;
	int simultaneous_replication_receivers;
	struct file_replicating replicating_inodes; /* dummy header */

	/*
	 * only used by gfmd channel
	 */
	struct gfmdc_peer_record *gfmdc_record;
};

static struct peer *peer_table;
static int peer_table_size, peer_initialized;
static pthread_mutex_t peer_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char peer_table_diag[] = "peer_table";
static gfarm_uint64_t cookie_seqno = 1;

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

static const char replication_diag[] = "replication";

/* only host_replicating_new() is allowed to call this routine */
gfarm_error_t
peer_replicating_new(struct peer *peer, struct host *dst,
	struct file_replicating **frp)
{
	struct file_replicating *fr;
	static const char diag[] = "peer_replicating_new";

	GFARM_MALLOC(fr);
	if (fr == NULL) {
		/* decrement replication_refcount */
		host_put_peer_for_replication(dst, peer);
		return (GFARM_ERR_NO_MEMORY);
	}

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

	if (fr == NULL) {
		/* decrement replication_refcount */
		host_put_peer_for_replication(dst, peer);
		return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
	}
	*frp = fr;
	return (GFARM_ERR_NO_ERROR);
}

/* only file_replicating_free() is allowed to call this routine */
void
peer_replicating_free(struct file_replicating *fr)
{
	struct peer *peer = fr->peer;
	static const char diag[] = "peer_replicating_free";

	gfarm_mutex_lock(&peer->replication_mutex, diag, replication_diag);
	--peer->simultaneous_replication_receivers;
	fr->prev_inode->next_inode = fr->next_inode;
	fr->next_inode->prev_inode = fr->prev_inode;
	gfarm_mutex_unlock(&peer->replication_mutex, diag, replication_diag);

	/* decrement replication_refcount */
	host_put_peer_for_replication(fr->dst, fr->peer);

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

/*
 * this frees file_replicating structures with the following condition:
 * GFS_PROTO_REPLICATION_REQUEST is successfully done,
 * but gfmd hasn't received GFM_PROTO_REPLICATION_RESULT.
 */
static void
peer_replicating_free_all_waiting_result(struct peer *peer)
{
	gfarm_error_t e;
	struct file_replicating *fr;
	int found = 0;
	struct host *dst = NULL;
	gfarm_ino_t ino = 0;
	gfarm_int64_t gen = 0;
	static const char diag[] = "peer_replicating_free_all_waiting_result";

	for (;;) {
		found = 0;

		gfarm_mutex_lock(&peer->replication_mutex,
		    diag, replication_diag);
		for (fr = peer->replicating_inodes.next_inode;
		    fr != &peer->replicating_inodes; fr = fr->next_inode) {
			if (fr->handle != -1) {
				found = 1;
				dst = fr->dst;
				ino = inode_get_number(fr->inode);
				gen = fr->igen;
				break;
			}
		}
		gfarm_mutex_unlock(&peer->replication_mutex,
		    diag, replication_diag);

		if (!found)
			break;

		e = inode_replicated(fr, 0, GFARM_ERR_CONNECTION_ABORTED, -1);
		gflog_debug(GFARM_MSG_1003612,
		    "%s: (%s, %lld:%lld): connection aborted: %s",
		    diag, host_name(dst), (long long)ino, (long long)gen,
		    gfarm_error_string(e));
	}
}

static void
peer_replicating_free_all(struct peer *peer)
{
	struct file_replicating *fr;
	static const char diag[] = "peer_replicating_free_all";

	gfarm_mutex_lock(&peer->replication_mutex, diag, "loop");

	while ((fr = peer->replicating_inodes.next_inode) !=
	    &peer->replicating_inodes) {
		gfarm_mutex_unlock(&peer->replication_mutex, diag, "settle");
		(void)inode_replicated(fr, GFARM_ERR_NO_ERROR,
		     GFARM_ERR_CONNECTION_ABORTED, -1);
		/* abandon error */
		/* assert(e == GFARM_ERR_INVALID_FILE_REPLICA); */
		gfarm_mutex_lock(&peer->replication_mutex, diag, "settle");
	}

	gfarm_mutex_unlock(&peer->replication_mutex, diag, "loop");
}

void
#ifdef PEER_REFCOUNT_DEBUG
peer_add_ref_impl(struct peer *peer, const char *file, int line, const char *func)
#else
peer_add_ref(struct peer *peer)
#endif
{
	static const char diag[] = "peer_add_ref";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
	++peer->refcount;
#ifdef PEER_REFCOUNT_DEBUG
	gflog_info(GFARM_MSG_1003613, "%s(%d):%s(): peer_add_ref(%p):%d",
	    file, line, func, peer, peer->refcount);
#endif
	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

int
#ifdef PEER_REFCOUNT_DEBUG
peer_del_ref_impl(struct peer *peer, const char *file, int line, const char *func)
#else
peer_del_ref(struct peer *peer)
#endif
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
#ifdef PEER_REFCOUNT_DEBUG
	gflog_info(GFARM_MSG_1003614, "%s(%d):%s(): peer_del_ref(%p):%d",
	    file, line, func, peer, peer->refcount);
#endif

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	return (referenced);
}

void
peer_add_ref_for_replication(struct peer *peer)
{
	static const char diag[] = "peer_add_ref_for_replication";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
	++peer->replication_refcount;
	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

int
peer_del_ref_for_replication(struct peer *peer)
{
	int referenced;
	static const char diag[] = "peer_del_ref_for_replication";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	if (--peer->replication_refcount > 0) {
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
	int do_async_free;
	static const char diag[] = "peer_closer";

	gfarm_mutex_lock(&peer_closing_queue.mutex, diag,
	    "peer_closing_queue");

	for (;;) {
		while (peer_closing_queue.head == NULL)
			gfarm_cond_wait(&peer_closing_queue.ready_to_close,
			    &peer_closing_queue.mutex,
			    diag, "queue is not empty");

		do_async_free = 0;
		for (prev = &peer_closing_queue.head;
		    (peer = *prev) != NULL; prev = &peer->next_close) {
			if (peer->refcount == 0 &&
			    !watcher_event_is_active(peer->readable_event)) {
				if (peer->async != NULL &&
				    peer_async_free != NULL) {
					do_async_free = 1;
					break;
				} else if (peer->replication_refcount == 0) {
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
		if (do_async_free) {
			/* async rpc cleanup should be done before freeing peer */
			(*peer_async_free)(peer, peer->async);
			peer->async = NULL;

			/* there is no chance to receive result any more */
			peer_replicating_free_all_waiting_result(peer);

			/*
			 * this peer cannot be freed yet,
			 * wait until peer->replication_refcount becomes 0.
			 * see the problem 2 of
			 * https://sourceforge.net/apps/trac/gfarm/ticket/408
			 */
		} else {
			peer_free(peer);
		}
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
		gflog_info(GFARM_MSG_1002766,
		    "%s(%s) : shutdown(%d): %s", BACK_CHANNEL_DIAG(peer),
		    peer_get_hostname(peer), fd, strerror(errno));

	*peer_closing_queue.tail = peer;
	peer->next_close = NULL;
	peer_closing_queue.tail = &peer->next_close;

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

void
peer_init(int max_peers)
{
	int i;
	struct peer *peer;
	gfarm_error_t e;
	static const char diag[] = "peer_init";

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);

	GFARM_MALLOC_ARRAY(peer_table, max_peers);
	if (peer_table == NULL)
		gflog_fatal(GFARM_MSG_1000278,
		    "peer table: %s", strerror(ENOMEM));
	peer_table_size = max_peers;

	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		peer->next_close = NULL;
		peer->refcount = 0;
		peer->replication_refcount = 0;

		peer->conn = NULL;
		peer->async = NULL;
		peer->readable_event = NULL;

		peer->username = NULL;
		peer->hostname = NULL;
		peer->user = NULL;
		peer->host = NULL;

		/*
		 * foreground channel
		 */

		peer->process = NULL;
		peer->protocol_error = 0;
		gfarm_mutex_init(&peer->protocol_error_mutex,
		    "peer_init", "peer:protocol_error_mutex");

		peer->fd_current = -1;
		peer->fd_saved = -1;
		peer->flags = 0;
		peer->findxmlattrctx = NULL;
		peer->pending_new_generation = NULL;
		peer->u.client.jobs = NULL;

		peer->iostatp = NULL;

		/* gfsd back channel */
		gfarm_mutex_init(&peer->replication_mutex,
		    "peer_init", "replication");
		peer->simultaneous_replication_receivers = 0;
		/* make circular list `replicating_inodes' empty */
		peer->replicating_inodes.prev_inode =
		peer->replicating_inodes.next_inode =
		    &peer->replicating_inodes;
		GFARM_HCIRCLEQ_INIT(peer->cookies, hcircleq);

		/* gfmd channel */
		peer->gfmdc_record = NULL;
	}

	e = create_detached_thread(peer_closer, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000282,
		    "create_detached_thread(peer_closer): %s",
			    gfarm_error_string(e));

	peer_initialized = 1;

	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
}

static gfarm_error_t
peer_alloc0(int fd, struct peer **peerp, struct gfp_xdr *conn)
{
	gfarm_error_t e;
	struct peer *peer;
	int sockopt, conn_alloced = 0;
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
			"bad file descriptor: conn is not NULL");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	peer->next_close = NULL;
	peer->refcount = 0;
	peer->replication_refcount = 0;

	/* XXX FIXME gfp_xdr requires too much memory */
	if (conn == NULL) {
		e = gfp_xdr_new_socket(fd, &conn);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001583,
			    "gfp_xdr_new_socket() failed: %s",
			    gfarm_error_string(e));
			gfarm_mutex_unlock(&peer_table_mutex, diag,
			    peer_table_diag);
			return (e);
		}
		conn_alloced = 1;
	}
	peer->conn = conn;
	peer->async = NULL; /* synchronous protocol by default */
	peer->watcher = NULL;
	if (peer->readable_event == NULL) {
		e = watcher_fd_readable_event_alloc(fd,
		    &peer->readable_event);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002767,
			    "peer watching %d: %s", fd, gfarm_error_string(e));
			if (conn_alloced)
				gfp_xdr_free(peer->conn);
			peer->conn = NULL;
			gfarm_mutex_unlock(&peer_table_mutex, diag,
			    peer_table_diag);
			return (e);
		}
	}

	peer->username = NULL;
	peer->hostname = NULL;
	peer->user = NULL;
	peer->host = NULL;

	/*
	 * foreground channel
	 */

	peer->process = NULL;
	peer->protocol_error = 0;

	peer->fd_current = -1;
	peer->fd_saved = -1;
	peer->flags = 0;
	peer->findxmlattrctx = NULL;
	peer->u.client.jobs = NULL;
	GFARM_HCIRCLEQ_INIT(peer->cookies, hcircleq);

	if (peer->iostatp == NULL)
		peer->iostatp = gfarm_iostat_get_ip(fd);

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
	struct sockaddr *addr, enum gfarm_auth_method auth_method,
	struct peer_watcher *watcher)
{
	struct host *h;
	struct mdhost *m;

	peer->id_type = id_type;
	peer->user = NULL;
	peer->username = username;

	switch (id_type) {
	case GFARM_AUTH_ID_TYPE_USER:
		peer->user = user_lookup(username);
		if (peer->user != NULL) {
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

	default:
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
			gflog_notice(GFARM_MSG_1000284,
			    "unknown host: %s", hostname);
		else
			gflog_debug(GFARM_MSG_1002768,
			    "%s connected from %s",
			    peer_get_service_name(peer),
			    abstract_host_get_name(peer->host));
		break;
	default:
		break;
	}
	/* We don't record auth_method for now */

	peer->watcher = watcher;

	if (gfp_xdr_recv_is_ready(peer_get_conn(peer)))
		thrpool_add_job(watcher->thrpool,
		    watcher->readable_handler, peer);
	else
		peer_watch_access(peer);
}

static int
peer_get_numeric_name(struct peer *peer, char *hostbuf, size_t hostlen)
{
	struct sockaddr_in sin;
	socklen_t slen = sizeof(sin);

	if (getpeername(peer_get_fd(peer),
	    (struct sockaddr *)&sin, &slen) != 0)
		return (errno);

	return (gfarm_getnameinfo((struct sockaddr *)&sin, slen,
	    hostbuf, hostlen, NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV));
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_free(struct peer *peer)
{
	int err;
	char *username;
	const char *hostname;
	static const char diag[] = "peer_free";
	struct cookie *cookie;
	char hostbuf[NI_MAXHOST];

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);

	username = peer_get_username(peer);
	hostname = peer_get_hostname(peer);

	/*
	 * both username and hostname may be null,
	 * if peer_authorized() hasn't been called. (== authentication failed)
	 */
	if (hostname == NULL) {
		/*
		 * IP address must be logged instead of (maybe faked) hostname
		 * in case of an authentication failure.
		 */
		err = peer_get_numeric_name(peer, hostbuf, sizeof(hostbuf));
		if (err == 0) {
			hostname = hostbuf;
		} else {
			hostname = "<not-socket>";
			gflog_info(GFARM_MSG_1003276,
			    "unable to convert peer address to string: %s",
			    strerror(err));
		}
	}
	gflog_notice(GFARM_MSG_1000286,
	    "(%s@%s) disconnected",
	    username != NULL ? username : "<unauthorized>",
	    hostname != NULL ? hostname : hostbuf);

	/*
	 * free resources for gfmd channel
	 */
	if (peer->gfmdc_record != NULL) {
		gfmdc_peer_record_free(peer->gfmdc_record, diag);
		peer->gfmdc_record = NULL;
	}

	/*
	 * free resources for gfsd back channel
	 */
	/* this must be called after (*peer_async_free)() */
	peer_replicating_free_all(peer);

	/*
	 * free resources for foreground channel
	 */

	if (peer->iostatp != NULL) {
		gfarm_iostat_clear_ip(peer->iostatp);
		peer->iostatp = NULL;
	}

	/*XXX XXX*/
	while (peer->u.client.jobs != NULL)
		job_table_remove(job_get_id(peer->u.client.jobs), username,
		    &peer->u.client.jobs);
	peer->u.client.jobs = NULL;

	while (!GFARM_HCIRCLEQ_EMPTY(peer->cookies, hcircleq)) {
		cookie = GFARM_HCIRCLEQ_FIRST(peer->cookies, hcircleq);
		GFARM_HCIRCLEQ_REMOVE(cookie, hcircleq);
		free(cookie);
	}
	GFARM_HCIRCLEQ_INIT(peer->cookies, hcircleq);

	peer_unset_pending_new_generation(peer);

	peer->findxmlattrctx = NULL;

	peer->protocol_error = 0;
	if (peer->process != NULL) {
		process_detach_peer(peer->process, peer, diag);
		peer->process = NULL;
	}

	/*
	 * free common resources
	 */

	peer->user = NULL;
	peer->host = NULL;
	if (peer->username != NULL) {
		free(peer->username); peer->username = NULL;
	}
	if (peer->hostname != NULL) {
		free(peer->hostname); peer->hostname = NULL;
	}

	peer->watcher = NULL;
	/* We don't free peer->readable_event. */
	if (peer->conn) {
		gfp_xdr_free(peer->conn);
		peer->conn = NULL;
	}

	peer->next_close = NULL;
	peer->refcount = 0;
	peer->replication_refcount = 0;

	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_shutdown_all(void)
{
	int i;
	struct peer *peer;
	int needs_cleanup = 0, transaction = 0;
	static const char diag[] = "peer_shutdown_all";

	/* We never unlock this mutex any more */
	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);

	if (!peer_initialized) {
		gflog_info(GFARM_MSG_1003473,
		    "peer module is not initialized yet, "
		    "skip to shutdown connections");
		return;
	}

	/*
	 * check whether we need to close some peer or not at first,
	 * to avoid unnecessary db_begin()/db_end().
	 */
	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		if (peer->process != NULL) {
			needs_cleanup = 1;
			break;
		}
	}
	if (!needs_cleanup)
		return;

	/*
	 * We do db_begin()/db_end() here instead of the caller of
	 * this function, to avoid SF.net #736
	 */
	if (db_begin(diag) == GFARM_ERR_NO_ERROR)
		transaction = 1;
	for (i = 0; i < peer_table_size; i++) {
		peer = &peer_table[i];
		if (peer->process == NULL)
			continue;

		gflog_notice(GFARM_MSG_1000287, "(%s@%s) shutting down",
		    peer->username, peer->hostname);
#if 0		/* we don't really have to do this at shutdown */
		peer_unset_pending_new_generation(peer);
#endif
		process_detach_peer(peer->process, peer, diag);
		peer->process = NULL;
	}
	if (transaction)
		db_end(diag);
}

/* XXX FIXME - rename this to peer_readable_invoked() */
void
peer_invoked(struct peer *peer)
{
	watcher_event_ack(peer->readable_event);
	peer_del_ref(peer);

	gfarm_cond_signal(&peer_closing_queue.ready_to_close,
	    "peer_invoked", "connection can be freed");
}

/* XXX FIXME - rename this to peer_readable_watch() */
void
peer_watch_access(struct peer *peer)
{
	peer_add_ref(peer);
	watcher_add_event(peer->watcher->w, peer->readable_event,
	    peer->watcher->thrpool, peer->watcher->readable_handler, peer);
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
			gflog_debug(GFARM_MSG_1002769,
				"host %s does not exist", hostname);
			return (GFARM_ERR_UNKNOWN_HOST);
		}
		peer->host = host_to_abstract_host(h);
		break;
	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		if (peer->host != NULL) { /* already set */
			gflog_debug(GFARM_MSG_1002770,
				"peer metadata-host is already set");
			return (GFARM_ERR_NO_ERROR);
		}
		if ((m = mdhost_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_1002771,
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

	gflog_debug(GFARM_MSG_1002772,
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
	    abstract_host_get_name(peer->host) : peer->hostname);
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
	return (peer->host == NULL ? NULL :
	    abstract_host_to_host(peer->host));
}

struct mdhost *
peer_get_mdhost(struct peer *peer)
{
	return (peer->host == NULL ? NULL :
	    abstract_host_to_mdhost(peer->host));
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
peer_unset_process(struct peer *peer, const char *diag)
{
	if (peer->process == NULL)
		gflog_fatal(GFARM_MSG_1000292,
		    "peer_unset_process: already unset");

	peer_unset_pending_new_generation(peer);

	peer_fdpair_clear(peer, diag);

	process_detach_peer(peer->process, peer, diag);
	peer->process = NULL;
}

void
peer_record_protocol_error(struct peer *peer)
{
	static const char *diag = "peer_record_protocol_error";

	gfarm_mutex_lock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
	peer->protocol_error = 1;
	gfarm_mutex_unlock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
}

int
peer_had_protocol_error(struct peer *peer)
{
	int e;
	static const char *diag = "peer_had_protocol_error";

	gfarm_mutex_lock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
	e = peer->protocol_error;
	gfarm_mutex_unlock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
	return (e);
}

void
peer_set_watcher(struct peer *peer, struct peer_watcher *watcher)
{
	peer->watcher = watcher;
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
peer_fdpair_clear(struct peer *peer, const char *diag)
{
	if (peer->process == NULL) {
		assert(peer->fd_current == -1 && peer->fd_saved == -1);
		return;
	}
	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current, NULL,
		    diag);
	}
	if (peer->fd_saved != -1 &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0) {
		process_close_file(peer->process, peer, peer->fd_saved, NULL,
		    diag);
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
peer_fdpair_set_current(struct peer *peer, gfarm_int32_t fd, const char *diag)
{
	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current, NULL,
		diag);
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
peer_fdpair_save(struct peer *peer, const char *diag)
{
	if (peer->fd_current == -1) {
		gflog_debug(GFARM_MSG_1001591,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	if (peer->fd_saved != -1 &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0 &&
	    peer->fd_saved != peer->fd_current) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_saved, NULL,
		    diag);
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
peer_fdpair_restore(struct peer *peer, const char *diag)
{
	if (peer->fd_saved == -1) {
		gflog_debug(GFARM_MSG_1001592,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	if (peer->fd_current != -1 &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved) { /* prevent double close */
		process_close_file(peer->process, peer, peer->fd_current, NULL,
		    diag);
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

gfarm_uint64_t
peer_add_cookie(struct peer *peer)
{
	static const char *diag = "peer_add_cookie";
	struct cookie *cookie;
	gfarm_uint64_t result;

	GFARM_MALLOC(cookie);
	if (cookie == NULL)
		gflog_fatal(GFARM_MSG_1003277, "%s: no memory", diag);

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);
	result = cookie->id = cookie_seqno++;
	GFARM_HCIRCLEQ_INSERT_HEAD(peer->cookies, cookie, hcircleq);
	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);

	return (result);
}

int
peer_delete_cookie(struct peer *peer, gfarm_uint64_t cookie_id)
{
	static const char *diag = "peer_delete_cookie";
	struct cookie *cookie;
	int found = 0;

	gfarm_mutex_lock(&peer_table_mutex, diag, peer_table_diag);
	GFARM_HCIRCLEQ_FOREACH(cookie, peer->cookies, hcircleq) {
		if (cookie->id == cookie_id) {
			GFARM_HCIRCLEQ_REMOVE(cookie, hcircleq);
			free(cookie);
			found = 1;
			break;
		}
	}
	gfarm_mutex_unlock(&peer_table_mutex, diag, peer_table_diag);
	if (!found)
		gflog_warning(GFARM_MSG_1003278, "%s: bad cookie id %llu",
		    diag, (unsigned long long)cookie_id);

	return (found);
}

gfarm_error_t
peer_get_port(struct peer *peer, int *portp)
{
	struct sockaddr_in sin;
	socklen_t slen = sizeof(sin);

	if (getpeername(peer_get_fd(peer), (struct sockaddr *)&sin, &slen) != 0) {
		*portp = 0;
		return (gfarm_errno_to_error(errno));
	} else if (sin.sin_family != AF_INET) {
		*portp = 0;
		return (GFARM_ERR_ADDRESS_FAMILY_NOT_SUPPORTED_BY_PROTOCOL_FAMILY);
	} else {
		*portp = (int)ntohs(sin.sin_port);
		return (GFARM_ERR_NO_ERROR);
	}
}

void
peer_stat_add(struct peer *peer, unsigned int cat, int val)
{
	if (peer->iostatp != NULL)
		gfarm_iostat_stat_add(peer->iostatp, cat, val);
}

/*
 * only used by gfmd channel
 */
struct gfmdc_peer_record *
peer_get_gfmdc_record(struct peer *peer)
{
	return (peer->gfmdc_record);
}

void
peer_set_gfmdc_record(struct peer *peer, struct gfmdc_peer_record *peer_gfmdc)
{
	peer->gfmdc_record = peer_gfmdc;
}

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
#include <sys/types.h>

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include <gfarm/gfarm_iostat.h>

#include "gfutil.h"
#include "thrsubr.h"
#include "queue.h"

#include "gfp_xdr.h"
#include "auth.h"

#include "subr.h"
#include "watcher.h"
#include "user.h"
#include "abstract_host.h"
#include "host.h"
#include "mdhost.h"
#include "gfm_proto.h" /* for GFARM_DESCRIPTOR_INVALID */
#include "gfmd_channel.h"
#include "peer_watcher.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "job.h"
#include "iostat.h"

#include "protocol_state.h"
#include "peer_impl.h"

static const char PROTOCOL_ERROR_MUTEX_DIAG[] = "protocol_error_mutex";

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

static const char peer_seqno_diag[] = "peer_seqno_mutex";
static pthread_mutex_t peer_seqno_mutex = PTHREAD_MUTEX_INITIALIZER;
static gfarm_uint64_t cookie_seqno = 1;
static gfarm_int64_t private_peer_id_seqno = 1;

void
peer_closer_wakeup(struct peer *peer)
{
	if (peer->free_requested)
		gfarm_cond_signal(&peer_closing_queue.ready_to_close,
		    "peer_closer_wakeup", "connection can be freed");
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
		peer_closer_wakeup(peer);
	}
#ifdef PEER_REFCOUNT_DEBUG
	gflog_info(GFARM_MSG_1003614, "%s(%d):%s(): peer_del_ref(%p):%d",
	    file, line, func, peer, peer->refcount);
#endif

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	return (referenced);
}

void *
peer_closer(void *arg)
{
	struct peer *peer, **prev;
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
				if (!(*peer->ops->is_busy)(peer)) {
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
		 * XXX FIXME: is this still true?
		 */
		peer_free(peer);
		giant_unlock();

		gfarm_mutex_lock(&peer_closing_queue.mutex,
		    diag, "after giant");
	}

	/*NOTREACHED*/
#ifdef __GNUC__ /* shut up stupid warning by gcc */
	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
	return (NULL);
#endif
}

void
peer_free_request(struct peer *peer)
{
	static const char diag[] = "peer_free_request";

	gfarm_mutex_lock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");

	peer->free_requested = 1;

	/*
	 * wake up threads which may be sleeping at read() or write(), because
	 * they may be holding host_sender_lock() or host_receiver_lock(), but
	 * without closing the descriptor, because that leads race condition.
	 */
	(*peer->ops->shutdown)(peer);

	*peer_closing_queue.tail = peer;
	peer->next_close = NULL;
	peer_closing_queue.tail = &peer->next_close;

	gfarm_mutex_unlock(&peer_closing_queue.mutex,
	    diag, "peer_closing_queue");
}

void
peer_init(void)
{
	gfarm_error_t e;

	e = create_detached_thread(peer_closer, NULL);
	if (e != GFARM_ERR_NO_ERROR)
		gflog_fatal(GFARM_MSG_1000282,
		    "create_detached_thread(peer_closer): %s",
			    gfarm_error_string(e));
}

void
peer_clear_common(struct peer *peer)
{
	peer->next_close = NULL;
	peer->refcount = 0;
	peer->free_requested = 0;

	peer->username = NULL;
	peer->hostname = NULL;
	peer->user = NULL;
	peer->host = NULL;
	peer->peer_type = peer_type_foreground_channel;
	peer->private_peer_id = 0;

	/*
	 * foreground channel
	 */

	peer->process = NULL;
	peer->protocol_error = 0;
	peer->fd_current = GFARM_DESCRIPTOR_INVALID;
	peer->fd_saved = GFARM_DESCRIPTOR_INVALID;
	peer->flags = 0;
	peer->findxmlattrctx = NULL;
	peer->u.client.jobs = NULL;

	/* generation update, or generation update by cookie */
	peer->pending_new_generation = NULL;
	GFARM_HCIRCLEQ_INIT(peer->pending_new_generation_cookies, cookie_link);

	peer->iostatp = NULL;

	/* gfmd channel */
	peer->gfmdc_record = NULL;
}

void
peer_construct_common(struct peer *peer, struct peer_ops *ops,
	const char *diag)
{
	peer_clear_common(peer);
	peer->ops = ops;

	gfarm_mutex_init(&peer->protocol_error_mutex,
	    diag, "peer:protocol_error_mutex");
}

const char *
peer_get_service_name(struct peer *peer)
{
	return (peer == NULL ? "<null>" :
	    ((peer)->id_type == GFARM_AUTH_ID_TYPE_SPOOL_HOST ?  "gfsd" :
	    ((peer)->id_type == GFARM_AUTH_ID_TYPE_METADATA_HOST ?
	    "gfmd" : "client")));
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_free_common(struct peer *peer, const char *diag)
{
	char *username;

	username = peer_get_username(peer);

	/*
	 * both username and hostname may be null,
	 * if peer_authorized() hasn't been called in a local peer case.
	 *	(== authentication failed)
	 */
	(*peer->ops->notice_disconnected)(peer, peer_get_hostname(peer),
	    username != NULL ? username : "<unauthorized>");

	/*
	 * free resources for gfmd channel
	 */
	if (peer->gfmdc_record != NULL) {
		gfmdc_peer_record_free(peer->gfmdc_record, diag);
		peer->gfmdc_record = NULL;
	}

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

	peer_unset_pending_new_generation(peer, GFARM_ERR_CONNECTION_ABORTED);

	peer->findxmlattrctx = NULL;

	peer->protocol_error = 0;
	if (peer->process != NULL) {
		process_detach_peer(peer->process, peer);
		peer->process = NULL;
	}

	/*
	 * free common resources
	 */

	peer->private_peer_id = 0;

	peer->user = NULL;
	peer->host = NULL;
	if (peer->username != NULL) {
		free(peer->username); peer->username = NULL;
	}
	if (peer->hostname != NULL) {
		free(peer->hostname); peer->hostname = NULL;
	}

	peer->next_close = NULL;
	peer->refcount = 0;
	peer->free_requested = 0;
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_free(struct peer *peer)
{
	(*peer->ops->free)(peer);
}

struct local_peer *
peer_to_local_peer(struct peer *peer)
{
	return ((*peer->ops->peer_to_local_peer)(peer));
}

struct remote_peer *
peer_to_remote_peer(struct peer *peer)
{
	return ((*peer->ops->peer_to_remote_peer)(peer));
}

struct gfp_xdr *
peer_get_conn(struct peer *peer)
{
	return ((*peer->ops->get_conn)(peer));
}

gfp_xdr_async_peer_t
peer_get_async(struct peer *peer)
{
	return ((*peer->ops->get_async)(peer));
}

gfarm_error_t
peer_get_port(struct peer *peer, int *portp)
{
	return ((*peer->ops->get_port)(peer, portp));
}

gfarm_int64_t
peer_get_private_peer_id(struct peer *peer)
{
	return (peer->private_peer_id);
}

void
peer_set_private_peer_id(struct peer *peer)
{
	static const char diag[] = "peer_set_private_peer_id";

	gfarm_mutex_lock(&peer_seqno_mutex, diag, peer_seqno_diag);
	peer->private_peer_id = private_peer_id_seqno++;
	gfarm_mutex_unlock(&peer_seqno_mutex, diag, peer_seqno_diag);
}

struct peer *
peer_get_parent(struct peer *peer)
{
	return ((*peer->ops->get_parent)(peer));
}

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
	return (*peer->ops->get_mdhost)(peer);
}

enum peer_type
peer_get_peer_type(struct peer *peer)
{
	return (peer->peer_type);
}

void
peer_set_peer_type(struct peer *peer, enum peer_type peer_type)
{
	peer->peer_type = peer_type;
}

static void
protocol_state_init(struct protocol_state *ps)
{
	ps->nesting_level = 0;
}

void
peer_authorized_common(struct peer *peer,
	enum gfarm_auth_id_type id_type, char *username, char *hostname,
	struct sockaddr *addr, enum gfarm_auth_method auth_method)
{
	struct host *h;
	struct mdhost *m;

	protocol_state_init(&peer->pstate);

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
		h = addr != NULL ?
		    host_addr_lookup(hostname, addr) : host_lookup(hostname);
		if (h == NULL)
			peer->host = NULL;
		else
			peer->host = host_to_abstract_host(h);
		break;

	case GFARM_AUTH_ID_TYPE_METADATA_HOST:
		m = mdhost_lookup(hostname);
		if (m == NULL)
			peer->host = NULL;
		else
			peer->host = mdhost_to_abstract_host(m);
		break;

	default:
		break;
	}

	if (peer->host != NULL) {
		free(hostname);
		peer->hostname = NULL;
	} else
		peer->hostname = hostname;

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
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_set_pending_new_generation_by_fd(struct peer *peer, struct inode *inode)
{
	peer->pending_new_generation = inode;
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_reset_pending_new_generation_by_fd(struct peer *peer)
{
	peer->pending_new_generation = NULL;
}

/* NOTE: caller of this function should acquire giant_lock as well */
static void
peer_unset_pending_new_generation_by_fd(
	struct peer *peer, gfarm_error_t reason)
{
	if (peer->pending_new_generation != NULL) {
		inode_new_generation_by_fd_finish(peer->pending_new_generation,
		    peer, reason);
		peer->pending_new_generation = NULL;
	}
}

/* NOTE: caller of this function should acquire giant_lock as well */
gfarm_error_t
peer_add_pending_new_generation_by_cookie(
	struct peer *peer, struct inode *inode, gfarm_uint64_t *cookiep)
{
	static const char diag[] = "peer_add_cookie";
	struct pending_new_generation_by_cookie *cookie;
	gfarm_uint64_t result;

	GFARM_MALLOC(cookie);
	if (cookie == NULL) {
		gflog_debug(GFARM_MSG_1003277, "%s: no memory", diag);
		return (GFARM_ERR_NO_MEMORY);
	}

	gfarm_mutex_lock(&peer_seqno_mutex, diag, peer_seqno_diag);
	result = cookie_seqno++;
	gfarm_mutex_unlock(&peer_seqno_mutex, diag, peer_seqno_diag);

	cookie->inode = inode;
	*cookiep = cookie->id = result;
	GFARM_HCIRCLEQ_INSERT_HEAD(peer->pending_new_generation_cookies,
	    cookie, cookie_link);

	return (GFARM_ERR_NO_ERROR);
}

/* NOTE: caller of this function should acquire giant_lock as well */
int
peer_remove_pending_new_generation_by_cookie(
	struct peer *peer, gfarm_uint64_t cookie_id, struct inode **inodep)
{
	static const char diag[] = "peer_delete_cookie";
	struct pending_new_generation_by_cookie *cookie;
	int found = 0;

	GFARM_HCIRCLEQ_FOREACH(cookie, peer->pending_new_generation_cookies,
	    cookie_link) {
		if (cookie->id == cookie_id) {
			if (inodep != NULL)
				*inodep = cookie->inode;
			GFARM_HCIRCLEQ_REMOVE(cookie, cookie_link);
			free(cookie);
			found = 1;
			break;
		}
	}
	if (!found)
		gflog_warning(GFARM_MSG_1003278, "%s: bad cookie id %llu",
		    diag, (unsigned long long)cookie_id);

	return (found);
}

/* NOTE: caller of this function should acquire giant_lock as well */
static void
peer_unset_pending_new_generation_by_cookie(
	struct peer *peer, gfarm_error_t reason)
{
	struct pending_new_generation_by_cookie *cookie;

	while (!GFARM_HCIRCLEQ_EMPTY(peer->pending_new_generation_cookies,
	    cookie_link)) {
		cookie = GFARM_HCIRCLEQ_FIRST(
		    peer->pending_new_generation_cookies, cookie_link);
		inode_new_generation_by_cookie_finish(
		    cookie->inode, cookie->id, peer, reason);
		GFARM_HCIRCLEQ_REMOVE(cookie, cookie_link);
		free(cookie);
	}
	GFARM_HCIRCLEQ_INIT(peer->pending_new_generation_cookies, cookie_link);
}

/* NOTE: caller of this function should acquire giant_lock as well */
void
peer_unset_pending_new_generation(struct peer *peer, gfarm_error_t reason)
{
	/* pending_new_generation (file descriptor) case */
	peer_unset_pending_new_generation_by_fd(peer, reason);

	/* pending_new_generation_by_cookie (file handle) case */
	peer_unset_pending_new_generation_by_cookie(peer, reason);
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

	peer_unset_pending_new_generation_by_fd(
	    peer, GFARM_ERR_NO_SUCH_PROCESS);

	peer_fdpair_clear(peer);

	process_detach_peer(peer->process, peer);
	peer->process = NULL;
	peer->user = NULL;
}

void
peer_record_protocol_error(struct peer *peer)
{
	static const char diag[] = "peer_record_protocol_error";

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
	static const char diag[] = "peer_had_protocol_error";

	gfarm_mutex_lock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
	e = peer->protocol_error;
	gfarm_mutex_unlock(&peer->protocol_error_mutex, diag,
	    PROTOCOL_ERROR_MUTEX_DIAG);
	return (e);
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
		assert(peer->fd_current == GFARM_DESCRIPTOR_INVALID &&
		    peer->fd_saved == GFARM_DESCRIPTOR_INVALID);
		return;
	}
	if (peer->fd_current != GFARM_DESCRIPTOR_INVALID &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved /* prevent double close */ &&
	    (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(peer->fd_current))) {
		process_close_file(peer->process, peer, peer->fd_current, NULL);
	}
	if (peer->fd_saved != GFARM_DESCRIPTOR_INVALID &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0 &&
	    (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(peer->fd_saved))) {
		process_close_file(peer->process, peer, peer->fd_saved, NULL);
	}
	peer->fd_current = GFARM_DESCRIPTOR_INVALID;
	peer->fd_saved = GFARM_DESCRIPTOR_INVALID;
	peer->flags &= ~(
	    PEER_FLAGS_FD_CURRENT_EXTERNALIZED |
	    PEER_FLAGS_FD_SAVED_EXTERNALIZED);
}

gfarm_error_t
peer_fdpair_externalize_current(struct peer *peer)
{
	if (peer->fd_current == GFARM_DESCRIPTOR_INVALID) {
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
	if (peer->fd_current == GFARM_DESCRIPTOR_INVALID) {
		gflog_debug(GFARM_MSG_1001588,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}
	if (peer->fd_current == peer->fd_saved) {
		peer->flags &= ~PEER_FLAGS_FD_SAVED_EXTERNALIZED;
		peer->fd_saved = GFARM_DESCRIPTOR_INVALID;
	}
	peer->flags &= ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	peer->fd_current = GFARM_DESCRIPTOR_INVALID;
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
	if (peer->fd_current != GFARM_DESCRIPTOR_INVALID &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved /* prevent double close */ &&
	    (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(peer->fd_current))) {
		process_close_file(peer->process, peer, peer->fd_current, NULL);
	}
	peer->flags &= ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED;
	peer->fd_current = fd;
}

gfarm_error_t
peer_fdpair_get_current(struct peer *peer, gfarm_int32_t *fdp)
{
	if (peer->fd_current == GFARM_DESCRIPTOR_INVALID) {
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
	if (peer->fd_saved == GFARM_DESCRIPTOR_INVALID) {
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
	if (peer->fd_current == GFARM_DESCRIPTOR_INVALID) {
		gflog_debug(GFARM_MSG_1001591,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	if (peer->fd_saved != GFARM_DESCRIPTOR_INVALID &&
	    (peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) == 0 &&
	    peer->fd_saved != peer->fd_current /* prevent double close */ &&
	    (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(peer->fd_saved))) {
		process_close_file(peer->process, peer, peer->fd_saved, NULL);
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
	if (peer->fd_saved == GFARM_DESCRIPTOR_INVALID) {
		gflog_debug(GFARM_MSG_1001592,
			"bad file descriptor");
		return (GFARM_ERR_BAD_FILE_DESCRIPTOR);
	}

	if (peer->fd_current != GFARM_DESCRIPTOR_INVALID &&
	    (peer->flags & PEER_FLAGS_FD_CURRENT_EXTERNALIZED) == 0 &&
	    peer->fd_current != peer->fd_saved /* prevent double close */ &&
	    (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(peer->fd_current))) {
		process_close_file(peer->process, peer, peer->fd_current, NULL);
	}
	peer->fd_current = peer->fd_saved;
	peer->flags = (peer->flags & ~PEER_FLAGS_FD_CURRENT_EXTERNALIZED) |
	    ((peer->flags & PEER_FLAGS_FD_SAVED_EXTERNALIZED) ?
	     PEER_FLAGS_FD_CURRENT_EXTERNALIZED : 0);
	return (GFARM_ERR_NO_ERROR);
}

void
peer_findxmlattrctx_set(struct peer *peer, struct inum_path_array *ctx)
{
	peer->findxmlattrctx = ctx;
}

struct inum_path_array *
peer_findxmlattrctx_get(struct peer *peer)
{
	return (peer->findxmlattrctx);
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

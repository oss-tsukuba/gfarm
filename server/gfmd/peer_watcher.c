/*
 * combiled interface of watcher.c and thrpool.c, for peer.c and *_channel.c.
 * there is a plan to use this interface for writable_watcher too.
 */

#include <stddef.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "thrpool.h"

#include "watcher.h"
#include "peer_watcher.h"

struct peer_watcher {
	struct watcher *w;
	struct thread_pool *thrpool;
	void *(*handler)(void *);
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
	void *(*handler)(void *),
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

	pw->handler = handler;

	return (pw);
}

/* XXX this interface should be removed when backchannel queue is merged */
struct thread_pool *
peer_watcher_get_thrpool(struct peer_watcher *pw)
{
	return (pw->thrpool);
}

void
peer_watcher_schedule(struct peer_watcher *pw, struct local_peer *local_peer)
{
	thrpool_add_job(pw->thrpool, pw->handler, local_peer);
}

void
peer_watcher_add_event(struct peer_watcher *pw,
	struct watcher_event *event, struct local_peer *local_peer)
{
	watcher_add_event(pw->w, event, pw->thrpool, pw->handler, local_peer);
}

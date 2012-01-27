struct peer_watcher;

struct thread_pool;
struct local_peer;
struct watcher_event;

void peer_watcher_set_default_nfd(int);
struct peer_watcher *peer_watcher_alloc(int, int, 
	void *(*)(void *), const char *);
struct thread_pool *peer_watcher_get_thrpool(struct peer_watcher *);
void peer_watcher_schedule(struct peer_watcher *, struct local_peer *);
void peer_watcher_add_event(struct peer_watcher *,
	struct watcher_event *, struct local_peer *);

struct watcher_event;
struct thread_pool;
struct gfp_xdr;

gfarm_error_t watcher_gfp_xdr_readable_event_alloc(struct gfp_xdr *,
	struct watcher_event **);
gfarm_error_t watcher_gfp_xdr_writable_event_alloc(struct gfp_xdr *,
	struct watcher_event **);
gfarm_error_t watcher_socket_readable_or_timeout_event_alloc(int,
	struct watcher_event **);
void watcher_event_free(struct watcher_event *);
int watcher_event_is_readable(struct watcher_event *);
int watcher_event_is_active(struct watcher_event *);
void watcher_event_ack(struct watcher_event *);


struct watcher;

gfarm_error_t watcher_alloc(int, struct watcher **);

void watcher_add_event(struct watcher *, struct watcher_event *,
	struct thread_pool *, void *(*)(void *), void *);
void watcher_add_event_with_timeout(
	struct watcher *, struct watcher_event *, long,
	struct thread_pool *, void *(*)(void *), void *);

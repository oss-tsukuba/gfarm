struct watcher_event;
struct thread_pool;

gfarm_error_t watcher_fd_readable_event_alloc(int, struct watcher_event **);
gfarm_error_t watcher_fd_writable_event_alloc(int, struct watcher_event **);
gfarm_error_t watcher_timeout_event_alloc(struct watcher_event **);
gfarm_error_t watcher_closing_event_alloc(struct watcher_event **);
void watcher_event_free(struct watcher_event *);
void watcher_fd_closing_event_add_relevant_event(
	struct watcher_event *, struct watcher_event *);
int watcher_event_is_active(struct watcher_event *);
void watcher_event_ack(struct watcher_event *);


struct watcher;

gfarm_error_t watcher_alloc(int, struct watcher **);

void watcher_add_event(struct watcher *, struct watcher_event *,
	struct thread_pool *, void *(*)(void *), void *);
void watcher_add_timeout_event(struct watcher *, struct watcher_event *,
	long timeout_millisec,
	struct thread_pool *thrpool, void *(*)(void *), void *);

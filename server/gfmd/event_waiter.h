struct event_waiter_link {
	GFARM_HCIRCLEQ_ENTRY(event_waiter_link) event_link;
};

struct event_waiter_list {
	GFARM_HCIRCLEQ_HEAD(event_waiter_link) head;
};

struct event_waiter;

struct peer;
void event_waiter_list_init(struct event_waiter_list *);
gfarm_error_t event_waiter_alloc(struct peer *,
	gfarm_error_t (*)(struct peer *, void *, int *, gfarm_error_t),
	void *, struct event_waiter_list *);
gfarm_error_t event_waiter_with_timeout_alloc(struct peer *, int,
	gfarm_error_t (*)(struct peer *, void *, int *, gfarm_error_t),
	void *, struct event_waiter_list *);
struct peer *event_waiter_get_peer(struct event_waiter *);
void event_waiters_signal(struct event_waiter_list *, gfarm_error_t);
gfarm_error_t event_waiter_call_action(struct event_waiter *, int *);
void event_waiter_free(struct event_waiter *);

void resuming_enqueue(struct event_waiter *);
struct event_waiter *resuming_dequeue(const char *);

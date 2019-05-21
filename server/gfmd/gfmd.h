extern int gfmd_port;

struct peer;

#ifdef USE_EVENT_WAITER

struct event_waiter_link {
	GFARM_HCIRCLEQ_ENTRY(event_waiter_link) event_link;
};

struct event_waiter_list {
	GFARM_HCIRCLEQ_HEAD(event_waiter_link) head;
};

struct event_waiter;
void event_waiter_list_init(struct event_waiter_list *);
gfarm_error_t event_waiter_alloc(struct peer *,
	gfarm_error_t (*)(struct peer *, void *, int *, gfarm_error_t),
	void *, struct event_waiter_list *);
gfarm_error_t event_waiter_with_timeout_alloc(struct peer *, int,
	gfarm_error_t (*)(struct peer *, void *, int *, gfarm_error_t),
	void *, struct event_waiter_list *);
void event_waiters_signal(struct event_waiter_list *, gfarm_error_t);

#endif /* USE_EVENT_WAITER */

 /*
  * The following part exports hook points for a private extension.
  *
  * The official gfmd source code shouldn't use these interface.
  */

struct thread_pool;
extern struct thread_pool *authentication_thread_pool;
struct thread_pool *sync_protocol_get_thrpool(void);

gfarm_error_t gfm_server_protocol_extension_default(struct peer *,
	int, int, int, gfarm_int32_t, gfarm_int32_t *, gfarm_error_t *);
extern gfarm_error_t (*gfm_server_protocol_extension)(struct peer *,
	int, int, int, gfarm_int32_t, gfarm_int32_t *, gfarm_error_t *);

int protocol_service(struct peer *);
void *protocol_main(void *);
void gfmd_terminate(const char *);

void gfmd_modules_init_default(int);
extern void (*gfmd_modules_init)(int);

/* faillover_notify.c */
void failover_notify(void);



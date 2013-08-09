extern int gfmd_port;

struct peer;
struct event_waiter {
	struct event_waiter *next;

	struct peer *peer;
	gfarm_error_t (*action)(struct peer *, void *, int *);
	void *arg;
};

void resuming_enqueue(struct event_waiter *);

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

  

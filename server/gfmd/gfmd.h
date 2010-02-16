 /*
  * This header exports hook points for a private extension.
  *
  * The official gfmd source code shouldn't include this header.
  */

struct thread_pool *client_thread_pool;

gfarm_error_t gfm_server_protocol_extension_default(struct peer *,
	int, int, int, gfarm_int32_t, gfarm_int32_t *, gfarm_error_t *);
extern gfarm_error_t (*gfm_server_protocol_extension)(struct peer *,
	int, int, int, gfarm_int32_t, gfarm_int32_t *, gfarm_error_t *);

int protocol_service(struct peer *);
void *protocol_main(void *);

void gfmd_modules_init_default(int);
extern void (*gfmd_modules_init)(int);

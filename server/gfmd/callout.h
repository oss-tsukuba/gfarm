/*
 * See the following page for the overview of this module:
 *	http://man.netbsd.se/?find=callout+9+30
 *
 * There are some naming differences, though.
 * e.g.
 * callout_startup(9):	callout_module_init()
 * callout_init(9)	callout_new()
 * callout_destroy(9):	callout_free()
 */

struct callout;
struct thread_pool;

void callout_module_init(int);
struct callout *callout_new(void);
void callout_free(struct callout *);
void callout_schedule(struct callout *, int);
void callout_reset(struct callout *, int,
	struct thread_pool *, void *(*)(void *), void *);
void callout_setfunc(struct callout *,
	struct thread_pool *, void *(*)(void *), void *);
int callout_stop(struct callout *);
int callout_invoking(struct callout *);
void callout_ack(struct callout *);

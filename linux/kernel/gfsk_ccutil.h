#define GFCC_MUTEX_T	struct mutex
#define GFCC_MUTEX_INIT(x)	mutex_init(x)
#define GFCC_MUTEX_LOCK(x)	mutex_lock(x)
#define GFCC_MUTEX_UNLOCK(x)	mutex_unlock(x)
#define GFCC_MUTEX_DESTROY(x)	mutex_destroy(x)

#define GFCC_COND_T		wait_queue_head_t
#define GFCC_COND_INIT(x)	init_waitqueue_head(x)
#define GFCC_COND_WAIT(x, m, c)	({ int __err = 0; GFCC_MUTEX_UNLOCK(m); \
		__err = wait_event_interruptible(x, c); GFCC_MUTEX_LOCK(m); \
		__err; })

#define GFCC_COND_TIMED_WAIT(x, condition, t) \
	wait_event_interruptible_timeout(x, condition, t)
#define GFCC_COND_SIGNAL(x, m)	wake_up(x)
#define GFCC_COND_BROADCAST(x, m)	wake_up_all(x)
#define GFCC_COND_DESTROY(x)
#define GFCC_ALLOC(ptr, num, err)	\
if (!(ptr = kmalloc(sizeof(*ptr) * (num), GFP_KERNEL))) { \
	gflog_error(GFARM_MSG_UNFIXED, "alloc %ld", sizeof(*ptr) * (num)); \
	err; }
#define	GFREE(ptr)	kfree(ptr)


#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/wait.h>                 /* wait_queue_head_t, etc */
#include <linux/spinlock.h>             /* spinlock_t, etc */
#include <asm/atomic.h>                 /* atomic_t, etc */
#include <gfarm/gfarm.h>
#include "gfsk.h"
#include "gfsk_fs.h"
#include "gfsk_ccutil.h"
#include "gfsk_stack.h"
#include "gfsk_proc.h"

int
gfcc_stack_init(struct gfcc_stack *stp, int num, int size,
			void *data, int alloc)
{
	char *dp = data;

	memset(stp, 0, sizeof(*stp));
	stp->s_size = size;
	stp->s_num = num;
	stp->s_data = data;
	stp->s_anum = 4;
	stp->s_ause = 0;
	GFCC_ALLOC(stp->s_alloc, stp->s_anum, return -ENOMEM);
	memset(stp->s_alloc, 0, sizeof(void **) * stp->s_anum);
	GFCC_ALLOC(stp->s_stack, num, return -ENOMEM);
	if (alloc) {
		GFCC_ALLOC(dp, size * num, return -ENOMEM);
		memset(dp, 0, size * num);
		stp->s_alloc[stp->s_ause++] = dp;
	}
	if (size && dp) {
		int	i;
		for (i = 0; i < num; i++) {
			stp->s_stack[i] = dp;
			dp += size;
		}
		stp->s_free = i ;
	} else
		stp->s_free = 0 ;
	GFCC_MUTEX_INIT(&stp->s_lock);
	GFCC_COND_INIT(&stp->s_wait);
	return (0);
}
void
gfcc_stack_fini(struct gfcc_stack *stp, void (*dtr)(void *))
{
	int i, j;
	if (stp) {
		if (stp->s_num != stp->s_free) {
			gflog_warning(GFARM_MSG_1004983, "still used %d/%d",
					stp->s_free, stp->s_num);
		}
		for (i = 0; i < stp->s_ause; i++) {
			if (dtr) {
				int num = stp->s_num / stp->s_ause;
				char *dp = stp->s_alloc[i];
				for (j = 0; j < num; j++) {
					dtr((void *)dp);
					dp += stp->s_size;
				}
			}
			GFREE(stp->s_alloc[i]);
		}
		GFREE(stp->s_alloc);
		GFREE(stp->s_stack);
	}
}
void *
gfcc_stack_get(struct gfcc_stack *stp, int nowait)
{
	int err = 0;
	void *data = NULL;

	GFCC_MUTEX_LOCK(&stp->s_lock);
loop:
	if (stp->s_free) {
		data = stp->s_stack[--stp->s_free];
	} else if (!nowait) {
		if (stp->s_alloc[0] && stp->s_ause < stp->s_anum) {
			void **tmp;
			int i, num = stp->s_num / stp->s_ause;
			int snum = num * (stp->s_ause + 1);
			GFCC_ALLOC(tmp,  snum, ;);
			if (tmp) {
				GFREE(stp->s_stack);
				memset(tmp, 0, sizeof(tmp) * snum);
				stp->s_stack = tmp;
			}
			GFCC_ALLOC(tmp,  num * stp->s_size, ;);
			if (tmp) {
				memset(tmp, 0, stp->s_size * num);
				stp->s_alloc[stp->s_ause++] = tmp;
				for (i = 0; i < num; i++) {
					stp->s_stack[i] = (char *)tmp
						+ i * stp->s_size;
				}
				stp->s_free = num;
				stp->s_num += num;
				goto loop;
			}
		}
		stp->s_waiting++;
		err = GFCC_COND_WAIT(stp->s_wait, &stp->s_lock,
					stp->s_free > 0);
		stp->s_waiting--;
		if (!err)
			goto loop;
	}
	GFCC_MUTEX_UNLOCK(&stp->s_lock);
	return (data);
}
int
gfcc_stack_put(void *data, struct gfcc_stack *stp)
{
	int err = 0;

	GFCC_MUTEX_LOCK(&stp->s_lock);
	if (stp->s_free + 1 > stp->s_num) {
		gflog_error(GFARM_MSG_1004984, "too many put");
		err = -EINVAL;
	} else {
		stp->s_stack[stp->s_free++] = data;
		if (stp->s_waiting) {
			GFCC_COND_BROADCAST(&stp->s_wait, &stp->s_lock);
		}
	}
	GFCC_MUTEX_UNLOCK(&stp->s_lock);
	return (err);
}

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/anon_inodes.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <gfarm/gflog.h>
#include "gfsk_fs.h"

struct gfsk_evfd_ctx {
	wait_queue_head_t wqh;
	int count;
};

static void
gfsk_evfd_free_ctx(struct gfsk_evfd_ctx *ctx)
{
	kfree(ctx);
}

static int
gfsk_evfd_release(struct inode *inode, struct file *file)
{
	struct gfsk_evfd_ctx *ctx = file->private_data;

	wake_up_poll(&ctx->wqh, POLLHUP);
	kfree(ctx);
	return (0);
}

static unsigned int
gfsk_evfd_poll(struct file *file, poll_table *wait)
{
	struct gfsk_evfd_ctx *ctx = file->private_data;
	unsigned int events = 0;
	unsigned long flags;

	poll_wait(file, &ctx->wqh, wait);

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	if (ctx->count > 0)
		events |= POLLIN;
	if (ctx->count == INT_MAX)
		events |= POLLERR;
	if (INT_MAX - 1 > ctx->count)
		events |= POLLOUT;
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);

	return (events);
}

ssize_t
gfsk_evfd_ctx_read(struct gfsk_evfd_ctx *ctx, int no_wait, int cnt)
{
	ssize_t res;
	DECLARE_WAITQUEUE(wait, current);

	spin_lock_irq(&ctx->wqh.lock);
	res = -EAGAIN;
	if (ctx->count > 0)
		res = 1;
	else if (!no_wait) {
		__add_wait_queue(&ctx->wqh, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (ctx->count > 0) {
				res = 1;
				break;
			}
			if (signal_pending(current)) {
				res = -ERESTARTSYS;
				break;
			}
			spin_unlock_irq(&ctx->wqh.lock);
			schedule();
			spin_lock_irq(&ctx->wqh.lock);
		}
		__remove_wait_queue(&ctx->wqh, &wait);
		__set_current_state(TASK_RUNNING);
	}
	if (likely(res == 1)) {
		ctx->count -= 1;
		if (waitqueue_active(&ctx->wqh))
			wake_up_locked_poll(&ctx->wqh, POLLOUT);
	}
	spin_unlock_irq(&ctx->wqh.lock);

	return (res);
}
ssize_t
gfsk_evfd_ctx_write(struct gfsk_evfd_ctx *ctx, int no_wait, int cnt)
{
	ssize_t res;
	DECLARE_WAITQUEUE(wait, current);

	spin_lock_irq(&ctx->wqh.lock);
	res = -EAGAIN;
	if (ctx->count < INT_MAX - 1)
		res = 1;
	else if (!no_wait) {
		__add_wait_queue(&ctx->wqh, &wait);
		for (;;) {
			set_current_state(TASK_INTERRUPTIBLE);
			if (ctx->count < INT_MAX - 1) {
				res = 1;
				break;
			}
			if (signal_pending(current)) {
				res = -ERESTARTSYS;
				break;
			}
			spin_unlock_irq(&ctx->wqh.lock);
			schedule();
			spin_lock_irq(&ctx->wqh.lock);
		}
		__remove_wait_queue(&ctx->wqh, &wait);
		__set_current_state(TASK_RUNNING);
	}
	if (likely(res == 1)) {
		ctx->count += 1;
		if (waitqueue_active(&ctx->wqh))
			wake_up_locked_poll(&ctx->wqh, POLLIN);
	}
	spin_unlock_irq(&ctx->wqh.lock);

	return (res);
}

static ssize_t
gfsk_evfd_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct gfsk_evfd_ctx *ctx = file->private_data;

	count = gfsk_evfd_ctx_read(ctx, file->f_flags & O_NONBLOCK, 1);
	return (count);

}

static ssize_t
gfsk_evfd_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct gfsk_evfd_ctx *ctx = file->private_data;

	count = gfsk_evfd_ctx_write(ctx, file->f_flags & O_NONBLOCK, 1);
	return (count);

}

static const struct file_operations gfsk_evfd_fops = {
	.release	= gfsk_evfd_release,
	.poll		= gfsk_evfd_poll,
	.read		= gfsk_evfd_read,
	.write		= gfsk_evfd_write
};


struct file *
gfsk_evfd_file_create(unsigned int count)
{
	struct file *file;
	struct gfsk_evfd_ctx *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return (ERR_PTR(-ENOMEM));

	init_waitqueue_head(&ctx->wqh);
	ctx->count = count;

	file = anon_inode_getfile("[gfsk_evfd]", &gfsk_evfd_fops, ctx, O_RDWR);
	if (IS_ERR(file))
		gfsk_evfd_free_ctx(ctx);

	return (file);
}
int
gfsk_evfd_create(unsigned int count)
{
	int fd;

	struct file *file;
	file = gfsk_evfd_file_create(count);
	if (IS_ERR(file)) {
		fd = PTR_ERR(file);
		gflog_error(GFARM_MSG_UNFIXED, "fail gfsk_evfd_file_create %d"
			, fd);
	} else {
		if ((fd = gfsk_fd_file_set(file)) < 0)
			gflog_error(GFARM_MSG_UNFIXED,
				"fail gfsk_fd_file_set %d", fd);
		fput(file);
	}
	return (fd);
}
void
gfsk_evfd_signal(struct file *file)
{
	struct gfsk_evfd_ctx *ctx = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ctx->wqh.lock, flags);
	ctx->count += 1;
	if (waitqueue_active(&ctx->wqh))
		wake_up_locked_poll(&ctx->wqh, POLLIN);
	spin_unlock_irqrestore(&ctx->wqh.lock, flags);
}

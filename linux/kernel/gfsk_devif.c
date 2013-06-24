#include "gfsk_devif.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>

static struct kmem_cache *gfskdev_req_cachep;

struct gfskdev_conn *
gfskdev_conn_alloc(void)
{
	struct gfskdev_conn *dc;
	dc = kmalloc(sizeof(*dc), GFP_KERNEL);
	if (!dc) {
		return (NULL);
	}
	memset(dc, 0, sizeof(*dc));
	spin_lock_init(&dc->lock);
	atomic_set(&dc->count, 1);
	init_waitqueue_head(&dc->waitq);
	init_waitqueue_head(&dc->blocked_waitq);
	INIT_LIST_HEAD(&dc->pending);
	INIT_LIST_HEAD(&dc->processing);
	INIT_LIST_HEAD(&dc->io);
	atomic_set(&dc->num_waiting, 0);
	dc->reqctr = 0;
	dc->blocked = 1;

	return (dc);
}

void
gfskdev_conn_put(struct gfskdev_conn *dc)
{
	if (atomic_dec_and_test(&dc->count)) {
		dc->release(dc);
	}
}

struct gfskdev_conn *
gfskdev_conn_get(struct gfskdev_conn *dc)
{
	atomic_inc(&dc->count);
	return (dc);
}


static struct gfskdev_conn *
gfskdev_get_conn(struct file *file)
{
	/*
	 * Lockless access is OK, because file->private data is set
	 * once during mount and is valid until the file is released.
	 */
	return (file->private_data);
}

struct gfskdev_req *
gfskdev_request_alloc(void)
{
	struct gfskdev_req *req = kmem_cache_alloc(gfskdev_req_cachep,
			GFP_KERNEL);
	if (req) {
		memset(req, 0, sizeof(*req));
		INIT_LIST_HEAD(&req->list);
		init_waitqueue_head(&req->waitq);
		atomic_set(&req->count, 1);
	}
	return (req);
}
void
gfskdev_request_free(struct gfskdev_req *req)
{
	kmem_cache_free(gfskdev_req_cachep, req);
}

static void
block_sigs(sigset_t *oldset)
{
	sigset_t mask;

	siginitsetinv(&mask, sigmask(SIGKILL));
	sigprocmask(SIG_BLOCK, &mask, oldset);
}

static void
restore_sigs(sigset_t *oldset)
{
	sigprocmask(SIG_SETMASK, oldset, NULL);
}

static void
__gfskdev_get_request(struct gfskdev_req *req)
{
	atomic_inc(&req->count);
}

/* Must be called with > 1 refcount */
static void
__gfskdev_put_request(struct gfskdev_req *req)
{
	BUG_ON(atomic_read(&req->count) < 2);
	atomic_dec(&req->count);
}

static void
gfskdev_req_init_context(struct gfskdev_req *req)
{
	req->in.h.uid = current_fsuid();
	req->in.h.gid = current_fsgid();
	req->in.h.pid = current->pid;
}

struct gfskdev_req *
gfskdev_get_req(struct gfskdev_conn *dc)
{
	struct gfskdev_req *req;
	sigset_t oldset;
	int intr;
	int err;

	atomic_inc(&dc->num_waiting);
	block_sigs(&oldset);
	intr = wait_event_interruptible(dc->blocked_waitq, !dc->blocked);
	restore_sigs(&oldset);
	err = -EINTR;
	if (intr)
		goto out;

	err = -ENOTCONN;
	if (!dc->connected)
		goto out;

	req = gfskdev_request_alloc();
	err = -ENOMEM;
	if (!req)
		goto out;

	gfskdev_req_init_context(req);
	req->waiting = 1;
	return (req);

 out:
	atomic_dec(&dc->num_waiting);
	return (ERR_PTR(err));
}


void
gfskdev_put_request(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	if (atomic_dec_and_test(&req->count)) {
		if (req->waiting)
			atomic_dec(&dc->num_waiting);
		gfskdev_request_free(req);
	}
}

static unsigned
len_args(unsigned numargs, struct gfskdev_arg *args)
{
	unsigned nbytes = 0;
	unsigned i;

	for (i = 0; i < numargs; i++)
		nbytes += args[i].size;

	return (nbytes);
}

static u64
gfskdev_get_unique(struct gfskdev_conn *dc)
{
	dc->reqctr++;
	/* zero is special */
	if (dc->reqctr == 0)
		dc->reqctr = 1;

	return (dc->reqctr);
}

static void
queue_request(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	req->in.h.unique = gfskdev_get_unique(dc);
	req->in.h.len = sizeof(struct gfskdev_in_header) +
		len_args(req->in.numargs, (struct gfskdev_arg *) req->in.args);
	list_add_tail(&req->list, &dc->pending);
	req->state = GFSKDEV_REQ_PENDING;
	if (!req->waiting) {
		req->waiting = 1;
		atomic_inc(&dc->num_waiting);
	}
	wake_up(&dc->waitq);
	kill_fasync(&dc->fasync, SIGIO, POLL_IN);
}
/*
 * Called with dc->lock, unlocks it
 */
static void
request_end(struct gfskdev_conn *dc, struct gfskdev_req *req)
__releases(&dc->lock)
{
	void (*end) (struct gfskdev_conn *, struct gfskdev_req *) = req->end;

	req->end = NULL;
	list_del(&req->list);
	req->state = GFSKDEV_REQ_FINISHED;
	spin_unlock(&dc->lock);
	wake_up(&req->waitq);
	if (end)
		end(dc, req);
	gfskdev_put_request(dc, req);
}

static void
wait_answer_interruptible(struct gfskdev_conn *dc,
				struct gfskdev_req *req)
__releases(&dc->lock)
__acquires(&dc->lock)
{
	if (signal_pending(current))
		return;

	spin_unlock(&dc->lock);
	wait_event_interruptible(req->waitq,
			req->state == GFSKDEV_REQ_FINISHED);
	spin_lock(&dc->lock);
}

static void
request_wait_answer(struct gfskdev_conn *dc, struct gfskdev_req *req)
__releases(&dc->lock)
__acquires(&dc->lock)
{
	wait_answer_interruptible(dc, req);

	if (req->aborted) {
		if (req->locked) {
			spin_unlock(&dc->lock);
			wait_event(req->waitq, !req->locked);
			spin_lock(&dc->lock);
		}
	} else if (req->state != GFSKDEV_REQ_FINISHED) {
		list_del(&req->list);
		__gfskdev_put_request(req);
		req->out.h.error = -EINTR;
	}
}

void
gfskdev_request_send(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	req->isreply = 1;
	spin_lock(&dc->lock);
	if (!dc->connected)
		req->out.h.error = -ENOTCONN;
	else if (dc->conn_error)
		req->out.h.error = -ECONNREFUSED;
	else {
		queue_request(dc, req);
		__gfskdev_get_request(req);	/* ref for request_end() */

		request_wait_answer(dc, req);
	}
	spin_unlock(&dc->lock);
}

static void
gfskdev_request_send_nowait_locked(struct gfskdev_conn *dc,
					struct gfskdev_req *req)
{
	req->background = 1;
	queue_request(dc, req);
}

static void
gfskdev_request_send_nowait(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	spin_lock(&dc->lock);
	__gfskdev_get_request(req);	/* for client put */
	if (dc->connected) {
		gfskdev_request_send_nowait_locked(dc, req);
		spin_unlock(&dc->lock);
	} else {
		req->out.h.error = -ENOTCONN;
		request_end(dc, req);
	}
}

void
gfskdev_request_send_background(struct gfskdev_conn *dc,
		struct gfskdev_req *req)
{
	req->isreply = 1;
	gfskdev_request_send_nowait(dc, req);
}

static int request_pending(struct gfskdev_conn *dc)
{
	return (!list_empty(&dc->pending));
}

/* Wait until a request is available on the pending list */
static void request_wait(struct gfskdev_conn *dc)
__releases(&dc->lock)
__acquires(&dc->lock)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue_exclusive(&dc->waitq, &wait);
	while (dc->connected && !request_pending(dc)) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (signal_pending(current))
			break;

		spin_unlock(&dc->lock);
		schedule();
		spin_lock(&dc->lock);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dc->waitq, &wait);
}

static int
gfskdev_copy_usriov(int write, char *buf, ssize_t size,
		struct iovec *iov, long nr_segs)
{
	ssize_t len;
	int err = 0;

	for ( ; nr_segs-- > 0 && size > 0 && !err; iov++) {
		if ((len = iov->iov_len) > size)
			len = size;
		if (!len)
			continue;
		if (write)
			err = copy_from_user(buf, iov->iov_base, len);
		else
			err = copy_to_user(iov->iov_base, buf, len);
		iov->iov_base += len;
		iov->iov_len -= len;
		buf += len;
		size -= len;
	}
	return (err ? -EFAULT : 0);
}
static ssize_t
gfskdev_aio_read(struct kiocb *iocb, const struct iovec *oiov,
			unsigned long nr_segs, loff_t pos)
{
	int err, i;
	size_t nbytes = iov_length(oiov, nr_segs);
	struct gfskdev_req *req;
	struct gfskdev_in *in;
	unsigned int reqsize;
	struct file *file = iocb->ki_filp;
	struct gfskdev_conn *dc = gfskdev_get_conn(file);
	struct iovec xiov[4], *iov = xiov;

	if (!dc)
		return (-EPERM);
	if (nbytes < sizeof(in->h)) {
		return (-EINVAL);
	}
	if (nr_segs > 4) {
		printk(KERN_ERR "%s: too many iovec %ld)\n",
			__func__, nr_segs);
		return (-EINVAL);
	}
	for (i = 0; i < nr_segs; i++)
		xiov[i] = *(oiov + i);

restart:
	spin_lock(&dc->lock);
	err = -EAGAIN;
	if ((file->f_flags & O_NONBLOCK) && dc->connected &&
		!request_pending(dc))
		goto err_unlock;

	request_wait(dc);
	err = -ENODEV;
	if (!dc->connected)
		goto err_unlock;
	err = -ERESTARTSYS;
	if (!request_pending(dc))
		goto err_unlock;

	req = list_entry(dc->pending.next, struct gfskdev_req, list);
	req->state = GFSKDEV_REQ_READING;
	list_move(&req->list, &dc->io);

	in = &req->in;
	reqsize = in->h.len;
	/* If request is too large, reply with an error and restart the read */
	if (nbytes < reqsize) {
		req->out.h.error = -EIO;
		request_end(dc, req);
		/* unlocked */
		goto restart;
	}
	req->locked = 1;
	spin_unlock(&dc->lock);
	err = gfskdev_copy_usriov(0, (char *)&in->h, sizeof(in->h),
				iov, nr_segs);
	for (i = 0; !err && i < in->numargs; i++) {
		err = gfskdev_copy_usriov(0, (char *)in->args[i].value,
				in->args[i].size, iov, nr_segs);
	}
	spin_lock(&dc->lock);
	req->locked = 0;
	if (req->aborted) {
		request_end(dc, req);
		return (-ENODEV);
	}
	if (err) {
		req->out.h.error = -EIO;
		request_end(dc, req);
		return (err);
	}
	if (!req->isreply)
		/* packet is request_send_noreply()ed */
		request_end(dc, req);
	else {
		req->state = GFSKDEV_REQ_SENT;
		list_move_tail(&req->list, &dc->processing);
		spin_unlock(&dc->lock);
	}
	return (reqsize);

err_unlock:
	spin_unlock(&dc->lock);
	return (err);
}

/* Look up request on processing list by unique ID */
static struct
gfskdev_req *request_find(struct gfskdev_conn *dc, u64 unique)
{
	struct list_head *entry;

	list_for_each(entry, &dc->processing) {
		struct gfskdev_req *req;
		req = list_entry(entry, struct gfskdev_req, list);
		if (req->in.h.unique == unique)
			return (req);
	}
	return (NULL);
}

static ssize_t
gfskdev_aio_write(struct kiocb *iocb, const struct iovec *oiov,
				unsigned long nr_segs, loff_t pos)
{
	int err, i;
	size_t nbytes = iov_length(oiov, nr_segs);
	struct gfskdev_req *req;
	struct gfskdev_out_header oh;
	struct file *file = iocb->ki_filp;
	struct gfskdev_conn *dc = gfskdev_get_conn(file);
	struct iovec xiov[4], *iov = xiov;

	if (!dc)
		return (-EPERM);

	if (nbytes < sizeof(struct gfskdev_out_header)) {
		printk(KERN_ERR "%s: too small nbytes %ld)\n",
			__func__, nbytes);
		return (-EINVAL);
	}
	if (nr_segs > 4) {
		printk(KERN_ERR "%s: too many iovec %ld)\n",
			__func__, nr_segs);
		return (-EINVAL);
	}
	for (i = 0; i < nr_segs; i++)
		xiov[i] = *(oiov + i);
	err = gfskdev_copy_usriov(1, (char *)&oh, sizeof(oh), iov, nr_segs);
	if (err) {
		printk(KERN_ERR "%s: gfskdev_copy_usriov fail\n", __func__);
		goto err_finish;
	}
	err = -EINVAL;
	if (oh.len != nbytes) {
		printk(KERN_ERR "%s: len(%u) != %ld\n",
			__func__, oh.len, nbytes);
		goto err_finish;
	}
	if (!oh.unique) {
		printk(KERN_ERR "%s: unique is 0 \n", __func__);
		goto err_finish;
	}

	spin_lock(&dc->lock);
	err = -ENOENT;
	if (!dc->connected)
		goto err_unlock;

	req = request_find(dc, oh.unique);
	if (!req) {
		printk(KERN_ERR "%s: unique(%lu) not found\n",
			__func__, oh.unique);
		goto err_unlock;
	}

	if (req->aborted) {
		request_end(dc, req);
		/* unlocked */
		goto err_finish;
	}

	req->out.h = oh;
	req->locked = 1;
	spin_unlock(&dc->lock);
	err = 0;
	for (i = 0; !err && i < req->out.numargs; i++) {
		err = gfskdev_copy_usriov(1, (char *)req->out.args[i].value,
				req->out.args[i].size, iov, nr_segs);
	}
	spin_lock(&dc->lock);
	req->locked = 0;
	if (req->aborted) {
		err = -EIO;
		request_end(dc, req);
		goto err_finish;
	}
	if (err) {
		req->out.h.error = err;
		request_end(dc, req);
		goto err_finish;
	}

	request_end(dc, req);
	/* unlocked */
	return (nbytes);

err_unlock:
	spin_unlock(&dc->lock);
err_finish:
	return (err);
}

static unsigned
gfskdev_poll(struct file *file, poll_table *wait)
{
	unsigned mask = POLLOUT | POLLWRNORM;
	struct gfskdev_conn *dc = gfskdev_get_conn(file);
	if (!dc)
		return (POLLERR);

	poll_wait(file, &dc->waitq, wait);

	spin_lock(&dc->lock);
	if (!dc->connected)
		mask = POLLERR;
	else if (request_pending(dc))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock(&dc->lock);

	return (mask);
}

/*
 * Abort all requests on the given list (pending or processing)
 *
 * This function releases and reacquires dc->lock
 */
static void
end_requests(struct gfskdev_conn *dc, struct list_head *head)
__releases(&dc->lock)
__acquires(&dc->lock)
{
	while (!list_empty(head)) {
		struct gfskdev_req *req;
		req = list_entry(head->next, struct gfskdev_req, list);
		req->out.h.error = -ECONNABORTED;
		request_end(dc, req);
		spin_lock(&dc->lock);
	}
}

/*
 * Abort requests under I/O
 */
static void
end_io_requests(struct gfskdev_conn *dc)
__releases(&dc->lock)
__acquires(&dc->lock)
{
	while (!list_empty(&dc->io)) {
		struct gfskdev_req *req =
			list_entry(dc->io.next, struct gfskdev_req, list);
		void (*end) (struct gfskdev_conn *, struct gfskdev_req *)
				= req->end;

		req->aborted = 1;
		req->out.h.error = -ECONNABORTED;
		req->state = GFSKDEV_REQ_FINISHED;
		list_del_init(&req->list);
		wake_up(&req->waitq);
		if (end) {
			req->end = NULL;
			__gfskdev_get_request(req);
			spin_unlock(&dc->lock);
			wait_event(req->waitq, !req->locked);
			end(dc, req);
			gfskdev_put_request(dc, req);
			spin_lock(&dc->lock);
		}
	}
}

/*
 * Abort all requests.
 */
void
gfskdev_abort_conn(struct gfskdev_conn *dc)
{
	spin_lock(&dc->lock);
	if (dc->connected) {
		dc->connected = 0;
		dc->blocked = 0;
		end_io_requests(dc);
		end_requests(dc, &dc->pending);
		end_requests(dc, &dc->processing);
		wake_up_all(&dc->waitq);
		wake_up_all(&dc->blocked_waitq);
		kill_fasync(&dc->fasync, SIGIO, POLL_IN);
	}
	spin_unlock(&dc->lock);
}

int
gfskdev_release(struct inode *inode, struct file *file)
{
	struct gfskdev_conn *dc = gfskdev_get_conn(file);
	if (dc) {
		spin_lock(&dc->lock);
		dc->connected = 0;
		end_requests(dc, &dc->pending);
		end_requests(dc, &dc->processing);
		spin_unlock(&dc->lock);
		gfskdev_conn_put(dc);
	}

	return (0);
}

static int
gfskdev_fasync(int fd, struct file *file, int on)
{
	struct gfskdev_conn *dc = gfskdev_get_conn(file);
	if (!dc)
		return (-EPERM);

	/* No locking - fasync_helper does its own locking */
	return (fasync_helper(fd, file, on, &dc->fasync));
}

const struct file_operations gfskdev_operations = {
	.owner		= THIS_MODULE,
	.read		= do_sync_read,
	.aio_read	= gfskdev_aio_read,
	.write		= do_sync_write,
	.aio_write	= gfskdev_aio_write,
	.poll		= gfskdev_poll,
	.release	= gfskdev_release,
	.fasync		= gfskdev_fasync,
};

static struct miscdevice gfskdev_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "gfarm",
	.fops = &gfskdev_operations,
};

int __init
gfskdev_init(void)
{
	int err = -ENOMEM;
	gfskdev_req_cachep = kmem_cache_create("gfskdev_request",
				sizeof(struct gfskdev_req), 0, 0, NULL);
	if (!gfskdev_req_cachep)
		goto out;

	err = misc_register(&gfskdev_miscdevice);
	if (err)
		goto out_cache_clean;

	return (0);

 out_cache_clean:
	kmem_cache_destroy(gfskdev_req_cachep);
	gfskdev_req_cachep = NULL;
 out:
	return (err);
}

void
gfskdev_fini(void)
{
	if (gfskdev_req_cachep) {
		misc_deregister(&gfskdev_miscdevice);
		kmem_cache_destroy(gfskdev_req_cachep);
	}
}

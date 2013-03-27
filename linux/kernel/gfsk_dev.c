#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/fuse.h>
#include "fuse_i.h"
#include "gfsk.h"
#include "gfsk_if.h"
#include "gfsk_fs.h"
#include <gfarm/gflog.h>
#include <unistd.h>

#define GFSK_CONN_FILE(file)	((file)->private_data)

static void
gfsk_free_conn(struct fuse_conn *fc)
{
	kfree(fc);
}

#define WAKE_SEM	1
#define WAKE_FD	2
static void
gfsk_client_connect_proc(int kind, struct fuse_conn *fc, struct fuse_req *req)
{
	struct gfsk_rpl_connect *outarg = req->out.args[0].value;
	void *ev = req->ff;

	if (ev) {
		int fd = -1;
		int unset = 0;
		GFSK_CTX_DECLARE_SB(fc->sb);

		GFSK_CTX_SET_FORCE();

		spin_lock(&fc->lock);
		if (req->ff) {
			if (!req->out.h.error)
				fd = outarg->r_fd;
			else
				fd = req->out.h.error;
		}
		spin_unlock(&fc->lock);

		if (fd >= 0)
			fd = gfsk_fd_set(fd, S_IFSOCK);

		spin_lock(&fc->lock);
		if (req->ff) {
			outarg->r_fd = fd;
			req->ff = NULL;	/* done */
			switch (kind) {
			case WAKE_SEM:
				up((struct semaphore *)ev);
				break;
			case WAKE_FD:
				write((int)((long)ev), "V", 1);
				break;
			default:
				break;
			}
		} else if (fd >= 0)	/* not waiting anymore */
			unset = 1;
		spin_unlock(&fc->lock);

		if (unset)
			gfsk_fd_unset(fd);
		GFSK_CTX_UNSET_FORCE();
	}
}
static void
gfsk_client_connect_cb(struct fuse_conn *fc, struct fuse_req *req)
{
	gfsk_client_connect_proc(WAKE_SEM, fc, req);
}
static void
gfsk_client_connect_fdcb(struct fuse_conn *fc, struct fuse_req *req)
{
	gfsk_client_connect_proc(WAKE_FD, fc, req);
}

/*
 *  send connect request to user
 */
int
gfsk_req_connect_sync(int cmd, uid_t uid,
	struct gfsk_req_connect *inarg, struct gfsk_rpl_connect *outarg)
{
	int	err;
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	struct fuse_req *req;
	struct semaphore	sem;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return (PTR_ERR(req));
	req->in.h.uid = uid;
	req->in.h.opcode = cmd;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(*inarg);
	req->in.args[0].value = inarg;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(*outarg);
	req->out.args[0].value = outarg;
	req->end = gfsk_client_connect_cb;
	sema_init(&sem, 0);
	req->ff = (struct fuse_file *) &sem;
	fuse_request_send(fc, req);
	err = req->out.h.error;
	if (err != 0) {
		gflog_error(GFARM_MSG_UNFIXED, "failed: uid=%d, err=%d",
						uid, err);
	} else {
		down(&sem);
		if (outarg->r_fd < 0)
			err = -ENOMEM;
	}
	spin_lock(&fc->lock);
	req->ff = NULL;
	spin_unlock(&fc->lock);
	fuse_put_request(fc, req);
	return (err);
}

/*
 *  send connect request to user, not wait reply
 */
int
gfsk_req_connect_async(int cmd, uid_t uid,
	struct gfsk_req_connect *inarg, struct gfsk_rpl_connect *outarg,
	void **kevpp, int evfd)
{
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	struct fuse_req *req;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return (PTR_ERR(req));
	req->in.h.uid = uid;
	req->in.h.opcode = cmd;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(*inarg);
	req->in.args[0].value = inarg;
	req->out.numargs = 1;
	req->out.args[0].size = sizeof(*outarg);
	req->out.args[0].value = outarg;
	req->end = gfsk_client_connect_fdcb;
	req->ff = (struct fuse_file *)((long) evfd);
	fuse_request_send_background(fc, req);
	*kevpp = req;
	return (0);
}
/*
 * called when complete or timeout
 */
void
gfsk_req_free(void *kevp)
{
	struct fuse_req *req = (struct fuse_req *)kevp;
	if (req) {
		struct fuse_conn *fc = gfsk_fsp->gf_fc;
		spin_lock(&fc->lock);
		req->ff = NULL;
		spin_unlock(&fc->lock);
		kfree(req->in.args[0].value);
		fuse_put_request(fc, req);
	}
}

/*
 * check whether the connect request has been completed.
 * return fd when completed.
 */
int
gfsk_req_check_fd(void *kevp, int *fdp)
{
	struct fuse_req *req = (struct fuse_req *)kevp;
	int	done = 1;

	if (req) {
		struct fuse_conn *fc = gfsk_fsp->gf_fc;
		struct gfsk_rpl_connect *outarg = req->out.args[0].value;

		spin_lock(&fc->lock);
		if (req->ff) {
			done = 0;
		} else {
			if (!req->out.h.error)
				*fdp = outarg->r_fd;
			else
				*fdp = req->out.h.error;
		}
		spin_unlock(&fc->lock);
		if (done) {
			gfsk_req_free(kevp);
		}
	}
	return (done);
}
/*
 *  send terminate request to user
 */
int
gfsk_req_term(void)
{
	int	err = 0;
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	struct fuse_req *req;

	if (!fc)
		return (0);

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return (PTR_ERR(req));
	req->in.h.opcode = GFSK_OP_TERM;
	req->out.numargs = 0;
	fuse_request_send(fc, req);
	err = req->out.h.error;
	fuse_put_request(fc, req);
	return (err);
}

int
gfsk_conn_fd_set(int fd)
{
	int err = -EINVAL;
	struct file *file;

	file = fget(fd);
	if (!file)
		goto out;
	if (file->f_op != &fuse_dev_operations)
		goto err_fput;

	err = -ENODEV;
	if (!gfsk_fsp->gf_fc)
		goto err_fput;
	GFSK_CONN_FILE(file) = fuse_conn_get(gfsk_fsp->gf_fc);
	err = 0;
err_fput:
	fput(file);
out:
	return (err);
}
/*
 * set the fuse device fd which user opened and hands at mount
 */
int
gfsk_conn_init(int fd)
{
	int err = -EINVAL;
	struct fuse_conn *fc;
	struct file *file;

	if ((err = gfsk_fd2file(fd, &file))) {
		gfsk_fsp->gf_fc = NULL;
		goto out;
	}
	if (file->f_op != &fuse_dev_operations) {
		goto out_fput;
	}
	fc = kmalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc) {
		err = -ENOMEM;
		goto out_fput;
	}
	gfsk_fsp->gf_fc = fc;

	fuse_conn_init(fc);
	fc->sb = gfsk_fsp->gf_sb;
	fc->release = gfsk_free_conn;
	fc->user_id = current_uid();
	fc->group_id = current_gid();
	fc->connected = 1;
	fc->blocked = 0;
	GFSK_CONN_FILE(file) = fuse_conn_get(fc);
	err = 0;
out_fput:
	fput(file);
out:
	return (err);
}
void
gfsk_conn_umount(int fd)
{
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	struct file *file;
	if (!fc)
		return;
	/* TODO: XXXX blocked gfsk_req_term(); */
	fuse_abort_conn(fc);
	if (!gfsk_fd2file(fd, &file)) {
		gfsk_fsp->gf_fc = NULL;
		if (GFSK_CONN_FILE(file) == (void *) fc) {
			GFSK_CONN_FILE(file) = NULL;
			fuse_conn_put(fc);
		}
		fput(file);
	}
}
void
gfsk_conn_fini(void)
{
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	if (!fc)
		return;

	gfsk_fsp->gf_fc = NULL;
	fc->sb = NULL;
	fuse_conn_put(fc);
}

static struct miscdevice gfsk_miscdevice = {
	.minor = 0,
	.name  = "gfarm",
	.fops = &fuse_dev_operations,
};

int __init
gfsk_dev_init(void)
{
	int err = -ENOMEM;
	err = misc_register(&gfsk_miscdevice);
	if (err)
		goto out;
	err = 0;
 out:
	return (err);
}

void
gfsk_dev_fini(void)
{
	misc_deregister(&gfsk_miscdevice);
}
MODULE_LICENSE("GPL");

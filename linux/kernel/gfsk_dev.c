#include <linux/init.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/miscdevice.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/slab.h>
#include "gfsk.h"
#include "gfsk_if.h"
#include "gfsk_fs.h"
#include "gfsk_devif.h"
#include <gfarm/gflog.h>
#include <unistd.h>

#define GFSK_CONN_FILE(file)	((file)->private_data)

static void
gfsk_free_conn(struct gfskdev_conn *dc)
{
	kfree(dc);
}

#define WAKE_SEM	1
#define WAKE_FD	2
static void
gfsk_client_connect_proc(int kind, struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	struct gfsk_rpl_connect *outarg = req->out.args[0].value;
	void *ev = req->end_arg;

	if (ev) {
		int fd = -1;
		int unset = 0;
		GFSK_CTX_DECLARE_SB(dc->sb);

		GFSK_CTX_SET_FORCE();

		spin_lock(&dc->lock);
		if (req->end_arg) {
			if (!req->out.h.error)
				fd = outarg->r_fd;
			else
				fd = req->out.h.error;
		}
		spin_unlock(&dc->lock);

		if (fd >= 0)
			fd = gfsk_fd_set(fd, S_IFSOCK);

		spin_lock(&dc->lock);
		if (req->end_arg) {
			outarg->r_fd = fd;
			req->end_arg = NULL;	/* done */
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
		spin_unlock(&dc->lock);

		if (unset)
			gfsk_fd_unset(fd);
		GFSK_CTX_UNSET_FORCE();
	}
}
static void
gfsk_client_connect_cb(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	gfsk_client_connect_proc(WAKE_SEM, dc, req);
}
static void
gfsk_client_connect_fdcb(struct gfskdev_conn *dc, struct gfskdev_req *req)
{
	gfsk_client_connect_proc(WAKE_FD, dc, req);
}

/*
 *  send connect request to user
 */
int
gfsk_req_connect_sync(int cmd, uid_t uid,
	struct gfsk_req_connect *inarg, struct gfsk_rpl_connect *outarg)
{
	int	err;
	struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
	struct gfskdev_req *req;
	struct semaphore	sem;

	req = gfskdev_get_req(dc);
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
	req->end_arg = &sem;
	gfskdev_request_send(dc, req);
	err = req->out.h.error;
	if (err != 0) {
		gflog_error(GFARM_MSG_1004900, "%s:failed: uid=%d, err=%d",
						__func__, uid, err);
	} else {
		down(&sem);
		if (outarg->r_fd < 0)
			err = -ENOMEM;
	}
	spin_lock(&dc->lock);
	req->end_arg = NULL;
	spin_unlock(&dc->lock);
	gfskdev_put_request(dc, req);
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
	struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
	struct gfskdev_req *req;

	req = gfskdev_get_req(dc);
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
	req->end_arg = (void*)((long)evfd);
	gfskdev_request_send_background(dc, req);
	*kevpp = req;
	return (0);
}
/*
 * called when complete or timeout
 */
void
gfsk_req_free(void *kevp)
{
	struct gfskdev_req *req = (struct gfskdev_req *)kevp;
	if (req) {
		struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
		spin_lock(&dc->lock);
		req->end_arg = NULL;
		spin_unlock(&dc->lock);
		kfree(req->in.args[0].value);
		gfskdev_put_request(dc, req);
	}
}

/*
 * check whether the connect request has been completed.
 * return fd when completed.
 */
int
gfsk_req_check_fd(void *kevp, int *fdp)
{
	struct gfskdev_req *req = (struct gfskdev_req *)kevp;
	int	done = 1;

	if (req) {
		struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
		struct gfsk_rpl_connect *outarg = req->out.args[0].value;

		spin_lock(&dc->lock);
		if (req->end_arg) {
			done = 0;
		} else {
			if (!req->out.h.error)
				*fdp = outarg->r_fd;
			else
				*fdp = req->out.h.error;
		}
		spin_unlock(&dc->lock);
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
	struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
	struct gfskdev_req *req;

	if (!dc)
		return (0);

	req = gfskdev_get_req(dc);
	if (IS_ERR(req))
		return (PTR_ERR(req));
	req->in.h.opcode = GFSK_OP_TERM;
	req->out.numargs = 0;
	gfskdev_request_send(dc, req);
	err = req->out.h.error;
	gfskdev_put_request(dc, req);
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
	if (file->f_op != &gfskdev_operations)
		goto err_fput;

	err = -ENODEV;
	if (!gfsk_fsp->gf_dc)
		goto err_fput;
	GFSK_CONN_FILE(file) = gfskdev_conn_get(gfsk_fsp->gf_dc);
	err = 0;
err_fput:
	fput(file);
out:
	return (err);
}
/*
 * set the gfskdev device fd which user opened and hands at mount
 */
int
gfsk_conn_init(int fd)
{
	int err = -EINVAL;
	struct gfskdev_conn *dc;
	struct file *file;

	if ((err = gfsk_fd2file(fd, &file))) {
		gfsk_fsp->gf_dc = NULL;
		goto out;
	}
	if (file->f_op != &gfskdev_operations) {
		goto out_fput;
	}
	dc = gfskdev_conn_alloc();
	if (!dc) {
		err = -ENOMEM;
		goto out_fput;
	}
	gfsk_fsp->gf_dc = dc;

	dc->sb = gfsk_fsp->gf_sb;
	dc->release = gfsk_free_conn;
	dc->user_id = current_uid();
	dc->group_id = current_gid();
	dc->connected = 1;
	dc->blocked = 0;
	GFSK_CONN_FILE(file) = gfskdev_conn_get(dc);
	err = 0;
out_fput:
	fput(file);
out:
	return (err);
}
void
gfsk_conn_umount(int fd)
{
	struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
	struct file *file;
	if (!dc)
		return;
	/* TODO: XXXX blocked gfsk_req_term(); */
	gfskdev_abort_conn(dc);
	if (!gfsk_fd2file(fd, &file)) {
		gfsk_fsp->gf_dc = NULL;
		if (GFSK_CONN_FILE(file) == (void *) dc) {
			GFSK_CONN_FILE(file) = NULL;
			gfskdev_conn_put(dc);
		}
		fput(file);
	}
}
void
gfsk_conn_fini(void)
{
	struct gfskdev_conn *dc = gfsk_fsp->gf_dc;
	if (!dc)
		return;

	gfsk_fsp->gf_dc = NULL;
	dc->sb = NULL;
	gfskdev_conn_put(dc);
}

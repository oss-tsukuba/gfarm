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

#define GFSK_CONN_FILE(file)	((file)->private_data)

static void
gfsk_free_conn(struct fuse_conn *fc)
{
	kfree(fc);
}
static void
gfsk_client_connect_cb(struct fuse_conn *fc, struct fuse_req *req)
{
	struct gfsk_rpl_connect *outarg = req->out.args[0].value;
	struct semaphore *sem = (struct semaphore *) req->ff;

	if (!req->out.h.error && sem) {
		int fd;
		GFSK_CTX_DECLARE_SB(fc->sb);

		GFSK_CTX_SET_FORCE();
		fd = gfsk_fd_set(outarg->r_fd, S_IFSOCK);
		GFSK_CTX_UNSET_FORCE();
		outarg->r_fd = fd;
		up(sem);
	}
}

int
gfsk_req_connectmd(uid_t uid, struct gfsk_req_connect *inarg,
			struct gfsk_rpl_connect *outarg)
{
	int	err;
	struct fuse_conn *fc = gfsk_fsp->gf_fc;
	struct fuse_req *req;
	struct semaphore	sem;

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return (PTR_ERR(req));
	req->in.h.uid = uid;
	req->in.h.opcode = GFSK_OP_CONNECTMD;
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
		gflog_error(GFARM_MSG_UNFIXED, "connectmd failed: uid=%d, err=%d", uid, err);
	} else {
		down(&sem);
		if (outarg->r_fd < 0)
			err = -ENOMEM;
	}
	req->ff = NULL;
	fuse_put_request(fc, req);
	return (err);
}

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

#ifndef _FS_GFSK_DEVIF_H
#define _FS_GFSK_DEVIF_H

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/poll.h>
#include "gfsk_if.h"

extern const struct file_operations gfskdev_operations;

struct gfskdev_in_arg {
	unsigned size;
	const void *value;
};
struct gfskdev_in {
	struct gfskdev_in_header h;
	unsigned numargs;
	struct gfskdev_in_arg args[3];
};

struct gfskdev_arg {
	unsigned size;
	void *value;
};
struct gfskdev_out {
	struct gfskdev_out_header h;
	unsigned numargs;
	struct gfskdev_arg args[3];
};

/** The request state */
enum gfskdev_req_state {
	GFSKDEV_REQ_INIT = 0,
	GFSKDEV_REQ_PENDING,
	GFSKDEV_REQ_READING,
	GFSKDEV_REQ_SENT,
	GFSKDEV_REQ_WRITING,
	GFSKDEV_REQ_FINISHED
};

struct gfskdev_conn;
struct gfskdev_req {
	struct list_head list;
	atomic_t count;
	unsigned isreply:1;
	unsigned aborted:1;
	unsigned background:1;
	unsigned locked:1;
	unsigned waiting:1;
	enum gfskdev_req_state state;
	struct gfskdev_in in;
	struct gfskdev_out out;
	wait_queue_head_t waitq;
	void *end_arg;
	void (*end)(struct gfskdev_conn *, struct gfskdev_req *);
};

struct gfskdev_conn {
	spinlock_t lock;
	atomic_t count;
	uid_t user_id;
	gid_t group_id;
	unsigned flags;
	wait_queue_head_t waitq;
	struct list_head pending;
	struct list_head processing;
	struct list_head io;
	int blocked;
	wait_queue_head_t blocked_waitq;
	u64 reqctr;
	unsigned connected;
	unsigned conn_error:1;
	unsigned conn_init:1;
	atomic_t num_waiting;
	struct list_head entry;
	struct fasync_struct *fasync;
	void (*release)(struct gfskdev_conn *);
	struct super_block *sb;
};

int gfskdev_init(void);
void gfskdev_fini(void);
struct gfskdev_req *gfskdev_request_alloc(void);
void gfskdev_request_free(struct gfskdev_req *req);
struct gfskdev_req *gfskdev_get_req(struct gfskdev_conn *fc);
void gfskdev_put_request(struct gfskdev_conn *fc, struct gfskdev_req *req);
void gfskdev_request_send(struct gfskdev_conn *fc, struct gfskdev_req *req);
void gfskdev_request_send_background(struct gfskdev_conn *fc,
			struct gfskdev_req *req);
struct gfskdev_conn *gfskdev_conn_alloc(void);
void gfskdev_abort_conn(struct gfskdev_conn *fc);
struct gfskdev_conn *gfskdev_conn_get(struct gfskdev_conn *fc);
void gfskdev_conn_kill(struct gfskdev_conn *fc);
void gfskdev_conn_init(struct gfskdev_conn *fc);
void gfskdev_conn_put(struct gfskdev_conn *fc);

#endif /* _FS_GFSK_DEVIF_H */

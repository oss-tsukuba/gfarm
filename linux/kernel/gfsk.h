#ifndef _GFSK_H_
#define _GFSK_H_
#include <linux/sched.h>
struct gfarm_context;
struct gfsk_fs_context;

#define GFARM_MAGIC 	0x12345678 /* TODO: dummy value */
#define GFSK_USERNAME_MAX	64

struct  gfsk_task_context {
	struct gfarm_context *gk_gfarm_ctxp;
	struct gfsk_fs_context *gk_fs_ctxp;
	char	gk_uname[GFSK_USERNAME_MAX];
	int gk_errno;
};
#define gfsk_task_ctxp ((struct gfsk_task_context *)(current->journal_info))
#define gfsk_fsp (gfsk_task_ctxp->gk_fs_ctxp)

int gfsk_client_connect(const char *hostname, int port, const char *source_ip,
	const char *user, int *sock);

#endif /* _GFSK_H_ */

#ifndef _ERRNO_H_
#define _ERRNO_H_
#include <linux/errno.h>
#include "gfsk.h"
#ifndef __arch_um__
#define errno (gfsk_task_ctxp->gk_errno)
#else
#define kernel_errno (gfsk_task_ctxp->gk_errno)
#endif
#define gkfs_syscall_ret(ret) \
	do { if ((ret) < 0) { errno = -ret; ret = -1; } } while (0)
#endif /* _ERRNO_H_ */


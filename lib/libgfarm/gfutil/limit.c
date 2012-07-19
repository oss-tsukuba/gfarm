#include <gfarm/gfarm_config.h>

#include <errno.h>
#include <sys/types.h>
#ifdef HAVE_SETRLIMIT
#include <sys/time.h>
#include <sys/resource.h>
#endif

#ifdef __linux__
#include <stdio.h>

#define SYSTEM_FS_FILE_MAX	"/proc/sys/fs/file-max"
#define	SYSTEM_RESERVE_RATE	0.25
#endif

#include <gfarm/gflog.h>
#include "gfutil.h"

#ifdef __KERNEL__
#undef HAVE_SETRLIMIT
#endif /* __KERNEL__ */

/*
 * - Set file descriptor limit to min(*file_table_size_p, hard_limit).
 * - Return file descriptor limit to *file_table_size_p.
 */
int
gfarm_limit_nofiles(int *file_table_size_p)
{
#ifndef HAVE_SETRLIMIT
	return (EOPNOTSUPP);
#else
	struct rlimit limit;
	int save_errno, want = *file_table_size_p;
#ifdef __linux__
	FILE *fp;
	unsigned long file_max;
#endif

	if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
		save_errno = errno;
		gflog_warning_errno(GFARM_MSG_1000001, "getrlimit");
		return (save_errno);
	}
#ifdef __linux__
	/*
	 * On Linux,
	 * default hard limit is often far smaller than the system limit,
	 * thus read the system limit and use that as hard limit.
	 */
	if ((fp = fopen(SYSTEM_FS_FILE_MAX, "r")) == NULL) {
		gflog_warning_errno(GFARM_MSG_UNFIXED, "%s",
		    SYSTEM_FS_FILE_MAX);
	} else {
		if (fscanf(fp, "%lu", &file_max) != 1) {
			gflog_warning(GFARM_MSG_UNFIXED, "%s: cannot parse",
			    SYSTEM_FS_FILE_MAX);
		} else {
			limit.rlim_max = file_max * (1 - SYSTEM_RESERVE_RATE);
		}
		fclose(fp);
	}
#endif
	if (limit.rlim_max != RLIM_INFINITY && want > limit.rlim_max)
		want = limit.rlim_max;
	if (limit.rlim_cur != want) {
		limit.rlim_cur = want;
		if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
			save_errno = errno;
			gflog_warning_errno(GFARM_MSG_1000002, "setrlimit");
			return (save_errno);
		}
	}
	*file_table_size_p = want;
	return (0);
#endif
}

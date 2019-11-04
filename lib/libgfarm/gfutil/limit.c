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

#ifdef __KERNEL__	/* HAVE_SETRLIMIT */
#undef HAVE_SETRLIMIT
#endif /* __KERNEL__ */

/*
 * - Set file descriptor limit to min(*file_table_size_p, hard_limit).
 * - Return file descriptor limit to *file_table_size_p.
 *
 * NOTE: even if setrlimit() fails, this function returns SUCCESS (== 0),
 *	because *file_table_sizep will be correctly updated in that case too.
 */
int
gfarm_limit_nofiles(int *file_table_size_p)
{
#ifndef HAVE_SETRLIMIT
	return (EOPNOTSUPP);
#else
	struct rlimit old, new;
	int save_errno, want = *file_table_size_p;
#ifdef __linux__
	FILE *fp;
	unsigned long file_max;
#endif

	if (getrlimit(RLIMIT_NOFILE, &old) == -1) {
		save_errno = errno;
		gflog_warning_errno(GFARM_MSG_1000001, "getrlimit");
		return (save_errno);
	}
	new = old;

#ifdef __linux__
	/*
	 * On Linux,
	 * default hard limit is often far smaller than the system limit,
	 * thus read the system limit and use that as hard limit.
	 */
	if ((fp = fopen(SYSTEM_FS_FILE_MAX, "r")) == NULL) {
		gflog_warning_errno(GFARM_MSG_1003436, "%s",
		    SYSTEM_FS_FILE_MAX);
	} else {
		if (fscanf(fp, "%lu", &file_max) != 1) {
			gflog_warning(GFARM_MSG_1003437, "%s: cannot parse",
			    SYSTEM_FS_FILE_MAX);
		} else {
			file_max *= (1 - SYSTEM_RESERVE_RATE);
			/*
			 * fs.file-max may be really large
			 * (e.g. 7338458 on a machine with large memory),
			 * and if such large value is set to new.rlim_max,
			 * setrlimit(2) fails with EPERM.
			 */
			new.rlim_max = file_max < want ? file_max : want;
		}
		fclose(fp);
	}
#endif
	/* `want' will be new.rlim_cur, thus it shouldn't exceed new.rlim_max */
	if (new.rlim_max != RLIM_INFINITY && want > new.rlim_max)
		want = new.rlim_max;
	if (new.rlim_cur != want) {
		new.rlim_cur = want;
		if (setrlimit(RLIMIT_NOFILE, &new) == -1) {
			gflog_notice_errno(GFARM_MSG_1003438,
			    "setrlimit(RLIMIT_NOFILE) "
			    "from soft=%llu/hard=%llu "
			    "to soft=%llu/hard=%llu",
			    (long long)old.rlim_cur, (long long)old.rlim_max,
			    (long long)new.rlim_cur, (long long)new.rlim_max);
			/* log notice, but returns SUCCESS */
			*file_table_size_p = old.rlim_cur;
			return (0);
		}
	}
	*file_table_size_p = new.rlim_cur;
	return (0);
#endif
}

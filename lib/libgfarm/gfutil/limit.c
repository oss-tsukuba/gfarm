#include <gfarm/gfarm_config.h>
#include <sys/types.h>
#ifdef HAVE_SETRLIMIT
#include <sys/time.h>
#include <sys/resource.h>
#endif
#include "gfutil.h"

/*
 * - Unlimit file descriptors.
 * - Returns numbers of file descriptors to *file_table_size_p,
 *   if it is available, otherwise do not touch that.
 */
void
gfarm_unlimit_nofiles(int *file_table_size_p)
{
#ifdef HAVE_SETRLIMIT
	struct rlimit limit;

	if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
		gflog_warning_errno("getrlimit");
		return;
	}
	if (limit.rlim_cur != limit.rlim_max) {
		/* do not use rlim_t here, because rlim_t is not portable */
		struct rlimit save_current = limit;

		limit.rlim_cur = limit.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
			limit = save_current;
			gflog_warning_errno("setrlimit");
		}
	}
	if (limit.rlim_cur != RLIM_INFINITY)
		*file_table_size_p = limit.rlim_cur;
#endif
}

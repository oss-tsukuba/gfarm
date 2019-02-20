/*
 * $Id$
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <gfarm/gfarm.h>

#include "timer.h"

#include "context.h"
#include "gfs_profile.h"

void
gfs_profile_set(void)
{
	gfarm_ctxp->profile = 1;
	gfarm_timerval_calibrate();
}

void
gfs_profile_unset(void)
{
	gfarm_ctxp->profile = 0;
}

void
gfs_profile_display_timers(int n, struct gfs_profile_list list[], void *sp)
{
	int i;

	for (i = 0; i < n; ++i) {
		char *format = list[i].format;
		void *value = (char *)sp + list[i].offset;

		switch (list[i].type) {
		case 'd':
			gflog_info(GFARM_MSG_1005054, format, *(double *)value);
			break;
		case 'l':
			gflog_info(GFARM_MSG_1005055, format,
				   *(unsigned long long *)value);
			break;
		}
	}
}

gfarm_error_t
gfs_profile_value(const char *name, int n,
	struct gfs_profile_list list[], void *sp,
	char *value, size_t *sizep)
{
	int size, i;

	if (name == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);

	/* XXX - use binary search */
	for (i = 0; i < n; ++i) {
		char *profile_name = list[i].name;
		char *format = list[i].format_value;
		void *v = (char *)sp + list[i].offset;

		if (strcmp(name, profile_name) != 0)
			continue;
		switch (list[i].type) {
		case 'd':
			/* XXX - snprintf wastes one byte for '\0' */
			size = snprintf(value, *sizep, format, *(double *)v);
			break;
		case 'l':
			size = snprintf(value, *sizep, format,
				*(unsigned long long *)v);
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		*sizep = size;
		return (GFARM_ERR_NO_ERROR);
	}
	return (GFARM_ERR_NO_SUCH_OBJECT);
}

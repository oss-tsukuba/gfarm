#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>

#include "liberror.h"

size_t
gfarm_humanize_number(char *buf, size_t len, unsigned long long number,
	int flags)
{
	unsigned int divisor = (flags & GFARM_HUMANIZE_BINARY) ? 1024 : 1000;
	double n = number;
	unsigned long long i = number;
	int scale = 0;
	static unsigned char units[] = { '\0', 'K', 'M', 'G', 'T', 'P', 'E' };

	while (n >= 999.5 && scale < GFARM_ARRAY_LENGTH(units)) {
		n /= divisor;
		i /= divisor;
		scale++;
	}
	if (scale == 0)
		return (snprintf(buf, len, "%llu", number));

	if (n < 99.5 && n != i)
		return (snprintf(buf, len, "%.1f%c", n, units[scale]));
	else
		return (snprintf(buf, len, "%.0f%c", n, units[scale]));
}

size_t
gfarm_humanize_signed_number(char *buf, size_t len, long long number,
	int flags)
{
	if (number < 0) {
		if (len >= 1 + 1) { /* '-' + '\0' */
			*buf++ = '-';
			--len;
		}
		return (gfarm_humanize_number(buf, len, -number, flags) + 1);
	} else {
		return (gfarm_humanize_number(buf, len, number, flags));
	}
}

gfarm_error_t
gfarm_humanize_number_to_int64(gfarm_int64_t *vp, const char *str)
{
	char *ep;

	*vp = gfarm_strtoi64(str, &ep);
	if (errno != 0) {
		int save_errno = errno;

		gflog_debug(GFARM_MSG_1000966,
		    "conversion to int64 failed (%s): %s",
		    str, strerror(save_errno));
		return (gfarm_errno_to_error(save_errno));
	}
	if (ep == str) {
		gflog_debug(GFARM_MSG_1000967,
		    "Integer expected but (%s)", str);
		return (GFARM_ERRMSG_INTEGER_EXPECTED);
	}
	if (*ep != '\0') {
		switch (*ep) {
		case 't':
		case 'T':
			*vp *= 1024;
			/* FALLTHROUGH */
		case 'g':
		case 'G':
			*vp *= 1024;
			/* FALLTHROUGH */
		case 'm':
		case 'M':
			*vp *= 1024;
			/* FALLTHROUGH */
		case 'k':
		case 'K':
			*vp *= 1024;
			ep++;
			break;
		}
		if (*ep != '\0') {
			gflog_debug(GFARM_MSG_1000968,
			    "Invalid character found (%s)", str);
			return (GFARM_ERRMSG_INVALID_CHARACTER);
		}
	}
	gflog_debug(GFARM_MSG_1003707, "%s = %lld", str, (long long)*vp);

	return (GFARM_ERR_NO_ERROR);
}

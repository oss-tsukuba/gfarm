#include <stdio.h>

#include <gfarm/gfarm.h>

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

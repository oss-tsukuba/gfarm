/*
 * $Id$
 */


#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef sun
#include <stdarg.h>
#endif

#include "gfperf-lib.h"

#ifdef sun
int
asprintf(char **strp, const char *fmt, ...)
{
	int ret, size;
	char *bufp;
	va_list ap;
	va_start(ap, fmt);
	size = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (size < 0)
		return (size);
	GFARM_MALLOC_ARRAY(bufp, size+1);
	if (bufp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	va_start(ap, fmt);
	ret = vsnprintf(bufp, size+1, fmt, ap);
	va_end(ap);
	*strp = bufp;
	return (ret);
}

time_t
timegm(struct tm *tm)
{
	time_t time_local, time_gmt;
	struct tm *tm_gmt;

	time_local = mktime(tm);
	if (time_local == -1) {
		tm->tm_hour--;
		time_local = mktime(tm);
		if (time_local == -1)
			return (-1);
		time_local += 3600;
	}
	tm_gmt = gmtime(&time_local);
	tm_gmt->tm_isdst = 0;
	time_gmt = mktime(tm_gmt);
	if (time_gmt == -1) {
		tm_gmt->tm_hour--;
		time_gmt = mktime(tm_gmt);
		if (time_gmt == -1)
			return (-1);
		time_gmt += 3600;
	}
	return (time_local - (time_gmt - time_local));
}
#endif

float gfperf_timeval_to_float(struct timeval *a)
{
	return (((float)a->tv_sec) + ((float)a->tv_usec)/1000000);
}

long long
gfperf_strtonum(const char *str)
{
	long long tmp = 0;
	const char *p = str;
	char c;

	while (*p != '\0') {
		c = *p++;
		if (c >= '0' && c <= '9') {
			tmp *= 10;
			tmp += c - '0';
		} else {
			switch (c) {
			case 'k':
			case 'K':
				tmp *= 1<<10;
			break;
			case 'm':
			case 'M':
				tmp *= 1<<20;
			break;
			case 'g':
			case 'G':
				tmp *= 1<<30;
			break;
			case 't':
			case 'T':
				tmp *= (long long)1<<40;
			break;
			case 'p':
			case 'P':
				tmp *= (long long)1<<50;
			break;
			case 'e':
			case 'E':
				tmp *= (long long)1<<60;
			break;
			}
		}
	}
	return (tmp);
}

void
gfperf_sub_timeval(const struct timeval *a, const struct timeval *b,
		   struct timeval *c)
{

	if (a->tv_usec < b->tv_usec) {
		c->tv_usec = 1000000+a->tv_usec-b->tv_usec;
		c->tv_sec = a->tv_sec-1-b->tv_sec;
	} else {
		c->tv_usec = a->tv_usec-b->tv_usec;
		c->tv_sec = a->tv_sec-b->tv_sec;
	}
}

gfarm_error_t
gfperf_is_dir_posix(char *path)
{
	struct stat sb;
	int e;

	e = stat(path, &sb);
	if (e != 0)
		return (GFARM_ERR_NOT_A_DIRECTORY);

	if (S_ISDIR(sb.st_mode))
		return (GFARM_ERR_NO_ERROR);
	else
		return (GFARM_ERR_NOT_A_DIRECTORY);
}

gfarm_error_t
gfperf_is_dir_gfarm(char *path)
{
	struct gfs_stat sb;
	int e;

	e = gfs_stat(path, &sb);

	if (e != GFARM_ERR_NO_ERROR)
		return (GFARM_ERR_NOT_A_DIRECTORY);

	if (GFARM_S_ISDIR(sb.st_mode)) {
		gfs_stat_free(&sb);
		return (GFARM_ERR_NO_ERROR);
	} else {
		gfs_stat_free(&sb);
		return (GFARM_ERR_NOT_A_DIRECTORY);
	}
}

const char *
gfperf_find_root_from_url(const char *url)
{
	int i = 0;
	const char *p = url;
	if (*p == '/')
		return (url);
	while (*p != '\0') {
		if (*p == '/') {
			i++;
			if (i >= 3)
				return (p);
		}
		p++;
	}
	return (NULL);
}

int gfperf_is_file_url(const char *url)
{
	return (strncmp(url, GFPERF_FILE_URL_PREFIX,
			GFPERF_FILE_URL_PREFIX_LEN) == 0);
}

int gfperf_parse_utc_time_string(const char *s, time_t *ret)
{
	char *r;
	time_t t;
	struct tm tm;
	static char *fmt = "%Y-%m-%dT%H:%M:%SZ";

	r = strptime(s, fmt, &tm);
	if (r == NULL)
		return (-1);

	t = timegm(&tm);

	*ret = t;

	return (0);
}

int gfperf_is_file_exist_posix(const char *filename)
{
	struct stat buf;
	int r;

	r = stat(filename, &buf);
	if (r < 0)
		return (0);

	return (1);
}

int gfperf_is_file_exist_gfarm(const char *filename)
{
	struct gfs_stat buf;
	int r;

	r = gfs_stat(filename, &buf);
	if (r != GFARM_ERR_NO_ERROR)
		return (0);

	gfs_stat_free(&buf);
	return (1);
}

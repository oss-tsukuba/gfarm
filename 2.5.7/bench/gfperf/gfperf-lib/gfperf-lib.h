/*
 * $Id$
 */

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <gfarm/gfarm.h>
#include <time.h>

#ifndef _GFPERF_LIB_H_
#define _GFPERF_LIB_H_

#define GFPERF_COPY_BUF_SIZE (4*1024*1024)
#define GFPERF_FILE_URL_PREFIX "file://"
#define GFPERF_FILE_URL_PREFIX_LEN 7

#ifdef sun
int asprintf(char **strp, const char *fmt, ...);
#endif

float gfperf_timeval_to_float(struct timeval *a);

gfarm_error_t
gfperf_create_file_on_local(const char *filename, long long file_size);
gfarm_error_t
gfperf_create_file_on_gfarm(const char *url, char *hostname,
				   long long file_size);

long long gfperf_strtonum(const char *str);
void gfperf_sub_timeval(const struct timeval *a, const struct timeval *b,
			struct timeval *c);
const char *gfperf_find_root_from_url(const char *url);

gfarm_error_t gfperf_is_dir_posix(char *path);
gfarm_error_t gfperf_is_dir_gfarm(char *path);
int gfperf_is_file_url(const char *url);
int gfperf_parse_utc_time_string(const char *s, time_t *ret);

int gfperf_is_file_exist_gfarm(const char *filename);
int gfperf_is_file_exist_posix(const char *filename);

#endif

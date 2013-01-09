/*
 * $Id$
 */


#include "gfperf-lib.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <string.h>

#include "gfperf-copy.h"

int direction = 0;
char *file_size_string = "1M";
long long file_size = 1024*1024;
char *buf_size_string = DEFAULT_BUF_SIZE_STRING;
size_t buf_size = DEFAULT_BUF_SIZE;

char *src_url = NULL;
char *src_filename = NULL;
char *dst_url = NULL;
char *dst_filename = NULL;
int posix_flag = 0;
char *gfsd_hostname = NULL;
char *gfarm2fs_mount_point = NULL;

static
void
usage(char *argv[])
{
	fprintf(stderr, "%s\n", argv[0]);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "\t -s, --src <src url> \n");
	fprintf(stderr, "\t -d, --dst <dst url> \n");
	fprintf(stderr, "\t [-l, --filesize <buffer size>] \n");
	fprintf(stderr, "\t [-b, --bufsize <buffer size>] \n");
	fprintf(stderr, "\t [-g, --gfsd <gfsd host>] \n");
	fprintf(stderr, "\t [-m, --gfarm2fs <gfarm2fs mount point>] \n");
#else
	fprintf(stderr, "\t -s <src url> \n");
	fprintf(stderr, "\t -d <dst url> \n");
	fprintf(stderr, "\t [-l <buffer size>] \n");
	fprintf(stderr, "\t [-b <buffer size>] \n");
	fprintf(stderr, "\t [-g <gfsd host>] \n");
	fprintf(stderr, "\t [-m <gfarm2fs mount point>] \n");
#endif
}

static
gfarm_error_t
parse_opt(int argc, char *argv[])
{
	int r, len, ret;
	pid_t pid;
	long long lltmp;
	static char *optstr = "hs:d:l:b:g:m:";
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	static struct option long_options[] = {
		{"src", 1, 0, 's'},
		{"dst", 1, 0, 'd'},
		{"filesize", 1, 0, 'l'},
		{"bufsize", 1, 0, 'b'},
		{"gfsd", 1, 0, 'g'},
		{"gfarm2fs", 1, 0, 'm'},
		{0, 0, 0, 0}
	};
#endif
	while (1) {
#ifdef HAVE_GETOPT_LONG
		r = getopt_long(argc, argv, optstr,
				long_options, &option_index);
#else
		r = getopt(argc, argv, optstr);
#endif
		if (r < 0)
			break;
		switch (r) {
		case 'h':
			return (GFARM_ERR_INVALID_ARGUMENT);
		case 's':
			src_url = optarg;
			break;
		case 'd':
			dst_url = optarg;
			break;
		case 'l':
			file_size_string = optarg;
			file_size = gfperf_strtonum(optarg);
			if (file_size < 0) {
				fprintf(stderr,
					"file size lower than zero.\n");
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			break;
		case 'b':
			buf_size_string = optarg;
			lltmp = gfperf_strtonum(optarg);
			if (lltmp < 0) {
				fprintf(stderr,
					"buffer size lower than zero.\n");
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			buf_size = (lltmp > INT_MAX) ? INT_MAX : lltmp;
			break;
		case 'g':
			gfsd_hostname = strdup(optarg);
			break;
		case 'm':
			gfarm2fs_mount_point = optarg;
			posix_flag = 1;
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	if (src_url == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);

	if (dst_url == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);

	if (gfperf_is_file_url(src_url)) {
		if (gfperf_is_file_url(dst_url)) {
			fprintf(stderr,
				"either src or dst must be gfarm url\n");
			return (GFARM_ERR_INVALID_ARGUMENT);
		} else {
			direction = LOCAL_TO_GFARM;
			src_url = &src_url[GFPERF_FILE_URL_PREFIX_LEN];
		}
	} else {
		if (gfperf_is_file_url(dst_url)) {
			direction = GFARM_TO_LOCAL;
			dst_url = &dst_url[GFPERF_FILE_URL_PREFIX_LEN];
		} else {
			fprintf(stderr,
				"either src or dst must be file url\n");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	pid = getpid();

	len = strlen(src_url);
	if (src_url[len-1] == '/')
		ret = asprintf(&src_filename, "%scopy-%s%s%s.tst",
			       src_url, file_size_string,
			       gfsd_hostname ? "-" : "",
			       gfsd_hostname ? gfsd_hostname : "");
	else
		ret = asprintf(&src_filename, "%s/copy-%s%s%s.tst",
			       src_url,  file_size_string,
			       gfsd_hostname ? "-" : "",
			       gfsd_hostname ? gfsd_hostname : "");
	if (ret < 0) {
		fprintf(stderr, "can not allocate memory.\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	len = strlen(dst_url);
	if (dst_url[len-1] == '/')
		ret = asprintf(&dst_filename, "%stest.%d.tst",
			       dst_url, pid);
	else
		ret = asprintf(&dst_filename, "%s/test.%d.tst",
			       dst_url, pid);
	if (ret < 0) {
		free(src_filename);
		src_filename = NULL;
		fprintf(stderr, "can not allocate memory.\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	e = parse_opt(argc, argv);
	if (e != GFARM_ERR_NO_ERROR) {
		usage(argv);
		gfarm_terminate();
		return (1);
	}

	if (posix_flag)
		do_posix_test();
	else
		do_libgfarm_test();

	if (gfsd_hostname)
		free(gfsd_hostname);
	free(src_filename);
	free(dst_filename);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	return (0);
}

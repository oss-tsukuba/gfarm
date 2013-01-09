/*
 * $Id$
 */


#include "gfperf-lib.h"
#include <sys/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <string.h>

#include "gfperf-replica.h"

#define PARALLEL_COMMAND "gfperf-parallel-replica"
#define MAX_WAIT_SEC 10

char *from_gfsd_name = NULL;
char *to_gfsd_name = NULL;
char *testdir = "/tmp";
char *testdir_filename = NULL;
long long file_size = 1024*1024;
char *file_size_string = "1M";
int parallel_flag = 0;
char *wait_time = NULL;
char *group_name = "unknown";

static
void
usage(char *argv[])
{
	fprintf(stderr, "%s\n", argv[0]);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "\t -s, --src <gfsd name> \n");
	fprintf(stderr, "\t -d, --dst <gfsd name> \n");
	fprintf(stderr, "\t [-l, --filesize <file size>] \n");
	fprintf(stderr, "\t [-t, --testdir <gfarm filesystem directory>] \n");
#else
	fprintf(stderr, "\t -s <gfsd name> \n");
	fprintf(stderr, "\t -d <gfsd name> \n");
	fprintf(stderr, "\t [-l <file size>] \n");
	fprintf(stderr, "\t [-t <gfarm filesystem directory>] \n");
#endif
	if (parallel_flag) {
#ifdef HAVE_GETOPT_LONG
		fprintf(stderr, "\t [-n, --name <group name>] \n");
		fprintf(stderr, "\t [-w, --wait <wait time in UTC>] \n");
#else
		fprintf(stderr, "\t [-n <group name>] \n");
		fprintf(stderr, "\t [-w <wait time in UTC>] \n");
#endif

	}

}

static
gfarm_error_t
parse_opt(int argc, char *argv[])
{
	int r, len, ret;
	pid_t pid;
	static char *optstr = "hs:d:l:t:n:w:";
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	static struct option long_options[] = {
		{"src", 1, 0, 's'},
		{"dst", 1, 0, 'd'},
		{"filesize", 1, 0, 'l'},
		{"testdir", 1, 0, 't'},
		{"name", 1, 0, 'n'},
		{"wait", 1, 0, 'w'},
		{0, 0, 0, 0}
	};
#endif
	if (strstr(argv[0], PARALLEL_COMMAND) != NULL)
		parallel_flag = 1;

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
			from_gfsd_name = optarg;
			break;
		case 'd':
			to_gfsd_name = optarg;
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
		case 't':
			testdir = optarg;
			break;
		case 'w':
			wait_time = optarg;
			break;
		case 'n':
			group_name = optarg;
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	if (from_gfsd_name == 0)
		return (GFARM_ERR_INVALID_ARGUMENT);

	if (to_gfsd_name == 0)
		return (GFARM_ERR_INVALID_ARGUMENT);

	pid = getpid();

	len = strlen(testdir);
	if (testdir[len-1] == '/')
		ret = asprintf(&testdir_filename, "%stest.%d.dat",
			       testdir, pid);
	else
		ret = asprintf(&testdir_filename, "%s/test.%d.dat",
			       testdir, pid);
	if (ret < 0) {
		fprintf(stderr, "can not allocate memory.\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int r;
	struct timespec w;
	struct timeval now, dst, diff;

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

	if (wait_time != NULL) {
		memset(&w, 0, sizeof(w));
		r = gfperf_parse_utc_time_string(wait_time, &dst.tv_sec);
		if (r < 0) {
			fprintf(stderr, "invalid time format\n");
			gfarm_terminate();
			return (1);
		}
		dst.tv_usec = 0;
		gettimeofday(&now, NULL);
		gfperf_sub_timeval(&dst, &now, &diff);
		if (diff.tv_sec > MAX_WAIT_SEC) {
			fprintf(stderr, "wait time too long!\n");
			gfarm_terminate();
			return (1);
		} else {
			w.tv_sec = diff.tv_sec;
			w.tv_nsec = diff.tv_usec * 1000;
			nanosleep(&w, NULL);
		}
	}

	do_test();

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	free(testdir_filename);
	return (0);
}

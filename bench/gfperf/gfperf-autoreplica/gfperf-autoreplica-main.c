/*
 * $Id$
 */

#include "gfperf-lib.h"
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <string.h>

#define PARALLEL_COMMAND "gfperf-parallel-autoreplica"
#define MAX_WAIT_SEC 10

struct filenames {
	int n;
	char *filename[0];
};

static char *testdir = "gfarm:///tmp";
static long long filesize = 1024*1024*1024;
static char *filesize_string = "1G";
static int posix_flag;
static int number = 10;
static char *gfarm2fs_dir = NULL;
static int replicate = 1;
static char *fullpath;
static int parallel_flag = 0;
static char *wait_time = NULL;
static char *group_name = "unknown";
static int stop_waiting = 60;

#define NCOPY_KEY "gfarm.ncopy"

static
gfarm_error_t
set_ncopy()
{
	gfarm_error_t e;
	char num_str[16];
	int num_len;

	if (replicate <= 1)
		return (GFARM_ERR_NO_ERROR);

	num_len = snprintf(num_str, sizeof(num_str), "%d", replicate);

	gfs_removexattr(testdir, NCOPY_KEY);

	e = gfs_setxattr(testdir, NCOPY_KEY, num_str, num_len,
			 GFS_XATTR_CREATE);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not set gfarm.ncopy! (%s)\n",
			gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
del_ncopy()
{
	gfarm_error_t e;

	if (replicate <= 1)
		return (GFARM_ERR_NO_ERROR);

	e = gfs_removexattr(testdir, NCOPY_KEY);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return (GFARM_ERR_NO_ERROR);
}

static
void
free_filenames(struct filenames *p)
{
	int i;
	int n = p->n;

	for (i = 0; i < n; i++)
		if (p->filename[i])
			free(p->filename[i]);
	free(p);
}

static
struct filenames *
create_filenames()
{
	int i, r;
	size_t size;
	char *tmp;
	pid_t pid;
	struct filenames *p;

	size = sizeof(struct filenames) + number*(sizeof(char *));
	GFARM_CALLOC_ARRAY(tmp, size);
	p = (struct filenames *)tmp;
	if (p == NULL)
		return (NULL);

	p->n = number;
	pid = getpid();
	for (i = 0; i < number; i++) {
		r = asprintf(&p->filename[i], "%s/testfile.%d.%d.tst",
			     fullpath, i, pid);
		if (r < 0)
			goto err_return;

	}

	return (p);
err_return:
	free_filenames(p);
	return (NULL);
}

static
void
usage(char *argv[])
{
	fprintf(stderr, "%s\n", argv[0]);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "\t [-t, --testdir <gfarm url>] \n");
	fprintf(stderr, "\t [-m, --gfarm2fs <gfarm2fs mount point>] \n");
	fprintf(stderr, "\t [-l, --filesize <file size>] \n");
	fprintf(stderr, "\t [-f, --number <number of files>] \n");
	fprintf(stderr, "\t [-r, --replica <number of replicas>] \n");
	fprintf(stderr, "\t [-s, "
		"--stop <number of seconds for stop waiting>] \n");
#else
	fprintf(stderr, "\t [-t <gfarm url>] \n");
	fprintf(stderr, "\t [-m <gfarm2fs mount point>] \n");
	fprintf(stderr, "\t [-l <file size>] \n");
	fprintf(stderr, "\t [-f <number of files>] \n");
	fprintf(stderr, "\t [-r <number of replicas>] \n");
	fprintf(stderr, "\t [-s <number of seconds for stop waiting>] \n");
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
	int r;
	static char *optstr = "ht:l:f:m:r:n:w:s:";
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	static struct option long_options[] = {
		{"testdir", 1, 0, 't'},
		{"filesize", 1, 0, 'l'},
		{"number", 1, 0, 'f'},
		{"gfarm2fs", 1, 0, 'm'},
		{"replica", 1, 0, 'r'},
		{"name", 1, 0, 'n'},
		{"wait", 1, 0, 'w'},
		{"stop", 1, 0, 's'},
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
		case 't':
			testdir = optarg;
			break;
		case 'l':
			filesize_string = optarg;
			filesize = gfperf_strtonum(filesize_string);
			if (filesize < 0) {
				fprintf(stderr, "filesize too big!\n");
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			break;
		case 'f':
			number = atoi(optarg);
			break;
		case 's':
			stop_waiting = atoi(optarg);
			break;
		case 'r':
			replicate = atoi(optarg);
			break;
		case 'm':
			gfarm2fs_dir = optarg;
			break;
		case 'n':
			group_name = optarg;
			break;
		case 'w':
			wait_time = optarg;
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	if (gfarm2fs_dir) {
		posix_flag = 1;
		r = asprintf(&fullpath, "%s%s", gfarm2fs_dir,
			     gfperf_find_root_from_url(testdir));
		if (r < 0)
			return (GFARM_ERR_NO_MEMORY);
	} else {
		posix_flag = 0;
		fullpath = strdup(testdir);
		if (fullpath == NULL)
			return (GFARM_ERR_NO_MEMORY);
	}

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
dup_wait(char *path)
{
	int i, n;
	gfarm_error_t e;
	struct gfs_replica_info *ri;
	int max_wait;

	max_wait = stop_waiting * 2;

	i = 0;
	while (1) {
		e = gfs_replica_info_by_name(path, 0, &ri);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "can not get replica info!\n");
			return (e);
		}

		n = gfs_replica_info_number(ri);
		gfs_replica_info_free(ri);
		if (n >= replicate)
			break;
		if (i >= max_wait) {
			fprintf(stderr,
				"time out! replica may not be created.\n");
			break;
		}
		usleep(500000);
		i++;
	}
	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_posix(struct filenames *p)
{
	int i;
	gfarm_error_t e;
	char *gpath;
	float f;
	struct timeval start_time, end_time, exec_time;

	e = set_ncopy();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gettimeofday(&start_time, NULL);
	for (i = 0; i < p->n; i++) {
		e = gfperf_create_file_on_local(p->filename[i], filesize);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	gettimeofday(&end_time, NULL);

	for (i = 0; i < p->n; i++) {
		e = asprintf(&gpath, "%s/%s",
			     testdir, basename(p->filename[i]));
		if (e < 0) {
			fprintf(stderr, "can not allocate memory!\n");
			break;
		}
		dup_wait(gpath);
		free(gpath);
	}

	for (i = 0; i < p->n; i++)
		unlink(p->filename[i]);

	del_ncopy();

	gfperf_sub_timeval(&end_time, &start_time, &exec_time);

	f = (float)(filesize * p->n) /
		((float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000);

	if (parallel_flag)
		printf("parallel/%s/autoreplica/gfam2fs/create/%s/%d/%d = "
		       "%.02f bytes/sec %g sec\n",
		       group_name,
		       filesize_string,
		       number, replicate, f,
		       gfperf_timeval_to_float(&exec_time));
	else
		printf("autoreplica/gfam2fs/create/%s/%d/%d = "
		       "%.02f bytes/sec %g sec\n",
		       filesize_string,
		       number, replicate, f,
		       gfperf_timeval_to_float(&exec_time));


	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_gfarm(struct filenames *p)
{
	int i;
	gfarm_error_t e;
	float f;
	struct timeval start_time, end_time, exec_time;

	e = set_ncopy();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gettimeofday(&start_time, NULL);
	for (i = 0; i < p->n; i++) {
		e = gfperf_create_file_on_gfarm(p->filename[i], NULL,
						filesize);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}
	gettimeofday(&end_time, NULL);

	for (i = 0; i < p->n; i++)
		dup_wait(p->filename[i]);

	for (i = 0; i < p->n; i++)
		gfs_unlink(p->filename[i]);

	del_ncopy();
	gfperf_sub_timeval(&end_time, &start_time, &exec_time);

	f = (float)(filesize * p->n) /
		((float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000);

	if (parallel_flag)
		printf("parallel/%s/autoreplica/libgfarm/create/%s/%d/%d = "
		       "%.02f bytes/sec %g sec\n",
		       group_name,
		       filesize_string,
		       number, replicate, f,
		       gfperf_timeval_to_float(&exec_time));
	else
		printf("autoreplica/libgfarm/create/%s/%d/%d = "
		       "%.02f bytes/sec %g sec\n",
		       filesize_string,
		       number, replicate, f,
		       gfperf_timeval_to_float(&exec_time));

	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int r;
	struct timespec w;
	struct timeval now, dst, diff;
	struct filenames *filenames;

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
		e = gfperf_is_dir_posix(fullpath);
	else
		e = gfperf_is_dir_gfarm(fullpath);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s is not a directory.\n",
			fullpath);
		free(fullpath);
		gfarm_terminate();
		return (1);
	}

	filenames = create_filenames();
	if (filenames == NULL) {
		fprintf(stderr, "can not allocate memory!\n");
		free(fullpath);
		gfarm_terminate();
		return (1);
	}

	if (wait_time != NULL) {
		memset(&w, 0, sizeof(w));
		r = gfperf_parse_utc_time_string(wait_time, &dst.tv_sec);
		if (r < 0) {
			fprintf(stderr, "invalid time format\n");
			free_filenames(filenames);
			free(fullpath);
			gfarm_terminate();
			return (1);
		}
		dst.tv_usec = 0;
		gettimeofday(&now, NULL);
		gfperf_sub_timeval(&dst, &now, &diff);
		if (diff.tv_sec > MAX_WAIT_SEC) {
			fprintf(stderr, "wait time too long!\n");
			free_filenames(filenames);
			free(fullpath);
			gfarm_terminate();
			return (1);
		} else {
			w.tv_sec = diff.tv_sec;
			w.tv_nsec = diff.tv_usec * 1000;
			nanosleep(&w, NULL);
		}
	}

	if (posix_flag)
		do_test_posix(filenames);
	else
		do_test_gfarm(filenames);

	free_filenames(filenames);
	free(fullpath);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	return (0);
}

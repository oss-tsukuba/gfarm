/*
 * $Id$
 */


#include "gfperf-lib.h"
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <string.h>
#include <signal.h>
#include <errno.h>

#define MAX_LOOP_SEC 60

static char *testdir = "gfarm:///tmp";
static long long filesize = 1024*1024;
static char *filesize_string = "1M";
static int bufsize = 4*1024;
static char *bufsize_string = "4K";
static int posix_flag;
static int random_flag = 0;
static char *gfsd_hostname = NULL;
static char hostname[1024];
static int stop_flag = 0;
static int timeout = MAX_LOOP_SEC;
static char *gfarm2fsdir = NULL;

static
void
alarm_handler(int sig)
{
	stop_flag = 1;
}

static
void
set_timer()
{
	struct itimerval new_val, old_val;
	if (timeout < 0)
		return;

	signal(SIGALRM, alarm_handler);

	memset(&new_val, 0, sizeof(new_val));

	new_val.it_interval.tv_sec = 0;
	new_val.it_value.tv_sec = timeout;

	setitimer(ITIMER_REAL, &new_val, &old_val);

}

static
void
usage(char *argv[])
{
	fprintf(stderr, "%s\n", argv[0]);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "\t [-t, --testdir <gfarm directory>] \n");
	fprintf(stderr, "\t [-m, --gfarm2fs <gfarm2fs mount point>] \n");
	fprintf(stderr, "\t [-l, --filesize <file size>] \n");
	fprintf(stderr, "\t [-b, --bufsize <buffer size>] \n");
	fprintf(stderr, "\t [-r, --random] \n");
	fprintf(stderr, "\t [-g, --gfsd <gfsd hostname>>] \n");
	fprintf(stderr, "\t [-k, --timeout <timeout (sec)>] \n");
#else
	fprintf(stderr, "\t [-t <gfarm directory>] \n");
	fprintf(stderr, "\t [-m <gfarm2fs mount point>] \n");
	fprintf(stderr, "\t [-l <file size>] \n");
	fprintf(stderr, "\t [-b <buffer size>] \n");
	fprintf(stderr, "\t [-r] (random) \n");
	fprintf(stderr, "\t [-g <gfsd hostname>>] \n");
	fprintf(stderr, "\t [-k <timeout (sec)>] \n");
#endif
}

static
gfarm_error_t
parse_opt(int argc, char *argv[])
{
	int r;
	static char *optstr = "ht:m:l:b:r::g:k:";
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	static struct option long_options[] = {
		{"testdir", 1, 0, 't'},
		{"filesize", 1, 0, 'l'},
		{"bufsize", 1, 0, 'b'},
		{"random", 2, 0, 'r'},
		{"gfsd", 1, 0, 'g'},
		{"gfarm2fs", 1, 0, 'm'},
		{"timeout", 1, 0, 'k'},
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
		case 'b':
			bufsize_string = optarg;
			bufsize = gfperf_strtonum(bufsize_string);
			if (bufsize < 0) {
				fprintf(stderr, "bufsize too big!\n");
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			break;
		case 'r':
			random_flag = 1;
			break;
		case 'g':
			gfsd_hostname = strdup(optarg);
			break;
		case 'm':
			gfarm2fsdir = optarg;
			break;
		case 'k':
			timeout = atoi(optarg);
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	if (gfarm2fsdir == NULL)
		posix_flag = 0;
	else
		posix_flag = 1;

	r = gethostname(hostname, sizeof(hostname));
	if (r < 0)
		strcpy(hostname, "unknown");

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_sequential_read_posix(const char *filename, char *buf)
{
	struct timeval start_time, middle_time, end_time, exec_time;
	int fd, r, ret, e;
	long long size;
	float t, f;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "can not open %s\n", filename);
		return (GFARM_ERR_CANT_OPEN);
	}


	gettimeofday(&start_time, NULL);
	set_timer();
	ret = read(fd, buf, bufsize);
	if (ret < 0) {
		fprintf(stderr, "read error %s\n", strerror(errno));
		close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	gettimeofday(&middle_time, NULL);

	size = 0;
	while ((r = read(fd, buf, bufsize)) > 0) {
		size += r;
		if (stop_flag)
			break;
	}
	if (r < 0) {
		fprintf(stderr, "read error %s\n", strerror(errno));
		close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gettimeofday(&end_time, NULL);

	e = close(fd);
	if (e < 0) {
		fprintf(stderr, "close error %s\n", strerror(errno));
		close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)ret / t;
	printf("io/gfarm2fs/read/sequential/startup/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname, gfsd_hostname, f, t);

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)size / t;
	printf("io/gfarm2fs/read/sequential/average/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname, gfsd_hostname, f, t);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_random_read_posix(const char *filename, char *buf)
{
	struct timeval start_time, middle_time, end_time, exec_time;
	long long i, n;
	int r, e;
	int fd;
	off_t offset, max_offset;
	long long size;
	float t, f;

	n = filesize / bufsize;
	if (filesize % bufsize != 0)
		n++;

	max_offset = filesize - bufsize;

	srandom(time(NULL));

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "can not open %s\n", filename);
		return (GFARM_ERR_CANT_OPEN);
	}


	gettimeofday(&start_time, NULL);
	set_timer();
	offset = ((long long)random()<<32) + random();
	lseek(fd,  offset % max_offset, SEEK_SET);
	r = read(fd, buf, bufsize);
	if (r < 0) {
		fprintf(stderr, "read error %s\n", strerror(errno));
		close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	gettimeofday(&middle_time, NULL);

	size = 0;
	for (i = 1; i < n; i++) {
		offset = ((long long)random()<<32) + random();
		lseek(fd, offset % max_offset, SEEK_SET);
		r = read(fd, buf, bufsize);
		if (r == 0)
			break;
		else if (r < 0) {
			fprintf(stderr, "read error %s\n", strerror(errno));
			close(fd);
			return (GFARM_ERR_INPUT_OUTPUT);
		}
		size += r;
		if (stop_flag)
			break;
	}
	gettimeofday(&end_time, NULL);

	e = close(fd);
	if (e < 0) {
		fprintf(stderr, "close error %s\n", strerror(errno));
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)r / t;
	printf("io/gfarm2fs/read/random/startup/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname, gfsd_hostname, f, t);

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)size / t;
	printf("io/gfarm2fs/read/random/average/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname, gfsd_hostname, f, t);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_posix(const char *filename, const char *gfarm_filename)
{
	gfarm_error_t e;
	char *buf;
	struct gfs_replica_info *ri;

	if (gfperf_is_file_exist_gfarm(gfarm_filename) == 0) {
		e = gfperf_create_file_on_gfarm(gfarm_filename,
						gfsd_hostname, filesize);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}

	if (gfsd_hostname == NULL) {
		e = gfs_replica_info_by_name(gfarm_filename, 0, &ri);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		gfsd_hostname = strdup(gfs_replica_info_nth_host(ri, 0));
		if (gfsd_hostname == NULL) {
			fprintf(stderr, "can not allocate memory!\n");
			unlink(filename);
			gfs_replica_info_free(ri);
			return (GFARM_ERR_NO_MEMORY);
		}
		gfs_replica_info_free(ri);
	}

	GFARM_MALLOC_ARRAY(buf, bufsize);
	if (buf == NULL) {
		fprintf(stderr, "can not allocate memory!\n");
		unlink(filename);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (random_flag)
		do_random_read_posix(filename, buf);
	else
		do_sequential_read_posix(filename, buf);

	free(buf);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_random_read_gfarm(const char *filename, char *buf)
{
	struct timeval start_time, middle_time, end_time, exec_time;
	long long i, n;
	int r, ret;
	gfarm_error_t e;
	gfarm_off_t offset, max_offset, r_offset;
	GFS_File fd;
	long long size;
	float t, f;

	n = filesize / bufsize;
	if (filesize % bufsize != 0)
		n++;

	max_offset = filesize - bufsize;

	srandom(time(NULL));

	e = gfs_pio_open(filename, GFARM_FILE_RDONLY, &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not open %s\n", filename);
		return (GFARM_ERR_CANT_OPEN);
	}


	gettimeofday(&start_time, NULL);
	set_timer();
	offset = ((long long)random()<<32) + random();
	gfs_pio_seek(fd,  offset % max_offset, GFARM_SEEK_SET, &r_offset);
	e = gfs_pio_read(fd, buf, bufsize, &r);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "read error %s\n",
			gfarm_error_string(e));
		gfs_pio_close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	gettimeofday(&middle_time, NULL);

	size = 0;
	for (i = 1; i < n; i++) {
		offset = ((long long)random()<<32) + random();
		gfs_pio_seek(fd,  offset % max_offset, GFARM_SEEK_SET,
			     &r_offset);
		e = gfs_pio_read(fd, buf, bufsize, &ret);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "read error %s\n",
				gfarm_error_string(e));
			gfs_pio_close(fd);
			return (GFARM_ERR_INPUT_OUTPUT);
		}
		if (ret == 0)
			break;
		size += ret;
		if (stop_flag)
			break;
	}
	gettimeofday(&end_time, NULL);

	e = gfs_pio_close(fd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "close error %s\n",
			gfarm_error_string(e));
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)r / t;
	printf("io/libgfarm/read/random/startup/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname,
	       gfsd_hostname, f, t);

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)size / t;
	printf("io/libgfarm/read/random/average/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname,
	       gfsd_hostname, f, t);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_sequential_read_gfarm(const char *filename, char *buf)
{
	struct timeval start_time, middle_time, end_time, exec_time;
	int r, ret;
	gfarm_error_t e;
	GFS_File fd;
	long long size;
	float t, f;

	e = gfs_pio_open(filename, GFARM_FILE_RDONLY, &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not open %s\n", filename);
		return (GFARM_ERR_CANT_OPEN);
	}


	gettimeofday(&start_time, NULL);
	set_timer();
	e = gfs_pio_read(fd, buf, bufsize, &r);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "read error %s\n",
			gfarm_error_string(e));
		gfs_pio_close(fd);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	gettimeofday(&middle_time, NULL);

	size = 0;
	while ((e = gfs_pio_read(fd, buf, bufsize, &ret))
	       == GFARM_ERR_NO_ERROR) {
		if (ret == 0)
			break;
		size += ret;
		if (stop_flag)
			break;
	}
	gettimeofday(&end_time, NULL);

	e = gfs_pio_close(fd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "close error %s\n",
			gfarm_error_string(e));
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)r / t;
	printf("io/libgfarm/read/sequential/startup/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname,
	       gfsd_hostname, f, t);

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)size / t;
	printf("io/libgfarm/read/sequential/average/%s/%s/%s/%s = "
	       "%.02f bytes/sec %g sec\n",
	       filesize_string, bufsize_string, hostname,
	       gfsd_hostname, f, t);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_gfarm(const char *filename)
{
	gfarm_error_t e;
	char *buf;
	struct gfs_replica_info *ri;

	if (gfperf_is_file_exist_gfarm(filename) == 0) {
		e = gfperf_create_file_on_gfarm(filename, gfsd_hostname,
						filesize);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
	}

	if (gfsd_hostname == NULL) {
		e = gfs_replica_info_by_name(filename, 0, &ri);
		if (e != GFARM_ERR_NO_ERROR)
			return (e);
		gfsd_hostname = strdup(gfs_replica_info_nth_host(ri, 0));
		if (gfsd_hostname == NULL) {
			fprintf(stderr, "can not allocate memory!\n");
			gfs_unlink(filename);
			gfs_replica_info_free(ri);
			return (GFARM_ERR_NO_MEMORY);
		}
		gfs_replica_info_free(ri);
	}

	GFARM_MALLOC_ARRAY(buf, bufsize);
	if (buf == NULL) {
		fprintf(stderr, "can not allocate memory!\n");
		gfs_unlink(filename);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (random_flag)
		do_random_read_gfarm(filename, buf);
	else
		do_sequential_read_gfarm(filename, buf);

	free(buf);

	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char *argv[])
{
	int r;
	gfarm_error_t e;
	char *filename;
	char *gfarm_filename = NULL;
	char *dir;

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

	if (gfarm2fsdir == NULL) {
		dir = strdup(testdir);
		if (dir == NULL) {
			fprintf(stderr, "can not allocate memory!\n");
			gfarm_terminate();
			return (1);
		}
	} else {
		r = asprintf(&dir, "%s%s",
			     gfarm2fsdir, gfperf_find_root_from_url(testdir));
		if (r < 0) {
			fprintf(stderr, "can not allocate memory!\n");
			gfarm_terminate();
			return (1);
		}
	}

	if (posix_flag)
		e = gfperf_is_dir_posix(dir);
	else
		e = gfperf_is_dir_gfarm(dir);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s is not a directory.\n",
			dir);
		free(dir);
		gfarm_terminate();
		return (1);
	}

	r = asprintf(&filename, "%s/read-%s%s%s.tst", dir, filesize_string,
		     gfsd_hostname ? "-" : "",
		     gfsd_hostname ? gfsd_hostname : "");
	if (r < 0) {
		fprintf(stderr, "can not allocate memory!\n");
		free(dir);
		gfarm_terminate();
		return (1);
	}

	if (posix_flag) {
		r = asprintf(&gfarm_filename, "%s/read-%s%s%s.tst",
			     gfperf_find_root_from_url(testdir),
			     filesize_string,
			     gfsd_hostname ? "-" : "",
			     gfsd_hostname ? gfsd_hostname : "");
		if (r < 0) {
			fprintf(stderr, "can not allocate memory!\n");
			free(dir);
			gfarm_terminate();
			return (1);
		}
		do_test_posix(filename, gfarm_filename);
		free(gfarm_filename);
	} else
		do_test_gfarm(filename);

	free(filename);
	free(dir);
	if (gfsd_hostname)
		free(gfsd_hostname);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	return (0);
}

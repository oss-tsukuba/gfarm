/*
 * $Id$
 */


#include "gfperf-lib.h"
#ifdef sun
#include <sys/stat.h>
#endif
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif
#include <string.h>

#define UNIT_OPS "ops"
#define UNIT_USEC "usec"
#define UNIT_FLAG_UNDEF 0
#define UNIT_FLAG_OPS 1
#define UNIT_FLAG_USEC 2

static char *testdir = "gfarm:///tmp";
static int width = 5;
static int depth = 3;
static int posix_flag = 0;
static char *unit = UNIT_OPS;
static int unit_flag = UNIT_FLAG_OPS;
static char hostname[1024];

static
gfarm_error_t
do_rmdir_posix(char *dir)
{
	DIR *current;
	struct dirent *de;
	char *name;
	int r;
#ifdef sun
	struct stat stat_buf;
#endif

	current = opendir(dir);
	if (current == NULL)
		return (GFARM_ERR_NOT_A_DIRECTORY);

	while ((de = readdir(current)) != NULL) {
		if (strcmp(de->d_name, ".") == 0)
			continue;
		if (strcmp(de->d_name, "..") == 0)
			continue;

		r = asprintf(&name, "%s/%s", dir, de->d_name);
		if (r < 0) {
			closedir(current);
			return (GFARM_ERR_NO_MEMORY);
		}
#ifdef sun
		r = lstat(name, &stat_buf);
		if (r < 0) {
			closedir(current);
			return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
		}
		if (S_ISDIR(stat_buf.st_mode)) {
			do_rmdir_posix(name);
			rmdir(name);
		} else if (S_ISREG(stat_buf.st_mode)) {
			unlink(name);
		}
#else
		if (de->d_type == DT_DIR) {
			do_rmdir_posix(name);
			rmdir(name);
		} else if (de->d_type == DT_REG) {
			unlink(name);
		}
#endif
		free(name);
	}

	closedir(current);
	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_mkdir_posix(char *dir, int n, int w, int d)
{
	int i;
	int e;
	char *name;
	if (n >= d)
		return (GFARM_ERR_NO_ERROR);

	for (i = 0; i < w; i++) {
		e = asprintf(&name, "%s/test%04d", dir, i);
		if (e < 0)
			return (GFARM_ERR_NO_MEMORY);
		e = mkdir(name, 0755);
		if (e != 0) {
			free(name);
			return (GFARM_ERR_NO_SPACE);
		}
		do_mkdir_posix(name, n+1, w, d);
		free(name);
	}

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_posix()
{
	struct timeval start_time, middle_time, end_time, exec_time;
	float f;
	int r;
	char *topdir;

	r = asprintf(&topdir, "%s/gfperf-tree-%s-%d", testdir,
		     hostname, getpid());
	if (r < 0)
		return (GFARM_ERR_NO_MEMORY);

	r = mkdir(topdir, 0755);
	if (r < 0) {
		fprintf(stderr, "can not make working directory!\n");
		free(topdir);
		return (GFARM_ERR_NO_SPACE);
	}

	gettimeofday(&start_time, NULL);
	do_mkdir_posix(topdir, 0, width, depth);
	gettimeofday(&middle_time, NULL);
	do_rmdir_posix(topdir);
	gettimeofday(&end_time, NULL);
	rmdir(topdir);
	free(topdir);

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	if (unit_flag == UNIT_FLAG_OPS) {
		f = (float)exec_time.tv_sec*1000000 + exec_time.tv_usec;
		f = (float)1000000 / f;
		printf("metadata/posix/tree/create/%d/%d = %.02f ops %g sec\n",
		       width, depth, f, gfperf_timeval_to_float(&exec_time));
	} else
		printf("metadata/posix/tree/create/%d/%d = %ld usec %g sec\n",
		       width, depth,
		       exec_time.tv_sec*1000000 + exec_time.tv_usec,
		       gfperf_timeval_to_float(&exec_time));

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	if (unit_flag == UNIT_FLAG_OPS) {
		f = (float)exec_time.tv_sec*1000000 + exec_time.tv_usec;
		f = (float)1000000/f;
		printf("metadata/posix/tree/remove/%d/%d = %.02f ops %g sec\n",
		       width, depth, f, gfperf_timeval_to_float(&exec_time));
	} else
		printf("metadata/posix/tree/remove/%d/%d = %ld usec %g sec\n",
		       width, depth,
		       exec_time.tv_sec*1000000 + exec_time.tv_usec,
		       gfperf_timeval_to_float(&exec_time));

	return (GFARM_ERR_NO_ERROR);

}

static
gfarm_error_t
do_rmdir_gfarm(char *dir)
{
	GFS_Dir current;
	struct gfs_dirent *de;
	char *name;
	gfarm_error_t e;
	int r;

	e = gfs_opendir(dir, &current);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	while ((e = gfs_readdir(current, &de)) == GFARM_ERR_NO_ERROR) {
		if (de == NULL)
			break;
		if (strcmp(de->d_name, ".") == 0)
			continue;
		if (strcmp(de->d_name, "..") == 0)
			continue;

		r = asprintf(&name, "%s/%s", dir, de->d_name);
		if (r < 0) {
			gfs_closedir(current);
			return (GFARM_ERR_NO_MEMORY);
		}
		if (de->d_type == GFS_DT_DIR) {
			do_rmdir_gfarm(name);
			gfs_rmdir(name);
		} else if (de->d_type == GFS_DT_REG) {
			gfs_unlink(name);
		}
		free(name);
	}

	gfs_closedir(current);
	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_mkdir_gfarm(char *dir, int n, int w, int d)
{
	int i;
	int r;
	gfarm_error_t e;
	char *name;
	if (n >= d)
		return (GFARM_ERR_NO_ERROR);

	for (i = 0; i < w; i++) {
		r = asprintf(&name, "%s/test%04d", dir, i);
		if (r < 0)
			return (GFARM_ERR_NO_MEMORY);
		e = gfs_mkdir(name, 0755);
		if (e != GFARM_ERR_NO_ERROR) {
			free(name);
			return (e);
		}
		do_mkdir_gfarm(name, n+1, w, d);
		free(name);
	}

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_test_gfarm()
{
	struct timeval start_time, middle_time, end_time, exec_time;
	float f;
	int r;
	gfarm_error_t e;
	char *topdir;

	r = asprintf(&topdir, "%s/gfperf-tree-%s-%d", testdir,
		     hostname, getpid());
	if (r < 0)
		return (GFARM_ERR_NO_MEMORY);

	e = gfs_mkdir(topdir, 0755);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not make working directory!\n");
		free(topdir);
		return (e);
	}

	gettimeofday(&start_time, NULL);
	do_mkdir_gfarm(topdir, 0, width, depth);
	gettimeofday(&middle_time, NULL);
	do_rmdir_gfarm(topdir);
	gettimeofday(&end_time, NULL);
	gfs_rmdir(topdir);
	free(topdir);

	gfperf_sub_timeval(&middle_time, &start_time, &exec_time);
	if (unit_flag == UNIT_FLAG_OPS) {
		f = (float)exec_time.tv_sec*1000000 + exec_time.tv_usec;
		f = (float)1000000/f;
		printf("metadata/libgfarm/tree/create/%d/%d = %.02f ops"
		       " %g sec\n",
		       width, depth, f, gfperf_timeval_to_float(&exec_time));
	} else
		printf("metadata/libgfarm/tree/create/%d/%d = %ld usec"
		       " %g sec\n",
		       width, depth,
		       exec_time.tv_sec*1000000 + exec_time.tv_usec,
		       gfperf_timeval_to_float(&exec_time));

	gfperf_sub_timeval(&end_time, &middle_time, &exec_time);
	if (unit_flag == UNIT_FLAG_OPS) {
		f = (float)exec_time.tv_sec*1000000 + exec_time.tv_usec;
		f = (float)1000000/f;
		printf("metadata/libgfarm/tree/remove/%d/%d = %.02f ops"
		       " %g sec\n",
		       width, depth, f, gfperf_timeval_to_float(&exec_time));
	} else
		printf("metadata/libgfarm/tree/remove/%d/%d = %ld usec"
		       " %g sec\n",
		       width, depth,
		       exec_time.tv_sec*1000000 + exec_time.tv_usec,
		       gfperf_timeval_to_float(&exec_time));

	return (GFARM_ERR_NO_ERROR);

}

static
void
usage(char *argv[])
{
	fprintf(stderr, "%s\n", argv[0]);
#ifdef HAVE_GETOPT_LONG
	fprintf(stderr, "\t [-t, --testdir <gfarm url>] \n");
	fprintf(stderr, "\t [-w, --width <number of directories>] \n");
	fprintf(stderr, "\t [-d, --depth <directory depth>] \n");
	fprintf(stderr, "\t [-u, --unit  <ops or usec>] \n");
#else
	fprintf(stderr, "\t [-t <gfarm url>] \n");
	fprintf(stderr, "\t [-w <number of directories>] \n");
	fprintf(stderr, "\t [-d <directory depth>] \n");
	fprintf(stderr, "\t [-u,<ops or usec>] \n");
#endif
}

static
gfarm_error_t
parse_opt(int argc, char *argv[])
{
	int r;
	static char *optstr = "ht:w:d:u:";
#ifdef HAVE_GETOPT_LONG
	int option_index = 0;
	static struct option long_options[] = {
		{"testdir", 1, 0, 't'},
		{"width", 1, 0, 'w'},
		{"depth", 1, 0, 'd'},
		{"unit", 1, 0, 'u'},
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
		case 'w':
			width = atoi(optarg);
			break;
		case 'd':
			depth = atoi(optarg);
			break;
		case 'u':
			unit = optarg;
			if (strcmp(unit, UNIT_OPS) == 0)
				unit_flag = UNIT_FLAG_OPS;
			else if (strcmp(unit, UNIT_USEC) == 0)
				unit_flag = UNIT_FLAG_USEC;
			else {
				unit_flag = UNIT_FLAG_UNDEF;
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
			break;
		default:
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}

	if (gfperf_is_file_url(testdir)) {
		posix_flag = 1;
		testdir = &testdir[GFPERF_FILE_URL_PREFIX_LEN];
	} else
		posix_flag = 0;

	r = gethostname(hostname, sizeof(hostname));
	if (r < 0)
		strcpy(hostname, "unknown");

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
		e = gfperf_is_dir_posix(testdir);
	else
		e = gfperf_is_dir_gfarm(testdir);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s is not a directory.\n",
			testdir);
		gfarm_terminate();
		return (1);
	}

	if (posix_flag)
		do_test_posix();
	else
		do_test_gfarm();

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", argv[0],
			gfarm_error_string(e));
		return (1);
	}

	return (0);
}

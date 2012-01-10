/*
 * $Id$
 */


#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <utime.h>
#include <dirent.h>
#include <errno.h>

#include "gfperf-lib.h"
#include "gfperf-metadata.h"

static
gfarm_error_t
do_posix_readdir()
{
	DIR *d;
	struct dirent *de;
	int c, e;
	float f;
	char filename[1024];
	struct timeval start_time, end_time, exec_time;
	struct stat st;

	gettimeofday(&start_time, NULL);
	d = opendir(testdir);
	if (d == NULL) {
		fprintf(stderr, "can not open directory %s\n", testdir);
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}

	c = 0;
	while ((de = readdir(d)) != NULL)
		c++;

	closedir(d);
	gettimeofday(&end_time, NULL);

	sub_timeval(&end_time, &start_time, &exec_time);
	f = (float)exec_time.tv_sec*1000000 + (float)exec_time.tv_usec;
	f = f / (float)c;

	if (unit_flag == UNIT_FLAG_OPS)
		f = (float)1000000/f;

	printf("metadata/posix/readdir/%d = %.02f %s\n", c, f, unit);

	gettimeofday(&start_time, NULL);
	d = opendir(testdir);
	if (d == NULL) {
		fprintf(stderr, "can not open directory %s\n", testdir);
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}

	c = 0;
	while ((de = readdir(d)) != NULL) {
		c++;
		snprintf(filename, sizeof(filename),
			"%s/%s", testdir, de->d_name);
		e = stat(filename, &st);
		if (e < 0)
			return (GFARM_ERR_INPUT_OUTPUT);
	}

	closedir(d);
	gettimeofday(&end_time, NULL);

	sub_timeval(&end_time, &start_time, &exec_time);
	f = (float)exec_time.tv_sec*1000000 + (float)exec_time.tv_usec;
	f = f / (float)c;

	if (unit_flag == UNIT_FLAG_OPS)
		f = (float)1000000/f;

	printf("metadata/posix/readdir+stat/%d = %.02f %s\n", c, f, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_mkdir(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = mkdir(names->names[0], MKDIR_MODE);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "mkdir: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = mkdir(names->names[i], MKDIR_MODE);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "mkdir: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/mkdir = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/mkdir = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_stat(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	struct stat sb;

	set_number(&r, names->n);
	set_start(&r);
	e = stat(names->names[0], &sb);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "stat: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = stat(names->names[i], &sb);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "stat: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/stat = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/stat = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_chmod(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = chmod(names->names[0], CHMOD_MODE);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "chmod: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = chmod(names->names[i], CHMOD_MODE);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "chmod: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/chmod = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/chmod = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_utimes(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	struct timeval times[2];

	gettimeofday(&times[0], NULL);
	gettimeofday(&times[1], NULL);

	set_number(&r, names->n);
	set_start(&r);
	e = utimes(names->names[0], times);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "utimes: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = utimes(names->names[i], times);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "utimes: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/utimes = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/utimes = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_rename(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	struct directory_names *tmp;

	tmp = create_directory_names(names->n, ".d");
	if (tmp == NULL) {
		fprintf(stderr, "can not allocate memory\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	set_number(&r, names->n);
	set_start(&r);
	e = rename(names->names[0], tmp->names[0]);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "rename: %s\n",
			strerror(saved_errno));
		goto err_return;
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = rename(names->names[i], tmp->names[i]);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "rename: %s\n",
				strerror(saved_errno));
			goto err_return;
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/rename = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/rename = %.2f %s\n",
	       loop_number, r.average, unit);

	for (i = 0; i <= names->n; i++)
		e = rename(tmp->names[i], names->names[i]);

	free_directory_names(tmp);
	return (GFARM_ERR_NO_ERROR);

err_return:
	for (i = 0; i <= names->n; i++)
		rename(tmp->names[i], names->names[i]);

	free_directory_names(tmp);
	return (gfarm_errno_to_error(saved_errno));
}

static
gfarm_error_t
do_posix_symlink(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	struct directory_names *tmp;
	char buf[BUF_SIZE];

	tmp = create_directory_names(names->n, ".d");
	if (tmp == NULL) {
		fprintf(stderr, "can not allocate memory\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	set_number(&r, names->n);
	set_start(&r);
	e = symlink(names->names[0], tmp->names[0]);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "rename: %s\n",
			strerror(saved_errno));
		goto err_return;
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = symlink(names->names[i], tmp->names[i]);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "rename: %s\n",
				strerror(saved_errno));
			goto err_return;
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/symlink = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/symlink = %.2f %s\n",
	       loop_number, r.average, unit);

	set_number(&r, names->n);
	set_start(&r);
	e = readlink(tmp->names[0], buf, sizeof(buf));
	if (e < 0) {
		saved_errno = errno;
		fprintf(stderr, "readlink: %s\n",
			strerror(saved_errno));
		goto err_return;
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = readlink(tmp->names[i], buf, sizeof(buf));
		if (e < 0) {
			saved_errno = errno;
			fprintf(stderr, "readlink: %s\n",
				strerror(saved_errno));
			goto err_return;
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/readlink = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/readlink = %.2f %s\n",
	       loop_number, r.average, unit);

	for (i = 0; i <= names->n; i++)
		e = unlink(tmp->names[i]);

	free_directory_names(tmp);
	return (GFARM_ERR_NO_ERROR);

err_return:
	for (i = 0; i <= names->n; i++)
		unlink(tmp->names[i]);

	free_directory_names(tmp);
	return (gfarm_errno_to_error(saved_errno));
}

static
gfarm_error_t
do_posix_rmdir(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = rmdir(names->names[0]);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "rmdir: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = rmdir(names->names[i]);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "rmdir: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/rmdir = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/rmdir = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_setxattr(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	static char *value = "2";
	size_t val_len = strlen(value);

	set_number(&r, names->n);
	set_start(&r);
	e = setxattr(names->names[0], XATTR_KEY, value, val_len, XATTR_CREATE);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "setxattr: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = setxattr(names->names[i], XATTR_KEY, value, val_len,
			     XATTR_CREATE);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "setxattr: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/setxattr = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/setxattr = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_getxattr(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;
	char *buf[BUF_SIZE];

	set_number(&r, names->n);
	set_start(&r);
	e = getxattr(names->names[0], XATTR_KEY, buf, sizeof(buf));
	if (e < 0) {
		saved_errno = errno;
		fprintf(stderr, "getxattr: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = getxattr(names->names[i], XATTR_KEY, buf, sizeof(buf));
		if (e < 0) {
			saved_errno = errno;
			fprintf(stderr, "getxattr: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/getxattr = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/getxattr = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_removexattr(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = removexattr(names->names[0], XATTR_KEY);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "removexattr: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = removexattr(names->names[i], XATTR_KEY);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "removexattr: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/removexattr = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/removexattr = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_create(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = creat(names->names[0], MKDIR_MODE);
	if (e < 0) {
		saved_errno = errno;
		fprintf(stderr, "create: %s\n",
			strerror(saved_errno));
		close(e);
		return (gfarm_errno_to_error(saved_errno));
	}
	close(e);
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = creat(names->names[i], MKDIR_MODE);
		if (e < 0) {
			saved_errno = errno;
			fprintf(stderr, "create: %s\n",
				strerror(saved_errno));
			close(e);
			return (gfarm_errno_to_error(saved_errno));
		}
		close(e);
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/create = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/create = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_posix_unlink(struct directory_names *names)
{
	int i, e, saved_errno;
	struct test_results r;

	set_number(&r, names->n);
	set_start(&r);
	e = unlink(names->names[0]);
	if (e != 0) {
		saved_errno = errno;
		fprintf(stderr, "unlink: %s\n",
			strerror(saved_errno));
		return (gfarm_errno_to_error(saved_errno));
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = unlink(names->names[i]);
		if (e != 0) {
			saved_errno = errno;
			fprintf(stderr, "unlink: %s\n",
				strerror(saved_errno));
			return (gfarm_errno_to_error(saved_errno));
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/posix/startup/unlink = %.2f %s\n",
	       r.startup, unit);
	printf("metadata/posix/average/%d/unlink = %.2f %s\n",
	       loop_number, r.average, unit);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
cleanup_files_posix(struct directory_names *files)
{
	int i;

	for (i = 0; i <= files->n; i++)
		unlink(files->names[i]);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
cleanup_dirs_posix(struct directory_names *dirs)
{
	int i;

	for (i = 0; i <= dirs->n; i++)
		rmdir(dirs->names[i]);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
do_posix_test(struct directory_names *dirs, struct directory_names *files)
{
	gfarm_error_t e;

	cleanup_files_posix(files);
	e = do_posix_create(files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_readdir();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_unlink(files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	cleanup_dirs_posix(dirs);
	e = do_posix_mkdir(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_stat(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_chmod(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_utimes(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_rename(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_symlink(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_setxattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_getxattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_removexattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_posix_rmdir(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return (GFARM_ERR_NO_ERROR);
}

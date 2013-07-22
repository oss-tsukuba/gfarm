/*
 * $Id$
 */


#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "gfperf-lib.h"
#include "gfperf-metadata.h"

static
gfarm_error_t
do_libgfarm_readdir()
{
	GFS_Dir d;
	struct gfs_dirent *de;
	gfarm_error_t e;
	int c;
	float f;
	char filename[1024];
	struct timeval start_time, end_time, exec_time;
	struct gfs_stat st;

	gettimeofday(&start_time, NULL);


	e = gfs_opendir_caching(topdir, &d);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not open directory %s\n", topdir);
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}

	c = 0;
	while ((e = gfs_readdir(d, &de)) == GFARM_ERR_NO_ERROR) {
		if (de == NULL)
			break;
		c++;
	}
	gfs_closedir(d);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_readdir: %s\n",
			gfarm_error_string(e));
		return (e);
	}

	gettimeofday(&end_time, NULL);

	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	f = (float)exec_time.tv_sec*1000000 + (float)exec_time.tv_usec;
	f = f / (float)c;

	if (unit_flag == UNIT_FLAG_OPS)
		f = (float)1000000/f;

	printf("metadata/libgfarm/readdir/%d = %.02f %s %g sec\n",
	       c, f, unit, gfperf_timeval_to_float(&exec_time));


	gettimeofday(&start_time, NULL);
	e = gfs_opendir_caching(topdir, &d);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "can not open directory %s\n", topdir);
		return (GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY);
	}

	c = 0;
	while ((e = gfs_readdir(d, &de)) == GFARM_ERR_NO_ERROR) {
		if (de == NULL)
			break;
		snprintf(filename, sizeof(filename),
			"%s/%s", topdir, de->d_name);
		e = gfs_lstat_cached(filename, &st);
		if (e == GFARM_ERR_NO_ERROR)
			gfs_stat_free(&st);
		else {
			fprintf(stderr, "gfs_stat: %s\n",
				gfarm_error_string(e));
			gfs_closedir(d);
			return (e);
		}
		c++;
	}

	gfs_closedir(d);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_readdir: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	gettimeofday(&end_time, NULL);

	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	f = (float)exec_time.tv_sec*1000000 + (float)exec_time.tv_usec;
	f = f / (float)c;

	if (unit_flag == UNIT_FLAG_OPS)
		f = (float)1000000/f;

	printf("metadata/libgfarm/readdir+stat/%d = %.02f %s %g sec\n",
	       c, f, unit, gfperf_timeval_to_float(&exec_time));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_mkdir(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_mkdir(names->names[0], MKDIR_MODE);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "mkdir: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_mkdir(names->names[i], MKDIR_MODE);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "mkdir: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/mkdir = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/mkdir = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_stat(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	struct gfs_stat sb;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_lstat(names->names[0], &sb);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "stat: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	gfs_stat_free(&sb);
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_lstat(names->names[i], &sb);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "stat: %s\n",
				gfarm_error_string(e));
			return (e);
		}
		gfs_stat_free(&sb);
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/stat = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/stat = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_chmod(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_chmod(names->names[0], CHMOD_MODE);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "chmod: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_chmod(names->names[i], CHMOD_MODE);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "stat: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/chmod = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/chmod = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_utimes(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	struct gfarm_timespec ts[2];
	struct timeval tv;

	gettimeofday(&tv, NULL);
	ts[0].tv_sec = tv.tv_sec;
	ts[0].tv_nsec = tv.tv_usec*1000;
	ts[1].tv_sec = tv.tv_sec;
	ts[1].tv_nsec = tv.tv_usec*1000;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_utimes(names->names[0], ts);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "utimes: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_utimes(names->names[i], ts);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "stat: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/utimes = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/utimes = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_rename(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	struct directory_names *tmp;

	tmp = create_directory_names(names->n, ".d");
	if (tmp == NULL) {
		fprintf(stderr, "can not allocate memory\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_rename(names->names[0], tmp->names[0]);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "rename: %s\n",
			gfarm_error_string(e));
		goto err_return;
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_rename(names->names[i], tmp->names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "rename: %s\n",
				gfarm_error_string(e));
			goto err_return;
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/rename = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/rename = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	for (i = 0; i <= names->n; i++)
		gfs_rename(tmp->names[i], names->names[i]);


	free_directory_names(tmp);
	return (GFARM_ERR_NO_ERROR);

err_return:
	for (i = 0; i <= names->n; i++)
		gfs_rename(tmp->names[i], names->names[i]);

	free_directory_names(tmp);
	return (e);

}

static
gfarm_error_t
do_libgfarm_symlink(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	char *read;
	struct directory_names *tmp;

	tmp = create_directory_names(names->n, ".d");
	if (tmp == NULL) {
		fprintf(stderr, "can not allocate memory\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_symlink(names->names[0], tmp->names[0]);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "symlink: %s\n",
			gfarm_error_string(e));
			goto err_return;
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_symlink(names->names[i], tmp->names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "symlink: %s\n",
				gfarm_error_string(e));
			goto err_return;
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/symlink = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/symlink = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));


	set_number(&r, names->n);
	set_start(&r);
	e = gfs_readlink(tmp->names[0], &read);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "readlink: %s\n",
			gfarm_error_string(e));
			goto err_return;
	}
	free(read);
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_readlink(tmp->names[i], &read);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "readlink: %s\n",
				gfarm_error_string(e));
			goto err_return;
		}
		free(read);
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/readlink = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/readlink = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	for (i = 0; i <= names->n; i++)
		gfs_unlink(tmp->names[i]);


	free_directory_names(tmp);
	return (GFARM_ERR_NO_ERROR);

err_return:
	for (i = 0; i <= names->n; i++)
		gfs_unlink(tmp->names[i]);

	free_directory_names(tmp);
	return (e);
}

static
gfarm_error_t
do_libgfarm_rmdir(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_rmdir(names->names[0]);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_rmdir(%s): %s\n",
		    names->names[0], gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_rmdir(names->names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_rmdir(%s): %s\n",
			    names->names[i], gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/rmdir = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/rmdir = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_setxattr(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	static char *value = "2";
	int val_len = strlen(value);

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_setxattr(names->names[0], XATTR_KEY, value, val_len,
			 GFS_XATTR_CREATE);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "setxattr: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_setxattr(names->names[i], XATTR_KEY, value, val_len,
				 GFS_XATTR_CREATE);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "setxattr: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/setxattr = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/setxattr = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_getxattr(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	char value[1024];
	size_t size;

	set_number(&r, names->n);
	set_start(&r);
	size = sizeof(value);
	e = gfs_getxattr(names->names[0], XATTR_KEY, value, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "getxattr: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		size = sizeof(value);
		e = gfs_getxattr(names->names[i], XATTR_KEY, value, &size);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "getxattr: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/getxattr = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/getxattr = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_removexattr(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_removexattr(names->names[0], XATTR_KEY);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "removexattr: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_removexattr(names->names[i], XATTR_KEY);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "removexattr: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/removexattr = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/removexattr = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_create(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;
	GFS_File f;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_pio_create(names->names[0], GFARM_FILE_RDWR, MKDIR_MODE, &f);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "create: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	gfs_pio_close(f);
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_pio_create(names->names[i], GFARM_FILE_RDWR,
				   MKDIR_MODE, &f);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "create: %s\n",
				gfarm_error_string(e));
			return (e);
		}
		gfs_pio_close(f);
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/create = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/create = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_libgfarm_unlink(struct directory_names *names)
{
	int i;
	struct test_results r;
	gfarm_error_t e;

	set_number(&r, names->n);
	set_start(&r);
	e = gfs_unlink(names->names[0]);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "unlink: %s\n",
			gfarm_error_string(e));
		return (e);
	}
	set_middle(&r);
	for (i = 1; i <= names->n; i++) {
		e = gfs_unlink(names->names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "unlink: %s\n",
				gfarm_error_string(e));
			return (e);
		}
	}
	set_end(&r);
	calc_result(&r);
	adjust_result(&r);

	printf("metadata/libgfarm/startup/unlink = %.2f %s %g sec\n",
	       r.startup, unit, get_start_middle(&r));
	printf("metadata/libgfarm/average/%d/unlink = %.2f %s %g sec\n",
	       loop_number, r.average, unit, get_middle_end(&r));

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
cleanup_files_libgfarm(struct directory_names *files)
{
	int i;

	for (i = 0; i <= files->n; i++)
		gfs_unlink(files->names[i]);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
cleanup_dirs_libgfarm(struct directory_names *dirs)
{
	int i;

	for (i = 0; i <= dirs->n; i++)
		gfs_rmdir(dirs->names[i]);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
do_libgfarm_test(struct directory_names *dirs, struct directory_names *files)
{
	gfarm_error_t e;

	cleanup_files_libgfarm(files);
	e = do_libgfarm_create(files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_readdir();
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_unlink(files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	cleanup_dirs_libgfarm(dirs);
	e = do_libgfarm_mkdir(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_stat(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_chmod(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_utimes(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_rename(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_symlink(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_setxattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_getxattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_removexattr(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = do_libgfarm_rmdir(dirs);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return (GFARM_ERR_NO_ERROR);
}

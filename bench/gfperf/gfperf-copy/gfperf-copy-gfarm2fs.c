/*
 * $Id$
 */


#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "gfperf-lib.h"

#include "gfperf-copy.h"

static
gfarm_error_t
do_copy() {
	const char *root;
	char *buf, *gfsd_hostname_bak;
	int sf, df, ret;
	float et;
	gfarm_error_t e;
	struct gfs_replica_info *ri;
	struct timeval start_time, end_time, exec_time;

	GFARM_MALLOC_ARRAY(buf, buf_size);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);

	if (gfarm_is_url(src_url)) {
		if (gfperf_is_file_exist_gfarm(src_filename) == 0) {
			e = gfperf_create_file_on_gfarm(src_filename,
							gfsd_hostname,
							file_size);
			if (e != GFARM_ERR_NO_ERROR) {
				free(buf);
				return (e);
			}
		}
		gfsd_hostname_bak = gfsd_hostname;
		if (gfsd_hostname == NULL) {
			e = gfs_replica_info_by_name(src_filename, 0, &ri);
			if (e != GFARM_ERR_NO_ERROR) {
				free(buf);
				return (e);
			}
			gfsd_hostname = strdup(gfs_replica_info_nth_host(ri,
									 0));
			if (gfsd_hostname == NULL) {
				fprintf(stderr, "can not allocate memory!\n");
				gfs_replica_info_free(ri);
				free(buf);
				return (GFARM_ERR_NO_MEMORY);
			}
			gfs_replica_info_free(ri);
		}
		root = gfperf_find_root_from_url(src_url);
		if (root == NULL) {
			free(buf);
			return (GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING);
		}
		free(src_filename);
		ret = asprintf(&src_filename, "%s%s/copy-%s%s%s.tst",
			       gfarm2fs_mount_point, root,
			       file_size_string,
			       gfsd_hostname_bak ? "-" : "",
			       gfsd_hostname_bak ? gfsd_hostname_bak : "");
		if (ret < 0) {
			free(buf);
			fprintf(stderr, "can not allocate memory.\n");
			return (GFARM_ERR_NO_MEMORY);
		}
	}

	if (gfarm_is_url(dst_url)) {
		e = gfperf_create_file_on_gfarm(dst_filename,
						gfsd_hostname, 1);
		if (e != GFARM_ERR_NO_ERROR) {
			free(buf);
			return (e);
		}
		if (gfsd_hostname == NULL) {
			e = gfs_replica_info_by_name(dst_filename, 0, &ri);
			if (e != GFARM_ERR_NO_ERROR) {
				free(buf);
				return (e);
			}
			gfsd_hostname = strdup(gfs_replica_info_nth_host(ri,
									 0));
			gfs_replica_info_free(ri);
		}
		root = gfperf_find_root_from_url(dst_url);
		if (root == NULL) {
			free(buf);
			return (GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING);
		}
		free(dst_filename);
		ret = asprintf(&dst_filename, "%s%s/test.%d.tst",
			       gfarm2fs_mount_point, root, getpid());
		if (ret < 0) {
			free(buf);
			fprintf(stderr, "can not allocate memory.\n");
			return (GFARM_ERR_NO_MEMORY);
		}

		if (gfperf_is_file_exist_posix(src_filename) == 0) {
			e = gfperf_create_file_on_local(src_filename,
							file_size);
			if (e != GFARM_ERR_NO_ERROR) {
				free(buf);
				return (e);
			}
		}
	}

	gettimeofday(&start_time, NULL);
	sf = open(src_filename, O_RDONLY);
	if (sf < 0) {
		fprintf(stderr, "open: %s,%s\n", src_filename,
			strerror(errno));
		unlink(src_filename);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	df = open(dst_filename, O_WRONLY|O_CREAT, 0644);
	if (df < 0) {
		fprintf(stderr, "open: %s, %s\n", dst_filename,
			strerror(errno));
		close(sf);
		unlink(src_filename);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	while ((ret = read(sf, buf, buf_size)) > 0) {
		ret = write(df, buf, ret);
		if (ret < 0) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			close(df);
			close(sf);
			unlink(src_filename);
			unlink(dst_filename);
			free(buf);
			return (GFARM_ERR_NO_SPACE);
		}
	}

	if (ret < 0) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		close(df);
		close(sf);
		unlink(src_filename);
		unlink(dst_filename);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	ret = close(df);
	if (ret < 0) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		close(sf);
		unlink(src_filename);
		unlink(dst_filename);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	ret = close(sf);
	if (ret < 0) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		unlink(src_filename);
		unlink(dst_filename);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	gettimeofday(&end_time, NULL);
	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	et = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;

	free(buf);
	unlink(dst_filename);

	switch (direction) {
	case 0:
		printf("io/posix/copy/%s/%s = %.02f bytes/sec %g sec\n",
		       file_size_string, buf_size_string,
		       (float)file_size/et, et);
		break;
	case LOCAL_TO_GFARM:
		printf("io/gfarm2fs/copy/togfarm/%s/%s/%s = %.02f bytes/sec"
		       " %g sec\n",
		       gfsd_hostname,
		       file_size_string, buf_size_string,
		       (float)file_size/et, et);
		break;
	case GFARM_TO_LOCAL:
		printf("io/gfarm2fs/copy/fromgfarm/%s/%s/%s = "
		       "%.02f bytes/sec %g sec\n",
		       gfsd_hostname,
		       file_size_string, buf_size_string,
		       (float)file_size/et, et);
		break;
	}
	return (GFARM_ERR_NO_ERROR);
}


gfarm_error_t
do_posix_test()
{
	do_copy();

	return (GFARM_ERR_NO_ERROR);
}

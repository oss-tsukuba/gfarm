/*
 * $Id$
 */


#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "gfperf-lib.h"

#include "gfperf-copy.h"

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include <openssl/evp.h>
#include "queue.h"
#include "gfs_pio.h"
#include "host.h"

static
gfarm_error_t
do_copy_to_gfarm()
{
	int sf, ret, size;
	GFS_File df;
	float et;
	gfarm_error_t e;
	char *src, *dst, *buf;
	struct gfs_replica_info *ri;
	struct timeval start_time, end_time, exec_time;

	GFARM_MALLOC_ARRAY(buf, buf_size);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);

	src = src_filename;
	dst = dst_filename;

	if (gfperf_is_file_exist_posix(src) == 0) {
		e = gfperf_create_file_on_local(src, file_size);
		if (e != GFARM_ERR_NO_ERROR) {
			free(buf);
			return (e);
		}
	}

	gettimeofday(&start_time, NULL);
	sf = open(src, O_RDONLY);
	if (sf < 0) {
		fprintf(stderr, "open: %s,%s\n", src, strerror(errno));
		unlink(src);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	e = gfs_pio_create(dst, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0644, &df);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "open: %s, %s\n", dst,
			gfarm_error_string(e));
		close(sf);
		unlink(src);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	if (gfsd_hostname != NULL) {
		/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
		e = gfs_pio_internal_set_view_section(df, gfsd_hostname);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
				"gfs_pio_internal_set_view_section() error! "
				"%s: %s\n", gfsd_hostname,
				gfarm_error_string(e));
			close(sf);
			unlink(src);
			gfs_pio_close(df);
			gfs_unlink(dst);
			free(buf);
			return (e);
		}
	}

	while ((ret = read(sf, buf, buf_size)) > 0) {
		e = gfs_pio_write(df, buf, ret, &size);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "write: %s\n",
				gfarm_error_string(e));
			gfs_pio_close(df);
			close(sf);
			unlink(src);
			gfs_unlink(dst);
			free(buf);
			return (GFARM_ERR_NO_SPACE);
		}
	}

	if (ret < 0) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		gfs_pio_close(df);
		close(sf);
		unlink(src);
		gfs_unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	e = gfs_pio_close(df);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "close: %s\n",
			gfarm_error_string(e));
		close(sf);
		unlink(src);
		gfs_unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	ret = close(sf);
	if (ret < 0) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		unlink(src);
		gfs_unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gettimeofday(&end_time, NULL);
	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	et = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	if (gfsd_hostname == NULL) {
		e = gfs_replica_info_by_name(dst, 0, &ri);
		if (e != GFARM_ERR_NO_ERROR)
			printf("io/libgfarm/copy/togfarm/%s/%s = "
			       "%.02f bytes/sec %g sec\n",
			       file_size_string, buf_size_string,
			       (float)file_size/et, et);
		else {
			printf("io/libgfarm/copy/togfarm/%s/%s/%s = "
			       "%.02f bytes/sec %g sec\n",
			       gfs_replica_info_nth_host(ri, 0),
			       file_size_string, buf_size_string,
			       (float)file_size/et, et);
			gfs_replica_info_free(ri);
		}
	} else
		printf("io/libgfarm/copy/togfarm/%s/%s/%s = "
		       "%.02f bytes/sec %g sec\n", gfsd_hostname,
		       file_size_string, buf_size_string, (float)file_size/et,
		       et);

	free(buf);
	gfs_unlink(dst);

	return (GFARM_ERR_NO_ERROR);
}

static
gfarm_error_t
do_copy_from_gfarm()
{
	int df, ret;
	GFS_File sf;
	float et;
	gfarm_error_t e;
	char *src, *dst, *buf;
	struct gfs_replica_info *ri;
	struct timeval start_time, end_time, exec_time;

	GFARM_MALLOC_ARRAY(buf, buf_size);
	if (buf == NULL)
		return (GFARM_ERR_NO_MEMORY);

	src = src_filename;
	dst = dst_filename;

	if (gfperf_is_file_exist_gfarm(src) == 0) {
		e = gfperf_create_file_on_gfarm(src, gfsd_hostname, file_size);
		if (e != GFARM_ERR_NO_ERROR) {
			free(buf);
			return (e);
		}
	}

	gettimeofday(&start_time, NULL);
	e = gfs_pio_open(src, GFARM_FILE_RDONLY, &sf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "open: %s,%s\n", src,
			gfarm_error_string(e));
		gfs_unlink(src);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	df = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (df < 0) {
		fprintf(stderr, "open: %s, %s\n", dst, strerror(errno));
		gfs_pio_close(sf);
		gfs_unlink(src);
		free(buf);
		return (GFARM_ERR_CANT_OPEN);
	}

	while ((e = gfs_pio_read(sf, buf, buf_size, &ret))
	       == GFARM_ERR_NO_ERROR) {
		if (ret == 0)
			break;
		ret = write(df, buf, ret);
		if (ret < 0) {
			fprintf(stderr, "write: %s\n", strerror(errno));
			gfs_pio_close(sf);
			close(df);
			gfs_unlink(src);
			unlink(dst);
			free(buf);
			return (GFARM_ERR_NO_SPACE);
		}
	}

	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "read: %s\n",
			gfarm_error_string(e));
		gfs_pio_close(sf);
		close(df);
		gfs_unlink(src);
		unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	ret = close(df);
	if (ret < 0) {
		fprintf(stderr, "close: %s\n", strerror(errno));
		gfs_pio_close(sf);
		gfs_unlink(src);
		unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}
	e = gfs_pio_close(sf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "close: %s\n",
			gfarm_error_string(e));
		gfs_unlink(src);
		unlink(dst);
		free(buf);
		return (GFARM_ERR_INPUT_OUTPUT);
	}

	gettimeofday(&end_time, NULL);
	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	et = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	if (gfsd_hostname == NULL) {
		e = gfs_replica_info_by_name(src, 0, &ri);
		if (e != GFARM_ERR_NO_ERROR)
			printf("io/libgfarm/copy/fromgfarm/%s/%s = "
			       "%.02f bytes/sec %g sec\n",
			       file_size_string, buf_size_string,
			       (float)file_size/et, et);
		else {
			printf("io/libgfarm/copy/fromgfarm/%s/%s/%s = "
			       "%.02f bytes/sec %g sec\n",
			       gfs_replica_info_nth_host(ri, 0),
			       file_size_string, buf_size_string,
			       (float)file_size/et, et);
			gfs_replica_info_free(ri);
		}
	} else
		printf("io/libgfarm/copy/fromgfarm/%s/%s/%s = "
		       "%.02f bytes/sec %g sec\n", gfsd_hostname,
		       file_size_string, buf_size_string, (float)file_size/et,
		       et);

	free(buf);
	unlink(dst);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
do_libgfarm_test()
{
	switch (direction) {
	case LOCAL_TO_GFARM:
		do_copy_to_gfarm();
		break;
	case GFARM_TO_LOCAL:
		do_copy_from_gfarm();
		break;
	default:
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	return (GFARM_ERR_NO_ERROR);
}

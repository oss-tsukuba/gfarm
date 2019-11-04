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

#include "host.h"
#include "gfs_pio.h"

gfarm_error_t
gfperf_create_file_on_gfarm(const char *url, char *hostname,
			    long long file_size)
{
	const char *filename;
	char *buf;
	long long leftsize;
	int ret, s;
	GFS_File fp;
	gfarm_error_t e;
	struct gfs_stat sb;

	filename = url;

	GFARM_CALLOC_ARRAY(buf, GFPERF_COPY_BUF_SIZE);
	if (buf == NULL) {
		fprintf(stderr, "can not allocate memory.\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	e = gfs_stat(filename, &sb);
	if (e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		fprintf(stderr, "file exists: %s\n", filename);
		if (e == GFARM_ERR_NO_ERROR)
			gfs_stat_free(&sb);
		free(buf);
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	e = gfs_pio_create(filename,
			   GFARM_FILE_WRONLY|GFARM_FILE_TRUNC,
			   0644,
			   &fp);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "open: %s\n",
			gfarm_error_string(e));
		free(buf);
		return (e);
	}
	if (hostname != NULL) {
		/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
		e = gfs_pio_internal_set_view_section(fp, hostname);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
				"gfs_pio_internal_set_view_section() error! "
				"%s: %s\n", hostname,
				gfarm_error_string(e));
			goto err_return;
		}
	}

	for (leftsize = file_size; leftsize > 0 ; leftsize -= ret) {
		s = (leftsize < GFPERF_COPY_BUF_SIZE) ?
			leftsize : GFPERF_COPY_BUF_SIZE;
		e = gfs_pio_write(fp, buf, s, &ret);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "write: %s\n",
				gfarm_error_string(e));
			goto err_return;
		}
	}
	e = gfs_pio_close(fp);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "close: %s\n",
			gfarm_error_string(e));
	}
	free(buf);
	return (e);

err_return:
	gfs_pio_close(fp);
	free(buf);
	return (e);
}

gfarm_error_t
gfperf_create_file_on_local(const char *filename, long long file_size)
{
	char *buf;
	long long leftsize;
	int ret, s;
	int fp;

	GFARM_CALLOC_ARRAY(buf, GFPERF_COPY_BUF_SIZE);
	if (buf == NULL) {
		fprintf(stderr, "can not allocate memory.\n");
		return (GFARM_ERR_NO_MEMORY);
	}

	fp = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fp < 0) {
		fprintf(stderr, "open: %s\n",
			strerror(errno));
		free(buf);
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	for (leftsize = file_size; leftsize > 0 ; leftsize -= ret) {
		s = (leftsize < GFPERF_COPY_BUF_SIZE) ?
			leftsize : GFPERF_COPY_BUF_SIZE;
		ret = write(fp, buf, s);
		if (ret < 0) {
			fprintf(stderr, "write: %s\n",
				strerror(errno));
			goto err_return;
		}
	}
	ret = close(fp);
	if (ret < 0) {
		fprintf(stderr, "close: %s\n",
			strerror(errno));
	}
	free(buf);
	return (ret);

err_return:
	close(fp);
	free(buf);
	return (ret);
}

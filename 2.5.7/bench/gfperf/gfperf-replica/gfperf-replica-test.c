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

#include "gfutil.h"
#include "gfperf-replica.h"

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include <openssl/evp.h>
#include "queue.h"
#include "gfs_pio.h"
#include "host.h"
#include "lookup.h"

static
gfarm_error_t
do_replica()
{
	gfarm_error_t e;
	struct gfm_connection *sv;
	struct gfarm_host_info from, to;
	const char *path = GFARM_PATH_ROOT;

	e = gfarm_url_parse_metadb(&path, &sv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "metadb %s: %s\n", testdir_filename,
			gfarm_error_string(e));
		return (e);
	}

	e = gfm_host_info_get_by_name_alias(sv, from_gfsd_name, &from);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "host_info %s: %s\n", from_gfsd_name,
			gfarm_error_string(e));
		return (e);
	}

	e = gfm_host_info_get_by_name_alias(sv, to_gfsd_name, &to);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "host_info %s: %s\n", to_gfsd_name,
			gfarm_error_string(e));
		gfarm_host_info_free(&from);
		return (e);
	}

	e = gfs_replicate_from_to(testdir_filename,
				  from.hostname, from.port,
				  to.hostname, to.port);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "replicate %s: %s\n", testdir_filename,
			gfarm_error_string(e));
		if (e == GFARM_ERR_ALREADY_EXISTS)
			fprintf(stderr,
				"may be enabled auto replication "
				"in %s or the parent directory.\n",
				testdir);
		gfarm_host_info_free(&from);
		gfarm_host_info_free(&to);
		return (e);
	}

	gfarm_host_info_free(&from);
	gfarm_host_info_free(&to);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
do_test()
{
	gfarm_error_t e;
	struct timeval start_time, end_time, exec_time;
	float f, t;

	e = gfperf_create_file_on_gfarm(testdir_filename,
					from_gfsd_name, file_size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	gettimeofday(&start_time, NULL);
	e = do_replica();
	gettimeofday(&end_time, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_unlink(testdir_filename);
		return (e);
	}

	e = gfs_unlink(testdir_filename);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "unlink %s: %s\n", testdir_filename,
			gfarm_error_string(e));
		return (e);
	}

	gfperf_sub_timeval(&end_time, &start_time, &exec_time);
	t = (float)exec_time.tv_sec + (float)exec_time.tv_usec/1000000;
	f = (float)file_size / t;
	if (parallel_flag)
		printf("parallel/%s/replica/libgfarm/%s/%s/%s = "
		       "%.02f bytes/sec %g sec\n",
		       group_name,
		       file_size_string, from_gfsd_name, to_gfsd_name, f, t);
	else
		printf("replica/libgfarm/%s/%s/%s = %.02f bytes/sec %g sec\n",
		       file_size_string, from_gfsd_name, to_gfsd_name, f, t);

	return (GFARM_ERR_NO_ERROR);
}

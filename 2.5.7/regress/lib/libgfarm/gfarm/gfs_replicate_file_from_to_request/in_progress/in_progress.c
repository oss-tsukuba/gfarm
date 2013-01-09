#include <stdio.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <gfarm/gfarm.h>

#include "queue.h"

#include "gfs_pio.h"

#define TEST_FILE_SIZE 100000000 /* 100MB */

static void
write_file(char *file, char *src_host)
{
	gfarm_error_t e;
	GFS_File gf;
	int rv, i;
	static const char buf[10];

#if 0
	gfs_unlink(file);
#endif

	e = gfs_pio_create(file, GFARM_FILE_WRONLY, 0644, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_open: %s\n", gfarm_error_string(e));
		exit(1);
	}
	e = gfs_pio_internal_set_view_section(gf, src_host);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "set_view: %s\n", gfarm_error_string(e));
		exit(1);
	}
	for (i = 0; i < TEST_FILE_SIZE / sizeof(buf); i++) {
		gfs_pio_write(gf, buf, sizeof buf, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_pio_write: %s\n",
			    gfarm_error_string(e));
			exit(1);
		}
	}
	e = gfs_pio_close(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_close: %s\n", gfarm_error_string(e));
		exit(1);
	}
}

static void
replicate_file(char *file, char *dst_host, char *diag)
{
	gfarm_error_t e;
	int n;
#if 0
	{
		struct gfs_replica_info *info;
		e = gfs_replica_info_by_name(file,
		    GFS_REPLICA_INFO_INCLUDING_INCOMPLETE_COPY, &info);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfs_replica_info_by_name(): %s\n",
			    diag, gfarm_error_string(e));
			return;
		}
		n = gfs_replica_info_number(info);
		gfs_replica_info_free(info);
	}
#else
	{
		char **copy;
		e = gfs_replica_list_by_name(file, &n, &copy);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfs_replica_list_by_name(): %s\n",
			    diag, gfarm_error_string(e));
			return;
		}
		gfarm_strings_free_deeply(n, copy);
	}
#endif
	if (n >= 2) {
		printf("%s: enough replica\n", diag);
		return;
	}

	e = gfs_replicate_file_to_request(file, dst_host, 0);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfs_replicate_file_to_request(): %s\n",
		    diag, gfarm_error_string(e));
		return;
	}
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	char *file, *src_host, *dst_host;

	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "initialize: %s\n", gfarm_error_string(e));
		return (2);
	}
	if (argc != 4) {
		fprintf(stderr,
		    "Usage: in_progress <gfarm_url> <src_host> <dst_host>\n");
		return (2);
	}
	file = argv[1];
	src_host = argv[2];
	dst_host = argv[3];

	write_file(file, src_host);
	replicate_file(file, dst_host, "1st time");
#if 0
	write_file(file, src_host);
#endif
	replicate_file(file, dst_host, "2nd time");

	gfarm_terminate();

	return (0);
}

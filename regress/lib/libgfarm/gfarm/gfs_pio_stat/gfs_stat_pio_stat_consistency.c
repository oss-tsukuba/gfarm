/*
 * test program for
 * SF.net #942 - mtime inconsistency between gfs_pio_stat() and gfs_stat()
 *
 * This inconsistency sometimes makes symlink extraction in tar(1) fail,
 * and a 0-byte file remains instead of a symlink in such case,
 * due to the implementation of create_placeholder_file() and
 * apply_delayed_links() of GNU tar command.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include "gfs_pio.h"	/* for gfs_pio_internal_set_view_section() */

char *program_name = "gfs_stat_pio_stat_consistency";

static int
timespec_compare(
	const struct gfarm_timespec *t1,
	const struct gfarm_timespec *t2, const char *diag)
{
	if (t1->tv_sec != t2->tv_sec) {
		fprintf(stderr, "st_%sspec.tv_sec differs %lld vs %lld\n",
		    diag, (long long)t1->tv_sec, (long long)t2->tv_sec);
		return (1);
	}
	if (t1->tv_nsec != t2->tv_nsec) {
		fprintf(stderr, "st_%sspec.tv_nsec differs %d vs %d\n",
		    diag, (int)t1->tv_nsec, (int)t2->tv_nsec);
		return (1);
	}
	return (0);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-acmNrRwW] [-h <host>] <testfile>\n",
	    program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	const char *pathname;
	GFS_File gf;
	char buf[BUFSIZ];
	ssize_t sz;
	int c, rv, failed, open_flags = GFARM_FILE_RDWR;
	enum { TEST_NOP, TEST_READ, TEST_WRITE } test_mode = TEST_WRITE;
	char *host = NULL;
	int test_atime = 0, test_ctime = 0, test_mtime = 0;
	struct gfs_stat st1, st2;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	while ((c = getopt(argc, argv, "acmNrRwWh:")) != -1) {
		switch (c) {
		case 'a':
			test_atime = 1;
			break;
		case 'c':
			test_ctime = 1;
			break;
		case 'm':
			test_mtime = 1;
			break;
		case 'N':
			test_mode = TEST_NOP;
			break;
		case 'r':
			open_flags = GFARM_FILE_RDONLY;
			break;
		case 'R':
			test_mode = TEST_READ;
			break;
		case 'w':
			open_flags = GFARM_FILE_WRONLY;
			break;
		case 'W':
			test_mode = TEST_WRITE;
			break;
		case 'h':
			host = optarg;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	pathname = argv[0];

	e = gfs_pio_create(pathname, open_flags, 0666, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_create(%s): %s\n",
		    pathname, gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	if (host != NULL) {
		/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
		e = gfs_pio_internal_set_view_section(gf, host);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr,
			    "gfs_pio_internal_set_view_section(%s): %s\n",
			    host, gfarm_error_string(e));
			return (EXIT_FAILURE);
		}
	}
	switch (test_mode) {
	case TEST_NOP:
		break;
	case TEST_READ:
		e = gfs_pio_read(gf, buf, sizeof buf, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfs_pio_read(%zd): %s\n",
			    sizeof buf, gfarm_error_string(e));
			return (EXIT_FAILURE);
		}
		break;
	case TEST_WRITE:
		while ((sz = read(0, buf, sizeof buf)) > 0) {
			e = gfs_pio_write(gf, buf, sz, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_pio_write(%zd): %s\n",
				    sz, gfarm_error_string(e));
				return (EXIT_FAILURE);
			}
		}
		break;
	}
	e = gfs_pio_stat(gf, &st1);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_stat(): %s\n", gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	e = gfs_pio_close(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_pio_close(): %s\n",
		    gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	e = gfs_stat(pathname, &st2);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_stat(): %s\n", gfarm_error_string(e));
		return (EXIT_FAILURE);
	}

	failed = 0;
	if (test_atime)
		failed |=
		    timespec_compare(&st1.st_atimespec, &st2.st_atimespec,
		    "atime");
	if (test_ctime)
		failed |=
		    timespec_compare(&st1.st_ctimespec, &st2.st_ctimespec,
		    "ctime");
	if (test_mtime) /* SF.net #942 */
		failed |=
		    timespec_compare(&st1.st_mtimespec, &st2.st_mtimespec,
		    "mtime");
	if (failed)
		return (EXIT_FAILURE);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

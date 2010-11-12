/*
 * $Id$
 */

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gfarm/gfarm.h>

int node_index = -1;

#ifdef i386

typedef unsigned long long timerval_t;
double timerval_calibration;

unsigned long long
get_cycles(void)
{
	unsigned long long rv;

	__asm __volatile("rdtsc" : "=A" (rv));
	return (rv);
}

#define gettimerval(tp)		(*(tp) = get_cycles())
#define timerval_second(tp)	(*(tp) * timerval_calibration)
#define timerval_sub(t1p, t2p)	((*(t1p) - *(t2p)) * timerval_calibration)

void
timerval_calibrate(void)
{
	timerval_t t1, t2;
	struct timeval s1, s2;

	/* warming up */
	gettimerval(&t1);
	gettimeofday(&s1, NULL);

	gettimerval(&t1);
	gettimeofday(&s1, NULL);
	sleep(3);
	gettimerval(&t2);
	gettimeofday(&s2, NULL);

	timerval_calibration = 
		((s2.tv_sec - s1.tv_sec) +
		 (s2.tv_usec - s1.tv_usec) * .000001) /
		(t2 - t1);

	fprintf(stderr, "[%03d] timer/sec=%g %s\n",
		node_index, 1.0 / timerval_calibration,
		gfarm_host_get_self_name());
}

#else /* gettimeofday */

typedef struct timeval timerval_t;

#define gettimerval(t1)		gettimeofday(t1, NULL)
#define timerval_second(t1)	((double)(t1)->tv_sec \
				 + (double)(t1)->tv_usec * .000001)
#define timerval_sub(t1, t2)	\
	(((double)(t1)->tv_sec - (double)(t2)->tv_sec)	\
	+ ((double)(t1)->tv_usec - (double)(t2)->tv_usec) * .000001)

void
timerval_calibrate(void)
{}

#endif

int tm_write_write_measured = 0;
timerval_t tm_write_open_0, tm_write_open_1;
timerval_t tm_write_write_0, tm_write_write_1;
timerval_t tm_write_write_all_0, tm_write_write_all_1;
timerval_t tm_write_sync_0, tm_write_sync_1;
timerval_t tm_write_close_0, tm_write_close_1;

int tm_read_read_measured = 0;
timerval_t tm_read_open_0, tm_read_open_1;
timerval_t tm_read_read_0, tm_read_read_1;
timerval_t tm_read_read_all_0, tm_read_read_all_1;
timerval_t tm_read_close_0, tm_read_close_1;

char *program_name = "thput-gfpio";

#define MAX_BUFFER_SIZE	(1024*1024)

char buffer[MAX_BUFFER_SIZE];

void
initbuffer(void)
{
	int i;

	for (i = 0; i < MAX_BUFFER_SIZE; i++)
		buffer[i] = i;
}

void
writetest(char *ofile, int buffer_size, off_t file_size)
{
	GFS_File gf;
	gfarm_error_t e;
	int rv;
	off_t residual;

	gettimerval(&tm_write_open_0);
	e = gfs_pio_create(ofile, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	    &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] cannot open %s: %s on %s\n",
			node_index, ofile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#if 0 /* not yet in gfarm v2 */
	e = gfs_pio_set_view_local(gf, GFARM_FILE_SEQUENTIAL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s on %s\n",
			node_index, ofile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#endif /* not yet in gfarm v2 */
	gettimerval(&tm_write_open_1);
	gettimerval(&tm_write_write_all_0);
	for (residual = file_size; residual > 0; residual -= rv) {
		if (!tm_write_write_measured) {
			tm_write_write_measured = 1;
			gettimerval(&tm_write_write_0);
			e = gfs_pio_write(gf, buffer,
				buffer_size <= residual ?
				buffer_size : residual,
				&rv);
			gettimerval(&tm_write_write_1);
		} else {
			e = gfs_pio_write(gf, buffer,
				buffer_size <= residual ?
				buffer_size : residual,
				&rv);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "[%03d] write test: %s on %s\n",
				node_index, gfarm_error_string(e),
				gfarm_host_get_self_name());
			break;
		}
		if (rv != (buffer_size <= residual ? buffer_size : residual))
			break;
	}
	gettimerval(&tm_write_write_all_1);
	if (residual > 0) {
		fprintf(stderr, "[%03d] write test failed, residual = %ld on %s\n",
			node_index, (long)residual, gfarm_host_get_self_name());
	}
	gettimerval(&tm_write_sync_0);
 	e = gfs_pio_sync(gf);
	gettimerval(&tm_write_sync_1);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] write test sync failed: %s on %s\n",
			node_index, gfarm_error_string(e),
			gfarm_host_get_self_name());
	}
	gettimerval(&tm_write_close_0);
	e = gfs_pio_close(gf);
	gettimerval(&tm_write_close_1);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] write test close failed: %s on %s\n",
			node_index, gfarm_error_string(e),
			gfarm_host_get_self_name());
	}
}

off_t
readtest(char *ifile, int buffer_size, off_t file_size)
{
	GFS_File gf;
	struct gfs_stat status;
	gfarm_error_t e;
	int rv;
	off_t residual;

	e = gfs_stat(ifile, &status);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] stat(%s): %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
	if (file_size <= 0)
		file_size = status.st_size;
	if (file_size > status.st_size)
		file_size = status.st_size;
	gfs_stat_free(&status);

	gettimerval(&tm_read_open_0);
	e = gfs_pio_open(ifile, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] cannot open %s: %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#if 0 /* not yet in gfarm v2 */
	e = gfs_pio_set_view_local(gf, GFARM_FILE_SEQUENTIAL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#endif /* not yet in gfarm v2 */
	gettimerval(&tm_read_open_1);

	gettimerval(&tm_read_read_all_0);
	for (residual = file_size; residual > 0; residual -= rv) {
		if (!tm_read_read_measured) {
			tm_read_read_measured = 1;
			gettimerval(&tm_read_read_0);
			e = gfs_pio_read(gf, buffer,
				buffer_size <= residual ?
				buffer_size : residual,
				&rv);
			gettimerval(&tm_read_read_1);
		} else {
			e = gfs_pio_read(gf, buffer,
				buffer_size <= residual ?
				buffer_size : residual,
				&rv);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "[%03d] read test: %s on %s\n",
				node_index, gfarm_error_string(e),
				gfarm_host_get_self_name());
			break;
		}
		if (rv == 0)
			break;
		if (rv != buffer_size && rv != residual)
			break;
	}
	gettimerval(&tm_read_read_all_1);
	if (residual > 0) {
		fprintf(stderr, "[%03d] read test failed, residual = %ld on %s\n",
			node_index, (long)residual, gfarm_host_get_self_name());
	}
	gettimerval(&tm_read_close_0);
	e = gfs_pio_close(gf);
	gettimerval(&tm_read_close_1);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] read test close failed: %s on %s\n",
			node_index, gfarm_error_string(e),
			gfarm_host_get_self_name());
	}
	return (file_size - residual);
}

off_t
copytest(char *ifile, char *ofile, int buffer_size, off_t file_size)
{
	GFS_File igf, ogf;
	struct gfs_stat status;
	gfarm_error_t e;
	int rv, osize;
	off_t residual;

	e = gfs_stat(ifile, &status);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] stat(%s): %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
	if (file_size <= 0)
		file_size = status.st_size;
	if (file_size > status.st_size)
		file_size = status.st_size;
	gfs_stat_free(&status);

	e = gfs_pio_open(ifile, GFARM_FILE_RDONLY, &igf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] cannot open %s: %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#if 0 /* not yet in gfarm v2 */
	e = gfs_pio_set_view_local(igf, GFARM_FILE_SEQUENTIAL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s on %s\n",
			node_index, ifile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#endif /* not yet in gfarm v2 */
	e = gfs_pio_create(ofile, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	    &ogf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] cannot open %s: %s on %s\n",
			node_index, ofile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#if 0 /* not yet in gfarm v2 */
	e = gfs_pio_set_view_local(ogf, GFARM_FILE_SEQUENTIAL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s on %s\n",
			node_index, ofile, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
#endif /* not yet in gfarm v2 */

	for (residual = file_size; residual > 0; residual -= rv) {
		e = gfs_pio_read(igf, buffer, buffer_size, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "[%03d] copytest read: %s on %s\n",
				node_index, gfarm_error_string(e),
				gfarm_host_get_self_name());
			break;
		}
		if (rv == 0)
			break;
		if (rv != buffer_size && rv != residual)
			break;

		osize = rv;
		e = gfs_pio_write(ogf, buffer, osize, &rv);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "[%03d] copytest write: %s on %s\n",
				node_index, gfarm_error_string(e),
				gfarm_host_get_self_name());
			break;
		}
		if (rv != osize)
			break;
	}
	if (residual > 0) {
		fprintf(stderr, "[%03d] copy test failed, residual = %ld on %s\n",
			node_index, (long)residual, gfarm_host_get_self_name());
	}
	e = gfs_pio_close(ogf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] copy test write close failed: %s on %s\n",
			node_index, gfarm_error_string(e),
			gfarm_host_get_self_name());
	}
	e = gfs_pio_close(igf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "[%03d] copy test read close failed: %s on %s\n",
			node_index, gfarm_error_string(e),
			gfarm_host_get_self_name());
	}
	return (file_size - residual);
}

double
timeval_sub(struct timeval *t1, struct timeval *t2)
{
	return ((t1->tv_sec + t1->tv_usec * .000001) -
		(t2->tv_sec + t2->tv_usec * .000001));
}

enum testmode { TESTMODE_WRITE, TESTMODE_READ, TESTMODE_COPY };
#define FLAG_MEASURE_PRIMITIVES	1

void
test(enum testmode test_mode, char *file1, char *file2,
     int buffer_size, off_t file_size, int flags)
{
	struct timeval t1, t2;
	char *label;

	gettimeofday(&t1, NULL);
	switch (test_mode) {
	case TESTMODE_WRITE:
		writetest(file1, buffer_size, file_size);
		label = "write";
		break;
	case TESTMODE_READ:
		file_size = readtest(file1, buffer_size, file_size);
		label = "read";
		break;
	case TESTMODE_COPY:
		file_size = copytest(file1, file2, buffer_size, file_size);
		label = "copy";
		break;
	default:
		fprintf(stderr, "[%03d] ??? wrong test_mode: %d\n",
			node_index, test_mode);
		exit(1);
	}
	gettimeofday(&t2, NULL);

	printf("[%03d] %" GFARM_PRId64 " %7d %-5s %10.0f %s\n",
	       node_index, (gfarm_off_t)file_size,
	       buffer_size, label,
	       file_size / timeval_sub(&t2, &t1), gfarm_host_get_self_name());
	fflush(stdout);

	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0) {
		fprintf(stderr, "[%03d] %" GFARM_PRId64 " %7d %-5s",
		    node_index, (gfarm_off_t)file_size,
		    buffer_size, label);
		if (test_mode == TESTMODE_WRITE)
			fprintf(stderr, " %11g %11g %11g %11g %11g\n",
			    timerval_sub(&tm_write_open_1, &tm_write_open_0),
			    timerval_sub(&tm_write_write_1, &tm_write_write_0),
			    timerval_sub(&tm_write_write_all_1,
					 &tm_write_write_all_0),
			    timerval_sub(&tm_write_sync_1, &tm_write_sync_0),
			    timerval_sub(&tm_write_close_1, &tm_write_close_0)
			);
		if (test_mode == TESTMODE_READ)
			fprintf(stderr, " %11g %11g %11g %11g\n",
			    timerval_sub(&tm_read_open_1, &tm_read_open_0),
			    timerval_sub(&tm_read_read_1, &tm_read_read_0),
			    timerval_sub(&tm_read_read_all_1,
					 &tm_read_read_all_0),
			    timerval_sub(&tm_read_close_1, &tm_read_close_0)
			);
		tm_write_write_measured = tm_read_read_measured = 0;
	}
}

#define DEFAULT_FILE_SIZE 1024

int
main(int argc, char **argv)
{
	char *file1 = "test.file1";
	char *file2 = "test.file2";
	int c, buffer_size = 1024 * 1024;
	off_t file_size = -1;
	enum testmode test_mode = TESTMODE_WRITE;
	int flags = 0;
	gfarm_error_t e;

	if (argc > 0)
		program_name = argv[0];

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initalize(): %s\n",
			program_name, gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "b:s:wrcm")) != -1) {
		switch (c) {
		case 'b':
			buffer_size = strtol(optarg, NULL, 0);
			if (buffer_size > MAX_BUFFER_SIZE) {
				fprintf(stderr, "%s: \"-b %d\" is too big\n",
					program_name, buffer_size);
				exit(1);
			}
			break;
		case 's':
			file_size = strtol(optarg, NULL, 0);
			break;
		case 'w':
			test_mode = TESTMODE_WRITE;
			break;
		case 'r':
			test_mode = TESTMODE_READ;
			break;
		case 'c':
			test_mode = TESTMODE_COPY;
			break;
		case 'm':
			flags |= FLAG_MEASURE_PRIMITIVES;
			break;
		case '?':
		default:
			fprintf(stderr,
				"Usage: %s [options]"
				" [create-file [copy-file]]\n"
				"options:\n"
				"\t-b block-size\n"
				"\t-s file-size\n"
				"\t-w			: write test\n"
				"\t-r			: read test\n"
				"\t-c			: copy test\n",
				program_name);
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

#if 0 /* not yet in gfarm v2 */
	e = gfs_pio_get_node_rank(&node_index);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfs_pio_get_node_rank(): %s\n",
			program_name, e);
		exit(1);
	}
#else
	node_index = 0;
#endif /* not yet in gfarm v2 */
	if (argc > 0)
		file1 = argv[0];
	if (argc > 1)
		file2 = argv[1];

	if (flags & FLAG_MEASURE_PRIMITIVES)
		timerval_calibrate();

	if (file_size == -1 && test_mode == TESTMODE_WRITE)
		file_size = DEFAULT_FILE_SIZE;

	file_size *= 1024 * 1024;
	initbuffer();

	test(test_mode, file1, file2, buffer_size, file_size, flags);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminiate(): %s on %s\n",
			program_name, gfarm_error_string(e),
			gfarm_host_get_self_name());
		exit(1);
	}
	return (0);
}

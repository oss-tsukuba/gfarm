#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <gfarm/gfarm.h>

double timerval_calibration;

#ifdef i386

typedef unsigned long long timerval_t;

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

	gettimerval(&t1);
	gettimeofday(&s1, NULL);
	sleep(10);
	gettimerval(&t2);
	gettimeofday(&s2, NULL);

	timerval_calibration = 
		(t2 - t1) / (
		(s2.tv_sec - s1.tv_sec) +
		(s2.tv_usec - s1.tv_usec) * .000001);
	timerval_calibration = 1.0 / timerval_calibration;
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
{
    timerval_calibration = 1.0;
}

#endif

int tm_write_write_measured = 0;
timerval_t tm_write_open_0, tm_write_open_1;
timerval_t tm_write_write_0, tm_write_write_1;
timerval_t tm_write_close_0, tm_write_close_1;

int tm_read_read_measured = 0;
timerval_t tm_read_open_0, tm_read_open_1;
timerval_t tm_read_read_0, tm_read_read_1;
timerval_t tm_read_close_0, tm_read_close_1;

char *program_name = "thput-gfpio";
int node_index = -1, total_nodes = -1;

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
	char *e;
	int rv;
	off_t residual;

	gettimerval(&tm_write_open_0);
	e = gfs_pio_create(ofile, GFARM_FILE_WRONLY, 0666, &gf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] cannot open %s: %s\n",
			node_index, ofile, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(gf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s\n",
			node_index, ofile, e);
		exit(1);
	}
	gettimerval(&tm_write_open_1);
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
		if (e != NULL) {
			fprintf(stderr, "[%03d] write test: %s\n",
				node_index, e);
			break;
		}
		assert(rv ==
		       (buffer_size <= residual ? buffer_size : residual));
	}
	if (residual > 0) {
		fprintf(stderr, "[%03d] write test failed, residual = %ld\n",
			node_index, (long)residual);
	}
	gettimerval(&tm_write_close_0);
	e = gfs_pio_close(gf);
	gettimerval(&tm_write_close_1);
	if (e != NULL) {
		fprintf(stderr, "[%03d] write test close failed: %s\n",
			node_index, e);
	}
}

void
readtest(char *ifile, int buffer_size, off_t file_size)
{
	GFS_File gf;
	char *e;
	int rv;
	off_t residual;

	gettimerval(&tm_read_open_0);
	e = gfs_pio_open(ifile, GFARM_FILE_RDONLY, &gf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] cannot open %s: %s\n",
			node_index, ifile, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(gf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s\n",
			node_index, ifile, e);
		exit(1);
	}
	gettimerval(&tm_read_open_1);
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
		if (e != NULL) {
			fprintf(stderr, "[%03d] read test: %s\n",
				node_index, e);
			break;
		}
		if (rv == 0)
			break;
		assert(rv == buffer_size || rv == residual);
	}
	if (residual > 0) {
		fprintf(stderr, "[%03d] read test failed, residual = %ld\n",
			node_index, (long)residual);
	}
	gettimerval(&tm_read_close_0);
	e = gfs_pio_close(gf);
	gettimerval(&tm_read_close_1);
	if (e != NULL) {
		fprintf(stderr, "[%03d] read test close failed: %s\n",
			node_index, e);
	}
}

void
copytest(char *ifile, char *ofile, int buffer_size, off_t file_size)
{
	GFS_File igf, ogf;
	char *e;
	int rv, osize;
	off_t residual;

	e = gfs_pio_open(ifile, GFARM_FILE_RDONLY, &igf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] cannot open %s: %s\n",
			node_index, ifile, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(igf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s\n",
			node_index, ifile, e);
		exit(1);
	}
	e = gfs_pio_create(ofile, GFARM_FILE_WRONLY, 0666, &ogf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] cannot open %s: %s\n",
			node_index, ofile, e);
		exit(1);
	}
	e = gfs_pio_set_view_local(ogf, GFARM_FILE_SEQUENTIAL);
	if (e != NULL) {
		fprintf(stderr, "[%03d] set_view_local(%s): %s\n",
			node_index, ofile, e);
		exit(1);
	}
	for (residual = file_size; residual > 0; residual -= rv) {
		e = gfs_pio_read(igf, buffer, buffer_size, &rv);
		if (e != NULL) {
			fprintf(stderr, "[%03d] copytest read: %s\n",
				node_index, e);
			break;
		}
		if (rv == 0)
			break;
		assert(rv == buffer_size || rv == residual);

		osize = rv;
		e = gfs_pio_write(ogf, buffer, osize, &rv);
		if (e != NULL) {
			fprintf(stderr, "[%03d] copytest write: %s\n",
				node_index, e);
			break;
		}
		assert(rv == osize);
	}
	if (residual > 0) {
		fprintf(stderr, "[%03d] copy test failed, residual = %ld\n",
			node_index, (long)residual);
	}
	e = gfs_pio_close(ogf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] copy test write close failed: %s\n",
			node_index, e);
	}
	e = gfs_pio_close(igf);
	if (e != NULL) {
		fprintf(stderr, "[%03d] copy test read close failed: %s\n",
			node_index, e);
	}
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
		readtest(file1, buffer_size, file_size);
		label = "read";
		break;
	case TESTMODE_COPY:
		copytest(file1, file2, buffer_size, file_size);
		label = "copy";
		break;
	default:
		fprintf(stderr, "[%03d] ??? wrong test_mode: %d\n",
			node_index, test_mode);
		exit(1);
	}
	gettimeofday(&t2, NULL);

	printf("[%03d] %" PR_FILE_OFFSET " %7d %-5s %10.0f %s\n",
	       node_index, CAST_PR_FILE_OFFSET (file_offset_t)file_size,
	       buffer_size, label,
	       file_size / timeval_sub(&t2, &t1), gfarm_host_get_self_name());
	fflush(stdout);

	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0) {
		fprintf(stderr, "[%03d] %" PR_FILE_OFFSET " %7d %-5s",
		    node_index, CAST_PR_FILE_OFFSET (file_offset_t)file_size,
		    buffer_size, label);
		if (test_mode == TESTMODE_WRITE)
			fprintf(stderr, " %g %g %g",
			    timerval_sub(&tm_write_open_1, &tm_write_open_0),
			    timerval_sub(&tm_write_write_1, &tm_write_write_0),
			    timerval_sub(&tm_write_close_1, &tm_write_close_0)
			);
		if (test_mode == TESTMODE_READ)
			fprintf(stderr, " %g %g %g",
			    timerval_sub(&tm_read_open_1, &tm_read_open_0),
			    timerval_sub(&tm_read_read_1, &tm_read_read_0),
			    timerval_sub(&tm_read_close_1, &tm_read_close_0)
			);
		fprintf(stderr, " %s\n", gfarm_host_get_self_name());
		tm_write_write_measured = tm_read_read_measured = 0;
	}
}

int
main(int argc, char **argv)
{
	char *file1 = "gfarm:test.file1";
	char *file2 = "gfarm:test.file2";
	int c, buffer_size = 1024 * 1024;
	off_t file_size = 1024;
	enum testmode test_mode = TESTMODE_WRITE;
	int flags = 0;
	char *e;

	if (argc > 0)
		program_name = argv[0];
	while ((c = getopt(argc, argv, "I:N:b:s:wrcmS:")) != -1) {
		switch (c) {
		case 'I':
			node_index = strtol(optarg, NULL, 0);
			break;
		case 'N':
			total_nodes = strtol(optarg, NULL, 0);
			break;
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
		case 'S':
			/*
			 * This option is provided to give scheduling hint
			 * to `gfrsh'. Benchmark itself just ignores `optarg'.
			 */
			break;
		case '?':
		default:
			fprintf(stderr,
				"Usage: gfrsh %s [options]"
				" [gfarm_url1 [gfarm_url2]]\n"
				"options:\n"
				"\t-b block-size\n"
				"\t-s file-size\n"
				"\t-w			: write test\n"
				"\t-r			: read test\n"
				"\t-c			: copy test\n"
				"\t-S gfarm_url		: scheduling hint\n",
				program_name);
			exit(1);
		}
	}
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "[%03d] %s: gfarm_initalize(): %s\n",
			node_index, program_name, e);
		exit(1);
	}
	argc -= optind;
	argv += optind;

	if (node_index < 0) {
		fprintf(stderr, "%s: missing node index, use -I option.\n",
		program_name);
		exit(1);
	}
	if (total_nodes <= 0) {
		fprintf(stderr,
			"%s: missing total node number, use -N option.\n",
			program_name);
		exit(1);
	}
	
	gfs_pio_set_local(node_index, total_nodes);

	if (argc > 0)
		file1 = argv[0];
	if (argc > 1)
		file2 = argv[1];

	if (flags & FLAG_MEASURE_PRIMITIVES) {
		timerval_calibrate();
		fprintf(stderr, "[%03d] timer/sec=%g %s\n",
			node_index, 1.0 / timerval_calibration,
			gfarm_host_get_self_name());
	}

	file_size *= 1024 * 1024;
	initbuffer();

	test(test_mode, file1, file2, buffer_size, file_size, flags);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_terminiate(): %s\n",
			program_name, e);
		exit(1);
	}
	return (0);
}

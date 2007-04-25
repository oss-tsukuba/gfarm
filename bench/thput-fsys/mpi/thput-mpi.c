/*
 * $Id: thput-fsys.c 3693 2007-04-17 08:04:18Z tatebe $
 */

#include "config.h"

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <mpi.h>

int node_index = -1;
int node_size = 0;
char hostname[MPI_MAX_PROCESSOR_NAME];

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

	fprintf(stderr, "timer/sec=%g\n", 1.0 / timerval_calibration);
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

/* Linux stupidly requires 512byte aligned buffer for raw device access. */
#define ALIGNMENT	512

char *buffer;

void *
alloc_aligned_memory(size_t size, int alignment)
{
	char *p = malloc(size + alignment - 1);

	if (p == NULL) {
		fprintf(stderr, "no memory for %ld bytes\n",
			(long)size + alignment - 1);
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	if (((long)p & (alignment - 1)) != 0)
		p += alignment - ((long)p & (alignment - 1));
	return (p);
}

void
initbuffer(int buffer_size)
{
	int i;

	for (i = 0; i < buffer_size; i++)
		buffer[i] = i;
}

void
writetest(char *ofile, int buffer_size, off_t file_size)
{
	int fd, rv;
	off_t residual;

	gettimerval(&tm_write_open_0);
	fd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	gettimerval(&tm_write_open_1);
	if (fd == -1) {
		perror(ofile);
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	gettimerval(&tm_write_write_all_0);
	for (residual = file_size; residual > 0; residual -= rv) {
		if (!tm_write_write_measured) {
			tm_write_write_measured = 1;
			gettimerval(&tm_write_write_0);
			rv = write(fd, buffer,
			   buffer_size <= residual ? buffer_size : residual);
			gettimerval(&tm_write_write_1);
		} else {
			rv = write(fd, buffer,
			   buffer_size <= residual ? buffer_size : residual);
		}
		if (rv == -1) {
			perror("write test");
			break;
		}
		if (rv != (buffer_size <= residual ? buffer_size : residual))
			break;
	}
	gettimerval(&tm_write_write_all_1);
	if (residual > 0) {
		fprintf(stderr, "write test failed, residual = %ld\n",
			(long)residual);
	}
	gettimerval(&tm_write_sync_0);
 	rv = fsync(fd);
	gettimerval(&tm_write_sync_1);
	if (rv == -1)
		perror("write test fsync failed");
	gettimerval(&tm_write_close_0);
	rv = close(fd);
	gettimerval(&tm_write_close_1);
	if (rv == -1)
		perror("write test close failed");
}

void
readtest(char *ifile, int buffer_size, off_t file_size)
{
	int fd, rv;
	off_t residual;

	gettimerval(&tm_read_open_0);
	fd = open(ifile, O_RDONLY);
	gettimerval(&tm_read_open_1);
	if (fd == -1) {
		perror(ifile);
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	gettimerval(&tm_read_read_all_0);
	for (residual = file_size; residual > 0; residual -= rv) {
		if (!tm_read_read_measured) {
			tm_read_read_measured = 1;
			gettimerval(&tm_read_read_0);
			rv = read(fd, buffer,
			  buffer_size <= residual ? buffer_size : residual);
			gettimerval(&tm_read_read_1);
		} else {
			rv = read(fd, buffer,
			  buffer_size <= residual ? buffer_size : residual);
		}
		if (rv == 0)
			break;
		if (rv == -1) {
			perror("read test");
			break;
		}
		if (rv != buffer_size && rv != residual)
			break;
	}
	gettimerval(&tm_read_read_all_1);
	if (residual > 0) {
		fprintf(stderr, "read test failed, residual = %ld\n",
			(long)residual);
	}
	gettimerval(&tm_read_close_0);
	rv = close(fd);
	gettimerval(&tm_read_close_1);
	if (rv == -1)
		perror("read test closed failed");
}

void
copytest(char *ifile, char *ofile, int buffer_size, off_t file_size)
{
	int ifd, ofd;
	int rv, osize;
	off_t residual;

	ifd = open(ifile, O_RDONLY);
	if (ifd == -1) {
		perror(ifile);
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	ofd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (ofd == -1) {
		perror(ofile);
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	for (residual = file_size; residual > 0; residual -= rv) {
		rv = read(ifd, buffer,
			  buffer_size <= residual ? buffer_size : residual);
		if (rv == 0)
			break;
		if (rv == -1) {
			perror("copytest read");
			break;
		}
		if (rv != buffer_size && rv != residual)
			break;

		osize = rv;
		rv = write(ofd, buffer, osize);
		if (rv == -1) {
			perror("copytest write");
			break;
		}
		if (rv != osize)
			break;
	}
	if (residual > 0) {
		fprintf(stderr, "copy test failed, residual = %ld\n",
			(long)residual);
	}
	if (close(ofd) == -1)
		perror("copy test write close failed");
	if (close(ifd) == -1)
		perror("copy test read close failed");
}

double
timeval_sub(struct timeval *t1, struct timeval *t2)
{
	return ((t1->tv_sec + t1->tv_usec * .000001) -
		(t2->tv_sec + t2->tv_usec * .000001));
}

#define TESTMODE_WRITE	1
#define TESTMODE_READ	2
#define TESTMODE_COPY	4

#define FLAG_MEASURE_PRIMITIVES	2

void
test(int test_mode, char *file1, char *file2, int buffer_size, off_t file_size,
     int flags)
{
	struct timeval t1, t2;
	double etime, gtime;
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
		MPI_Abort(MPI_COMM_WORLD, 1);
	}
	gettimeofday(&t2, NULL);

	etime = timeval_sub(&t2, &t1);

	printf("[%03d] %lld %7d %-5s %10.0f %s\n",
	       node_index, file_size,
	       buffer_size, label, file_size / etime, hostname);
	fflush(stdout);

	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0) {
		fprintf(stderr, "[%03d] %lld %7d %-5s",
		    node_index, file_size, buffer_size, label);
		if (test_mode == TESTMODE_WRITE)
			fprintf(stderr, " %11g %11g %11g %11g\n",
			    timerval_sub(&tm_write_open_1, &tm_write_open_0),
			    timerval_sub(&tm_write_write_1, &tm_write_write_0),
			    timerval_sub(&tm_write_write_all_1,
					 &tm_write_write_all_0),
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

	MPI_Reduce(&etime, &gtime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
	if (node_index == 0)
		printf("TOTAL %lld %7d %-5s %10.0f\n",
		       file_size * node_size,
		       buffer_size, label, file_size / gtime * node_size);
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	char *file1 = "test.file1";
	char *file2 = "test.file2";
	char *pfile1, *pfile2;
	int test_mode = TESTMODE_WRITE;
	int c, hostlen, buffer_size = 64 * 1024;
	off_t file_size = 1024;
	int flags = 0;
#define INT32STRLEN 11	/* max strlen(sprintf(s, "%d", int32)) */

	while ((c = getopt(argc, argv, "b:s:wrcm")) != -1) {
		switch (c) {
		case 'b':
			buffer_size = strtol(optarg, NULL, 0);
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
			timerval_calibrate();
			break;
		case '?':
		default:
			fprintf(stderr,
				"Usage: thput-mpi [options]"
				" [file1 [file2]]\n"
				"options:\n"
				"\t-b block-size\n"
				"\t-s file-size\n"
				"\t-w			: write test\n"
				"\t-r			: read test\n"
				"\t-c			: copy test\n");
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		file1 = argv[0];
	if (argc > 1)
		file2 = argv[1];

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &node_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &node_index);
	MPI_Get_processor_name(hostname, &hostlen);
	
	pfile1 = malloc(strlen(file1) + 2 + INT32STRLEN);
	pfile2 = malloc(strlen(file2) + 2 + INT32STRLEN);
	if (pfile1 == NULL || pfile2 == NULL)
		fprintf(stderr, "no memory\n"), MPI_Abort(MPI_COMM_WORLD, 1);

	sprintf(pfile1, "%s.%06d", file1, node_index);
	sprintf(pfile2, "%s.%06d", file2, node_index);

	buffer = alloc_aligned_memory(buffer_size, ALIGNMENT);

	file_size *= 1024 * 1024;
	initbuffer(buffer_size);

	test(test_mode, pfile1, pfile2, buffer_size, file_size, flags);

	MPI_Finalize();
	return (0);
}

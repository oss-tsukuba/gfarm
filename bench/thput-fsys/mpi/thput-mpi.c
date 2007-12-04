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
alloc_aligned_memory(size_t size, int alignment, MPI_Comm comm)
{
	char *p = malloc(size + alignment - 1);

	if (p == NULL) {
		fprintf(stderr, "no memory for %ld bytes\n",
			(long)size + alignment - 1);
		MPI_Abort(comm, 1);
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
writetest(char *ofile, int buffer_size, off_t file_size, MPI_Comm comm)
{
	int fd, rv;
	off_t residual;

	gettimerval(&tm_write_open_0);
	fd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	gettimerval(&tm_write_open_1);
	if (fd == -1) {
		perror(ofile);
		MPI_Abort(comm, 1);
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
readtest(char *ifile, int buffer_size, off_t file_size, MPI_Comm comm)
{
	int fd, rv;
	off_t residual;

	gettimerval(&tm_read_open_0);
	fd = open(ifile, O_RDONLY);
	gettimerval(&tm_read_open_1);
	if (fd == -1) {
		perror(ifile);
		MPI_Abort(comm, 1);
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
copytest(char *ifile, char *ofile, int buffer_size, off_t file_size,
	MPI_Comm comm)
{
	int ifd, ofd;
	int rv, osize;
	off_t residual;

	ifd = open(ifile, O_RDONLY);
	if (ifd == -1) {
		perror(ifile);
		MPI_Abort(comm, 1);
	}
	ofd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (ofd == -1) {
		perror(ofile);
		MPI_Abort(comm, 1);
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

#define FLAG_REPORT_PERFORMANCE_OF_EACH_PROCESS	1
#define FLAG_MEASURE_PRIMITIVES			2

void
test_title(char *msg, int test_mode, off_t file_size, int flags)
{
	printf("%s with %d MiByte file [KiByte/sec]\n", msg, (int)file_size);
	printf("[%4s] %8s", "RANK", "bufsize");
	if (test_mode & TESTMODE_WRITE)
		printf(" %10s   ", "write");
	if (test_mode & TESTMODE_READ)
		printf(" %10s   ", "read");
	if (test_mode & TESTMODE_COPY)
		printf(" %10s   ", "copy");
	printf("\n");
	fflush(stdout);
}

/*
 * etime[0]    - elapsed time of write test
 * etime[1]    - elapsed time of read test
 * etime[2]    - elapsed time of copy test
 */
void
display_timing(int test_mode, int buffer_size, off_t file_size, int flags,
	double etime[3])
{
	printf("[%04d] %8d %10.0f%3s",
		node_index, buffer_size, file_size / etime[0], "");
	if (test_mode & TESTMODE_READ)
		printf(" %10.0f%3s", file_size / etime[1], "");
	if (test_mode & TESTMODE_COPY)
		printf(" %10.0f%3s", file_size / etime[2], "");
	printf(" %s\n", hostname);
	fflush(stdout);

	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0) {
		fprintf(stderr, "[%04d]", node_index);
		if (test_mode & TESTMODE_WRITE)
			fprintf(stderr, " write %11g %11g %11g %11g",
			    timerval_sub(&tm_write_open_1, &tm_write_open_0),
			    timerval_sub(&tm_write_write_1, &tm_write_write_0),
			    timerval_sub(&tm_write_write_all_1,
					 &tm_write_write_all_0),
			    timerval_sub(&tm_write_close_1, &tm_write_close_0)
			);
		if (test_mode & TESTMODE_READ)
			fprintf(stderr, " read %11g %11g %11g %11g",
			    timerval_sub(&tm_read_open_1, &tm_read_open_0),
			    timerval_sub(&tm_read_read_1, &tm_read_read_0),
			    timerval_sub(&tm_read_read_all_1,
					 &tm_read_read_all_0),
			    timerval_sub(&tm_read_close_1, &tm_read_close_0)
			);
		tm_write_write_measured = tm_read_read_measured = 0;
		fprintf(stderr, "\n");
		fflush(stderr);
	}
}

void
test(int test_mode, char *file1, char *file2, int buffer_size, off_t file_size,
	int flags, MPI_Comm comm)
{
	struct timeval t1, t2, t3, t4, t5, t6;
	double etime[3], gtime[3];

	(void)unlink(file1);
	(void)unlink(file2);

	gettimeofday(&t1, NULL);
	/* TESTMODE_WRITE: writetest is always called. */
	writetest(file1, buffer_size, file_size, comm);
	gettimeofday(&t2, NULL);
	MPI_Barrier(comm);
	gettimeofday(&t3, NULL);
	if (test_mode & TESTMODE_READ)
		readtest(file1, buffer_size, file_size, comm);
	gettimeofday(&t4, NULL);
	MPI_Barrier(comm);
	gettimeofday(&t5, NULL);
	if (test_mode & TESTMODE_COPY)
		copytest(file1, file2, buffer_size, file_size, comm);
	gettimeofday(&t6, NULL);

	etime[0] = timeval_sub(&t2, &t1);
	etime[1] = timeval_sub(&t4, &t3);
	etime[2] = timeval_sub(&t6, &t5);

	file_size /= 1024;
	if ((flags & FLAG_REPORT_PERFORMANCE_OF_EACH_PROCESS) != 0)
		display_timing(
			test_mode, buffer_size, file_size, flags, etime);

	MPI_Reduce(etime, gtime, 3, MPI_DOUBLE, MPI_MAX, 0, comm);
	file_size *= node_size;
	if (node_index == 0) {
		test_title("TOTAL parallel I/O Bandwidth",
			test_mode, file_size / 1024, flags);
		printf("[ ALL] %8d %10.0f%3s",
			buffer_size, file_size / gtime[0], "");
		if (test_mode & TESTMODE_READ)
			printf(" %10.0f%3s", file_size / gtime[1], "");
		if (test_mode & TESTMODE_COPY)
			printf(" %10.0f%3s", file_size / gtime[2], "");
		printf("\n");
		fflush(stdout);
	}

	(void)unlink(file1);
	(void)unlink(file2);
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

	/*
	 * MPI_Init() is required to be called before command-line
	 * option parsing.
	 */
	MPI_Init(&argc, &argv);

	while ((c = getopt(argc, argv, "b:s:wrcvmh?")) != -1) {
		switch (c) {
		case 'b':
			buffer_size = strtol(optarg, NULL, 0);
			break;
		case 's':
			file_size = strtol(optarg, NULL, 0);
			break;
		case 'w':
			test_mode |= TESTMODE_WRITE;
			break;
		case 'r':
			test_mode |= TESTMODE_READ;
			break;
		case 'c':
			test_mode |= TESTMODE_COPY;
			break;
		case 'v':
			flags |= FLAG_REPORT_PERFORMANCE_OF_EACH_PROCESS;
			break;
		case 'm':
			flags |= FLAG_MEASURE_PRIMITIVES;
			timerval_calibrate();
			break;
		case 'h':
		case '?':
		default:
			fprintf(stderr,
				"Usage: thput-mpi [options]"
				" [file1 [file2]]\n"
				"options:\n"
				"\t-b block-size (Byte)\n"
				"\t-s file-size (MiByte)\n"
				"\t-v\t\t: report bandwidth of each process\n"
				"\t-r\t\t: do read test additionally\n"
				"\t-c\t\t: do copy test additionally\n");
			MPI_Finalize();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		file1 = argv[0];
	if (argc > 1)
		file2 = argv[1];

	MPI_Comm_size(MPI_COMM_WORLD, &node_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &node_index);
	MPI_Get_processor_name(hostname, &hostlen);
	
	pfile1 = malloc(strlen(file1) + 2 + INT32STRLEN);
	pfile2 = malloc(strlen(file2) + 2 + INT32STRLEN);
	if (pfile1 == NULL || pfile2 == NULL)
		fprintf(stderr, "no memory\n"), MPI_Abort(MPI_COMM_WORLD, 1);

	sprintf(pfile1, "%s.%06d", file1, node_index);
	sprintf(pfile2, "%s.%06d", file2, node_index);

	buffer = alloc_aligned_memory(buffer_size, ALIGNMENT, MPI_COMM_WORLD);

	if ((flags & FLAG_REPORT_PERFORMANCE_OF_EACH_PROCESS) != 0
	    && node_index == 0)
		test_title("Individual parallel I/O Bandwidth",
			test_mode, file_size, flags);

	file_size *= 1024 * 1024;
	initbuffer(buffer_size);

	test(test_mode, pfile1, pfile2, buffer_size, file_size, flags,
		MPI_COMM_WORLD);

	MPI_Finalize();
	return (0);
}

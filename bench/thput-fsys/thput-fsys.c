#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

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

#define ARRAY_LENGTH(array)	(sizeof(array)/sizeof(array[0]))

/* Linux stupidly requires 512byte aligned buffer for raw device access. */
#define ALIGNMENT	512

#define MAX_BUFFER_SIZE_NUMBER	32

char *buffer;
int max_buffer_size = 0;

void *
alloc_aligned_memory(size_t size, int alignment)
{
	char *p = malloc(size + alignment - 1);

	if (p == NULL) {
		fprintf(stderr, "no memory for %ld bytes\n",
			(long)size + alignment - 1);
		exit(1);
	}
	if (((long)p & (alignment - 1)) != 0)
		p += alignment - ((long)p & (alignment - 1));
	return (p);
}

void
initbuffer(void)
{
	int i;

	for (i = 0; i < max_buffer_size; i++)
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
		exit(1);
	}
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
		assert(rv ==
		       (buffer_size <= residual ? buffer_size : residual));
	}
	if (residual > 0) {
		fprintf(stderr, "write test failed, residual = %ld\n",
			(long)residual);
	}
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
		exit(1);
	}
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
		assert(rv == buffer_size || rv == residual);
	}
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
		exit(1);
	}
	ofd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (ofd == -1) {
		perror(ofile);
		exit(1);
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
		assert(rv == buffer_size || rv == residual);

		osize = rv;
		rv = write(ofd, buffer, osize);
		if (rv == -1) {
			perror("copytest write");
			break;
		}
		assert(rv == osize);
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

#define FLAG_DONT_REMOVE	1
#define FLAG_MEASURE_PRIMITIVES	2

void
test_title(int test_mode, off_t file_size, int flags)
{
	fprintf(stdout, "testing with %d MB file\n", (int)file_size);
	printf("%-8s", "bufsize");
	if (test_mode & TESTMODE_WRITE)
		printf(" %20s", "write [bytes/sec]");
	if (test_mode & TESTMODE_READ)
		printf(" %20s", "read [bytes/sec]");
	if (test_mode & TESTMODE_COPY)
		printf(" %20s", "copy [bytes/sec]");
	printf("\n");
	fflush(stdout);

	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0 &&
	    (test_mode & (TESTMODE_WRITE|TESTMODE_READ)) != 0)
		fprintf(stderr, "timer/sec=%g\n",
			1.0 / timerval_calibration);
}

void
test(int test_mode, char *file1, char *file2, int buffer_size, off_t file_size,
     int flags)
{
	struct timeval t1, t2, t3, t4;

	if ((flags & FLAG_DONT_REMOVE) == 0) {
		if (test_mode & TESTMODE_WRITE)
			unlink(file1);
		if (test_mode & TESTMODE_COPY)
			unlink(file2);
	}

	gettimeofday(&t1, NULL);
	if (test_mode & TESTMODE_WRITE)
		writetest(file1, buffer_size, file_size);
	gettimeofday(&t2, NULL);
	if (test_mode & TESTMODE_READ)
		readtest(file1,buffer_size, file_size);
	gettimeofday(&t3, NULL);
	if (test_mode & TESTMODE_COPY)
		copytest(file1, file2, buffer_size, file_size);
	gettimeofday(&t4, NULL);

	printf("%7d ", buffer_size);
	if (test_mode & TESTMODE_WRITE)
		printf(" %10.0f%10s", file_size / timeval_sub(&t2, &t1), "");
	if (test_mode & TESTMODE_READ)
		printf(" %10.0f%10s", file_size / timeval_sub(&t3, &t2), "");
	if (test_mode & TESTMODE_COPY)
		printf(" %10.0f%10s", file_size / timeval_sub(&t4, &t3), "");
	printf("\n");
	fflush(stdout);

	if ((flags & FLAG_DONT_REMOVE) == 0) {
		if (test_mode & TESTMODE_WRITE)
			unlink(file1);
		if (test_mode & TESTMODE_COPY)
			unlink(file2);
	}
	if ((flags & FLAG_MEASURE_PRIMITIVES) != 0 &&
	    (test_mode & (TESTMODE_WRITE|TESTMODE_READ)) != 0) {
		fprintf(stderr, "%7d ", buffer_size);
		if (test_mode & TESTMODE_WRITE)
			fprintf(stderr, " %g %g %g",
			    timerval_sub(&tm_write_open_1, &tm_write_open_0),
			    timerval_sub(&tm_write_write_1, &tm_write_write_0),
			    timerval_sub(&tm_write_close_1, &tm_write_close_0)
			);
		if (test_mode & TESTMODE_READ)
			fprintf(stderr, " %g %g %g",
			    timerval_sub(&tm_read_open_1, &tm_read_open_0),
			    timerval_sub(&tm_read_read_1, &tm_read_read_0),
			    timerval_sub(&tm_read_close_1, &tm_read_close_0)
			);
		fprintf(stderr, "\n");
		tm_write_write_measured = tm_read_read_measured = 0;
	}
}

int
main(int argc, char **argv)
{
	char *file1 = "test.file1";
	char *file2 = "test.file2";
	int test_mode = 0;
	int c, i, buffer_sizec = 0, buffer_sizes_space[MAX_BUFFER_SIZE_NUMBER];
	static int buffer_sizes_default[] = {
		512,
		1024,
		8 * 1024,
		64 * 1024,
		256 * 1024,
		1024 * 1024,
	};
	int *buffer_sizes;
	off_t file_size = 1024;
	int flags = 0;

	while ((c = getopt(argc, argv, "b:s:wrcmp")) != -1) {
		switch (c) {
		case 'b':
			if (buffer_sizec >= MAX_BUFFER_SIZE_NUMBER) {
				fprintf(stderr,
					"too many -b options (max %d)\n",
					MAX_BUFFER_SIZE_NUMBER);
				exit(1);
			}
			buffer_sizes_space[buffer_sizec] =
			    strtol(optarg, NULL, 0);
			buffer_sizec++;
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
		case 'm':
			flags |= FLAG_MEASURE_PRIMITIVES;
			timerval_calibrate();
			break;
		case 'p':
			flags |= FLAG_DONT_REMOVE;
			break;
		case '?':
		default:
			fprintf(stderr,
				"Usage: thput-fsys [options]"
				" [file1 [file2]]\n"
				"options:\n"
				"\t-b block-size\n"
				"\t-s file-size\n"
				"\t-w			: write test\n"
				"\t-r			: read test\n"
				"\t-c			: copy test\n"
				"\t-p			: don't remove\n");
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		file1 = argv[0];
	if (argc > 1)
		file2 = argv[1];

	if (test_mode == 0)
		test_mode = TESTMODE_WRITE|TESTMODE_READ|TESTMODE_COPY;

	if (buffer_sizec == 0) {
		buffer_sizec = ARRAY_LENGTH(buffer_sizes_default);
		buffer_sizes = buffer_sizes_default;
	} else {
		buffer_sizes = buffer_sizes_space;
	}
	for (i = 0; i < buffer_sizec; i++) {
		if (max_buffer_size < buffer_sizes[i])
			max_buffer_size = buffer_sizes[i];
	}
	buffer = alloc_aligned_memory(max_buffer_size, ALIGNMENT);

	test_title(test_mode, file_size, flags);

	file_size *= 1024 * 1024;
	initbuffer();

	for (i = 0; i < buffer_sizec; i++) {
		test(test_mode, file1, file2,
		     buffer_sizes[i], file_size, flags);
	}
	return (0);
}

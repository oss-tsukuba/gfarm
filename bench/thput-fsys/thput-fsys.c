#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define ARRAY_LENGTH(array)	(sizeof(array)/sizeof(array[0]))

#define MAX_BUFFER_SIZE_NUMBER	32
#define MAX_BUFFER_SIZE		(1024*1024)

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
	int fd = open(ofile, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	int rv;
	off_t residual;

	if (fd == -1) {
		perror(ofile);
		exit(1);
	}
	for (residual = file_size; residual > 0; residual -= rv) {
		rv = write(fd, buffer,
			   buffer_size <= residual ? buffer_size : residual);
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
	if (close(fd) == -1)
		perror("write test close failed");
}

void
readtest(char *ifile, int buffer_size, off_t file_size)
{
	int fd = open(ifile, O_RDONLY);
	int rv;
	off_t residual;

	if (fd == -1) {
		perror(ifile);
		exit(1);
	}
	for (residual = file_size; residual > 0; residual -= rv) {
		rv = read(fd, buffer,
			  buffer_size <= residual ? buffer_size : residual);
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
	if (close(fd) == -1)
		perror("read test closed failed");
}

void
copytest(char *ifile, char *ofile, int buffer_size, off_t file_size)
{
	int ifd, ofd;
	int rv, osize, i;
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
	return ((t1->tv_sec + t1->tv_usec / 1000000.0) -
		(t2->tv_sec + t2->tv_usec / 1000000.0));
}

#define TESTMODE_WRITE	1
#define TESTMODE_READ	2
#define TESTMODE_COPY	4

#define FLAG_DONT_REMOVE	1

void
test_title(int test_mode, off_t file_size)
{
	fprintf(stdout, "testing with %d MB file\n", file_size);
	printf("%-8s", "bufsize");
	if (test_mode & TESTMODE_WRITE)
		printf(" %20s", "write [bytes/sec]");
	if (test_mode & TESTMODE_READ)
		printf(" %20s", "read [bytes/sec]");
	if (test_mode & TESTMODE_COPY)
		printf(" %20s", "copy [bytes/sec]");
	printf("\n");
	fflush(stdout);
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
}

main(int argc, char **argv)
{
	char *file1 = "test.file1";
	char *file2 = "test.file2";
	int test_mode = 0;
	int c, i, buffer_sizec = 0, buffer_sizes[MAX_BUFFER_SIZE_NUMBER];
	static int buffer_sizes_default[] = {
		512,
		1024,
		8 * 1024,
		64 * 1024,
		256 * 1024,
		1024 * 1024,
	};
	off_t file_size = 1024;
	int flag = 0;

	while ((c = getopt(argc, argv, "b:s:wrcp")) != -1) {
		switch (c) {
		case 'b':
			if (buffer_sizec >= MAX_BUFFER_SIZE_NUMBER) {
				fprintf(stderr,
					"too many -b options (max %d)\n",
					MAX_BUFFER_SIZE_NUMBER);
				exit(1);
			}
			buffer_sizes[buffer_sizec] = strtol(optarg, NULL, 0);
			if (buffer_sizes[buffer_sizec] > MAX_BUFFER_SIZE) {
				fprintf(stderr, "-b: %d is too big\n",
					buffer_sizes[buffer_sizec]);
				exit(1);
			}
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
		case 'p':
			flag |= FLAG_DONT_REMOVE;
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

	test_title(test_mode, file_size);

	file_size *= 1024 * 1024;
	initbuffer();

	if (buffer_sizec == 0) {
		for (i = 0; i < ARRAY_LENGTH(buffer_sizes_default); i++)
			test(test_mode, file1, file2,
			     buffer_sizes_default[i], file_size, flag);
	} else {
		for (i = 0; i < buffer_sizec; i++)
			test(test_mode, file1, file2,
			     buffer_sizes[i], file_size, flag);
	}
	return (0);
}

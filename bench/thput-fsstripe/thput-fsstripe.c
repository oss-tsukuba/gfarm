#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

char *program_name = "thput-fsstripe.c";

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) \
    || defined(__bsdi__)
#define strtoll(nptr, endptr, base) strtoq(nptr, endptr, base)
#endif

/* compatibility with the source code of gfsd */
typedef off_t file_offset_t;
#define file_offset_floor(n)	(n)

#define MAX_IOSIZE 524288

int iosize = 4096;
int iosize_alignment = 512;
int iosize_minimum_division = 65536;

int verbose_mode = 0;

void
worker(char *filename,
	off_t offset,
	off_t size,
	off_t interleave_factor,
	off_t full_stripe_size)
{
	off_t chunk_size;
	int rv;
	char buffer[MAX_IOSIZE];
	int fd = open(filename, O_RDONLY);

	if (fd == -1) {
		perror(filename);
		exit(1);
	}
	if (verbose_mode) {
		fprintf(stderr, "off=%lld size=%lld ileave=%lld stripe=%lld\n",
		    offset, size, interleave_factor, full_stripe_size);
	}

	if (lseek(fd, offset, SEEK_SET) == -1) {
		perror("lseek");
		exit(1);
	}
	for (;;) {
		chunk_size = interleave_factor == 0 || size < interleave_factor
		    ? size : interleave_factor;
		for (; chunk_size > 0; chunk_size -= rv, size -= rv) {
			rv = read(fd, buffer, chunk_size < iosize ?
			    chunk_size : iosize);
			if (rv <= 0) {
				if (rv == -1) {
					perror("read");
					exit(1);
				}
				return;
			}
#if 0
			write(STDOUT_FILENO, buffer, rv);
#endif
		}
		if (size <= 0)
			break;
		offset += full_stripe_size;
		if (lseek(fd, offset, SEEK_SET) == -1) {
			perror("lseek");
			exit(1);
		}
	}
}

void
simple_division(char *filename, file_offset_t file_size, int n)
{
	file_offset_t offset = 0, residual = file_size;
	file_offset_t size_per_division = file_offset_floor(file_size / n);
	int i;

	if (file_offset_floor(size_per_division / iosize_alignment) *
	    iosize_alignment != size_per_division) {
		size_per_division = (file_offset_floor(
		    size_per_division / iosize_alignment) + 1) *
		    iosize_alignment;
	}

	for (i = 0; i < n && residual > 0; i++) {
		file_offset_t size = residual <= size_per_division ?
		    residual : size_per_division;

		switch (fork()) {
		case 0:
			worker(filename, offset, size, 0, 0);
			exit(0);
		case -1:
			perror("fork");
			/*FALLTHROUGH*/
		default:
			break;
		}

		offset += size_per_division;
		residual -= size;
	}
}

void
striping(char *filename,
	file_offset_t file_size, int n, int interleave_factor)
{
	file_offset_t full_stripe_size = (file_offset_t)interleave_factor * n;
	file_offset_t stripe_number = file_offset_floor(file_size /
	    full_stripe_size);
	file_offset_t size_per_division = interleave_factor * stripe_number;
	file_offset_t residual = file_size - full_stripe_size * stripe_number;
	file_offset_t chunk_number_on_last_stripe;
	file_offset_t last_chunk_size;
	file_offset_t offset = 0;
	int i;

	if (residual == 0) {
		chunk_number_on_last_stripe = 0;
		last_chunk_size = 0;
	} else {
		chunk_number_on_last_stripe = file_offset_floor(
		    residual / interleave_factor);
		last_chunk_size = residual - 
		    interleave_factor * chunk_number_on_last_stripe;
	}

	for (i = 0; i < n; i++) {
		file_offset_t size = size_per_division;

		if (i < chunk_number_on_last_stripe)
			size += interleave_factor;
		else if (i == chunk_number_on_last_stripe)
			size += last_chunk_size;
		if (size <= 0)
			break;

		switch (fork()) {
		case 0:
			worker(filename, offset, size, interleave_factor,
			    full_stripe_size);
			exit(0);
		case -1:
			perror("fork");
			/*FALLTHROUGH*/
		default:
			break;
		}

		offset += interleave_factor;
	}
}

void
limit_division(int *ndivisionsp, file_offset_t file_size)
{
	int ndivisions = *ndivisionsp;

	/* do not divide too much */
	if (ndivisions > file_size / iosize_minimum_division) {
		ndivisions = file_size / iosize_minimum_division;
		if (ndivisions == 0)
			ndivisions = 1;
	}
	*ndivisionsp = ndivisions;
}

double
timeval_sub(struct timeval *t1, struct timeval *t2)
{
	return ((t1->tv_sec + t1->tv_usec * .000001) -
		(t2->tv_sec + t2->tv_usec * .000001));
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [options] <input_file>\n", program_name);
	fprintf(stderr, "\t-a <iosize_alignment>\n");
	fprintf(stderr, "\t-b <blocking_size>\n");
	fprintf(stderr, "\t-g <full_stripe_size> (gap between stripe)\n");
	fprintf(stderr, "\t-i <stripe_unit_size> (interleave factor)\n");
	fprintf(stderr, "\t-m <minimum_division_size>\n");
	fprintf(stderr, "\t-o <offset> (slave mode)\n");
	fprintf(stderr, "\t-p <parallelism>\n");
	fprintf(stderr, "\t-s <file_size>\n");
	fprintf(stderr, "\t-v\t(verbose)\n");
	exit(1);
}

extern char *optarg;
extern int optind;

int
main(int argc, char **argv)
{
	int full_stripe_size = 0;
	int interleave_factor = 0;
	int slave_mode = 0;
	file_offset_t offset = 0;
	int parallelism = 1;
	file_offset_t file_size = -1;
	int ch, sv;
	char *filename;
	struct stat s;
	struct timeval t1, t2;

	if (argc > 0)
		program_name = argv[0];
	while ((ch = getopt(argc, argv, "a:b:g:i:m:o:p:s:v")) != -1) {
		switch (ch) {
		case 'a':
			iosize_alignment = strtoll(optarg, NULL, 0);
			break;
		case 'b':
			iosize = strtoll(optarg, NULL, 0);
			break;
		case 'g':
			full_stripe_size = strtoll(optarg, NULL, 0);
			break;
		case 'i':
			interleave_factor = strtoll(optarg, NULL, 0);
			break;
		case 'm':
			iosize_minimum_division = strtoll(optarg, NULL, 0);
			break;
		case 'o':
			slave_mode = 1;
			offset = strtoll(optarg, NULL, 0);
			break;
		case 'p':
			parallelism = strtoll(optarg, NULL, 0);
			break;
		case 's':
			file_size = strtoll(optarg, NULL, 0);
			break;
		case 'v':
			verbose_mode = 1;
			break;
		case '?':
			/*FALLTHROUGH*/
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr, "<input_file_name> is needed\n");
		exit(1);
	}
	filename = argv[0];

	if (file_size == -1) {
		if (stat(filename, &s) == -1) {
			perror("stat");
			exit(1);
		}
		if (!S_ISREG(s.st_mode)) {
			fprintf(stderr, "%s: not a file\n", filename);
			exit(1);
		}
		file_size = s.st_size;
	}

	if (full_stripe_size != 0 && interleave_factor != 0 &&
	    full_stripe_size != interleave_factor * parallelism) {
		fprintf(stderr, "inconsistency between -g and -i\n");
		exit(1);
	}

	limit_division(&parallelism, file_size);

	if (slave_mode) {
		worker(filename, offset, file_size,
		    interleave_factor, full_stripe_size);
		return (0);
	}

	gettimeofday(&t1, NULL);
	if (full_stripe_size == 0 && interleave_factor == 0) {
		simple_division(filename, file_size, parallelism);
	} else if (interleave_factor != 0) {
		striping(filename, file_size, parallelism, interleave_factor);
	} else {
		interleave_factor = full_stripe_size / parallelism;
		if ((interleave_factor / iosize_alignment) * iosize_alignment
		    != interleave_factor)
			interleave_factor =
			    ((interleave_factor / iosize_alignment) + 1) *
			    iosize_alignment;
		striping(filename, file_size, parallelism, interleave_factor);
	}
	while (waitpid(-1, &sv, 0) != -1 || errno != ECHILD)
		;
	gettimeofday(&t2, NULL);
	printf("%.0f\n", file_size / timeval_sub(&t2, &t1));

	return (0);
}

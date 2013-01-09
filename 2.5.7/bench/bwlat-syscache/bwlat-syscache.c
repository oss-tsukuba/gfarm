/*
 * A little disk benchmark originally written by Bruce Evans <bde@FreeBSD.org>.
 */

/*
 * Message-Id: <199604020202.SAA23506@freefall.freebsd.org>
 * X-Authentication-Warning: freefall.freebsd.org: Host localhost.cdrom.com [127.0.0.1] didn't use HELO protocol
 * To: "Michael L. VanLoon -- HeadCandy.com" <michaelv@HeadCandy.com>
 * cc: John M Vinopal <banshee@gabriella.resort.com>, port-i386@NetBSD.ORG
 * Subject: Re: scsi adaptor speeds etc 
 * In-reply-to: Your message of "Mon, 01 Apr 1996 09:43:45 PST."
 *              <199604011743.JAA19016@MindBender.HeadCandy.com> 
 * Date: Mon, 01 Apr 1996 18:02:23 -0800
 * From: "Justin T. Gibbs" <gibbs@freefall.freebsd.org>
 * Sender: owner-port-i386@NetBSD.ORG
 * Precedence: list
 * X-Loop: port-i386@NetBSD.ORG
 * 
 * >
 * >
 * >>As a data point:
 * >>ncr810, p100, netbsd-current
 * >>4338793 bytes/sec
 * >>with both scsiII drives I have.
 * >
 * >Using what test? ;-)
 * >
 * >What would be more useful (to me, at least) would be if you could
 * >provide iozone and/or Bonnie results.  I can make both binaries
 * >available if you don't want to build your own.
 * 
 * I would also suggest using this little program from Bruce Evans.
 * It attempts to measure the combined controller and disk command
 * overhead:
 */

#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#if 0
#define ITERATIONS	1000
#else
#define ITERATIONS	0		/* use elapsed time by default */
#endif
#define BLOCKSIZE	4096
#define ELAPSED_TIME	10

#ifdef linux
/* Linux stupidly requires 512byte aligned buffer for raw device access. */
#define ALIGNMENT	512
#else
#define ALIGNMENT	1
#endif

static void usage(void);
static int syserror(const char *where);
static long timeit(int fd, char *buf, unsigned blocksize);
static void alarm_handler();
static void *alloc_aligned_memory(size_t size, int alignment);

int alignment = ALIGNMENT;
int iterations = ITERATIONS;
unsigned blocksize = BLOCKSIZE;
int elapsed_time = ELAPSED_TIME;

int alarmed = 0;

char *program_name;

int main(int argc, char **argv)
{
    char *buf;
    int c, fd;
    long time_single;
    long time_double;

    program_name = argv[0];
    while ((c = getopt(argc, argv, "a:b:e:i:")) != -1) {
	switch (c) {
	case 'a': alignment = atoi(optarg); break;
	case 'b': blocksize = atoi(optarg); break;
	case 'e': elapsed_time = atoi(optarg); break;
	case 'i': iterations = atoi(optarg); break;
	case '?': usage();
	}
    }
    argc -= optind;
    argv += optind;
    if (argc != 1)
	usage();

    if (iterations == 0) {
	if (elapsed_time == 0)
	    elapsed_time = ELAPSED_TIME;
	elapsed_time /= 2;
	if (elapsed_time == 0)
	    elapsed_time = 1;
    }

    buf = alloc_aligned_memory(2 * blocksize, alignment);
    fd = open(argv[0], O_RDONLY);
    if (fd == -1)
	syserror("open");
    time_single = timeit(fd, buf, blocksize);
    time_double = timeit(fd, buf, 2 * blocksize);
    printf("Command overhead is %g usec "
	   "(time_%u = %g, time_%u = %g)\n",
	   (double)(time_single - (time_double - time_single)) / iterations,
	   blocksize, (double)time_single / iterations,
	   2 * blocksize, (double)time_double / iterations);
    printf("transfer speed is %g bytes/sec\n",
	   (double)blocksize * iterations * 1000000.0 /
	   (time_double - time_single));
    exit(0);
}

static void usage(void)
{
    fprintf(stderr, "Usage: %s [<options>] <raw device>\n", program_name);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t-a <alignment>\n");
    fprintf(stderr, "\t-b <block size>\n");
    fprintf(stderr, "\t-e <elapsed time in seconds>\n");
    fprintf(stderr, "\t-i <iterations>\n");
    exit(2);
}

static int syserror(const char *where)
{
    perror(where);
    exit(1);
}

static long timeit(int fd, char *buf, unsigned blocksize)
{
    struct timeval finish;
    struct timeval start;
    int i;

    if (read(fd, buf, blocksize) != blocksize)
	syserror("read");
    if (iterations != 0) {
	if (gettimeofday(&start, (struct timezone *)NULL) != 0)
	    syserror("gettimeofday(start)");
	for (i = 0; i < iterations; ++i) {
	    if (lseek(fd, (off_t)0, SEEK_SET) == -1)
		syserror("lseek");
	    if (read(fd, buf, blocksize) != blocksize)
		syserror("read");
	}
	if (gettimeofday(&finish, (struct timezone *)NULL) != 0)
	    syserror("gettimeofday(finish)");
    } else {
	alarmed = 0;
	if ((int)signal(SIGALRM, alarm_handler) == -1)
	    syserror("signal(SIGALARM)");
	if (alarm(elapsed_time) == (unsigned)-1)
	    syserror("alarm");
	if (gettimeofday(&start, (struct timezone *)NULL) != 0)
	    syserror("gettimeofday(start)");
	for (i = 0; !alarmed; i++) {
	    if (lseek(fd, (off_t)0, SEEK_SET) == -1)
		syserror("lseek");
	    if (read(fd, buf, blocksize) != blocksize)
		syserror("read");
	}
	if (gettimeofday(&finish, (struct timezone *)NULL) != 0)
	    syserror("gettimeofday(finish)");
	iterations = i;
    }
    return (finish.tv_sec - start.tv_sec) * 1000000
	    + finish.tv_usec - start.tv_usec;
}

static void alarm_handler()
{
	alarmed = 1;
}

void *alloc_aligned_memory(size_t size, int alignment)
{
    char *p = malloc(size + alignment - 1);

    if (p == NULL) {
	fprintf(stderr, "no memory for %ld bytes\n",
		(long)size + alignment - 1);
	exit(1);
    }
    if (((size_t)p & (alignment - 1)) != 0)
	p += alignment - ((size_t)p & (alignment - 1));
    return p;
}

/*
 * 
 * >-----------------------------------------------------------------------------
 * >  Michael L. VanLoon                                 michaelv@HeadCandy.com
 * >       --<  Free your mind and your machine -- NetBSD free un*x  >--
 * >     NetBSD working ports: 386+PC, Mac 68k, Amiga, HP300, Sun3, Sun4,
 * >                           DEC PMAX (MIPS), DEC Alpha, PC532
 * >     NetBSD ports in progress: VAX, Atari 68k, others...
 * >-----------------------------------------------------------------------------
 * 
 * --
 * Justin T. Gibbs
 * ===========================================
 *   FreeBSD: Turning PCs into workstations
 * ===========================================
 */

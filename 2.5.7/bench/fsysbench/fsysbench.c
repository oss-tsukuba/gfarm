/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/resource.h>
#include <signal.h>

/* #define ENABLE_RDTSC */
/* #define ENABLE_RUSAGE */

#define RDTSC_RESOLUTION 1

#ifdef ENABLE_RDTSC
#if 1 /* Pentium */
#define rdtsc(x) __asm__ volatile ("rdtsc" : "=A" (x))
#else /* AMD */
#define rdtsc(x) __asm__ volatile ("rdtsc; shlq $32,%%rdx; orq %%rdx,%%rax" : "=a" (x) :: "%rdx")
#endif
#endif /* ENABLE_RDTSC */

#define DIR_SUFFIX "-dir"
#define FILE_SUFFIX ".file"
#define TEST_MAX  20

volatile sig_atomic_t timer_interrupt = 0;

/**********************************************************************/

static void
print_help()
{
	fprintf(stderr,
		"Usage: [-w dir] [-d count] [-f count] [-i] [-F]\n"
		"       -w working directry\n"
		"       -d targetdir count\n"
		"       -f targetfile count\n"
		"       -i do I/O test (latency of read or write)\n"
#ifdef ENABLE_RDTSC
		"       -r use rdtsc timer\n"
#endif
		"       -F for FUSE filesystem\n");
}

static void
print_errno(int en, char *msg)
{
	fprintf(stderr, "ERROR: %s: %s \n", msg, strerror(en));
}

static void
check_errno(int en, char *msg)
{
	if (errno != 0) {
		print_errno(en, msg);
		exit(1);
	}
}

/**********************************************************************/

/* config */
struct fsb_conf {
	int dirs;
	int files;
	int timer_type;
	int fuse_mode;  /* for FUSE filesystem */
	int enable_io_test;  /* read, write */
};

#define TIMER_RDTSC 1
#define TIMER_GETTIMEOFDAY 2
#define TIMER_GETRUSAGE 3

/* default */
static struct fsb_conf conf = {
	.dirs = 10,
	.files = 1,
	.timer_type = TIMER_GETTIMEOFDAY,
	.fuse_mode = 0,
	.enable_io_test = 0,
};

struct fsb_op_st {
	char *testname;
	int(*testfunc)(char *, void *);
	void *time;
	unsigned int count;
};

static struct fsb_op_st test_ops[TEST_MAX];

static void
test_op_set(struct fsb_op_st *test_opp,
	    char *testname, int(*testfunc)(char *, void *), void *time)
{
	test_opp->testname = strdup(testname);
	test_opp->testfunc = testfunc;
	test_opp->time = time;
	test_opp->count = 0;
}

struct fsb_times {
	void *t_mkdir;
	void *t_rmdir;
	void *t_stat_noent;
	void *t_creat;
	void *t_open;
	void *t_read0;
	void *t_write0;
	void *t_read8;
	void *t_write8;
	void *t_utime;
	void *t_stat;
	void *t_chmod_w;
	void *t_chmod_x;
	void *t_rename;
	void *t_rename_overwrite;
	void *t_unlink;
};

static struct fsb_times times;

static void
times_init(void(*time_initializer)(void**))
{
	time_initializer(&times.t_mkdir);
	time_initializer(&times.t_rmdir);
	time_initializer(&times.t_stat_noent);
	time_initializer(&times.t_creat);
	time_initializer(&times.t_open);
	time_initializer(&times.t_read0);
	time_initializer(&times.t_write0);
	time_initializer(&times.t_read8);
	time_initializer(&times.t_write8);
	time_initializer(&times.t_utime);
	time_initializer(&times.t_stat);
	time_initializer(&times.t_chmod_w);
	time_initializer(&times.t_chmod_x);
	time_initializer(&times.t_rename);
	time_initializer(&times.t_rename_overwrite);
	time_initializer(&times.t_unlink);
}

static void
timer_interrupt_handler(int sig){
	char msg[] =
	    "*** Interrupt! (cleanup) (following values may be invalid)\n";

	timer_interrupt = 1;
	write(1, msg, sizeof(msg) - 1); /* printf(3) isn't async-signal-safe */
}

/**********************************************************************/

#define SECOND_BY_MICROSEC  1000000

static void
timeval_normalize(struct timeval *t)
{
	long n;

	if (t->tv_usec >= SECOND_BY_MICROSEC) {
		n = t->tv_usec / SECOND_BY_MICROSEC;
		t->tv_usec -= n * SECOND_BY_MICROSEC;
		t->tv_sec += n;
	} else if (t->tv_usec < 0) {
		n = -t->tv_usec / SECOND_BY_MICROSEC + 1;
		t->tv_usec += n * SECOND_BY_MICROSEC;
		t->tv_sec -= n;
	}
}

static void
timeval_add(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	timeval_normalize(t1);
}

static void
timeval_sub(struct timeval *t1, const struct timeval *t2)
{
	t1->tv_sec -= t2->tv_sec;
	t1->tv_usec -= t2->tv_usec;
	timeval_normalize(t1);
}

/**********************************************************************/

static void
timeval_malloc(void **val)
{
	*val = calloc(1, sizeof(struct timeval));
	if (*val == NULL) {
		print_errno(errno, "timeval_malloc");
		exit(1);
	}
}

static void
timer_gettimeofday_init()
{
	times_init(&timeval_malloc);
}

static void *
timer_gettimeofday_start()
{
	static struct timeval timer;

	gettimeofday(&timer, NULL);

	return (&timer);
}

static void
timer_gettimeofday_stop(void *timerp_tmp, void *addtimerp_tmp)
{
	struct timeval now, *timerp, *addtimerp;

	gettimeofday(&now, NULL);

	timerp = (struct timeval *)timerp_tmp;
	addtimerp = (struct timeval *)addtimerp_tmp;

	timeval_sub(&now, timerp);
	timeval_add(addtimerp, &now);
}

static void
timer_gettimeofday_print(void *timerp_tmp, char *msg, unsigned int avgnum)
{
	struct timeval *timerp = (struct timeval *)timerp_tmp;
	double time = (((double)timerp->tv_sec) * SECOND_BY_MICROSEC
		       + timerp->tv_usec) / SECOND_BY_MICROSEC;
	double average = time / avgnum;

	printf("%10.6f s | %10.6f avg | %s\n", time, average, msg);
}

/**********************************************************************/

#ifdef ENABLE_RUSAGE

static void
timer_getrusage_init()
{
	times_init(&timeval_malloc);
}

static void *
timer_getrusage_start()
{
	static struct timeval timer;
	struct rusage ru;

	getrusage(RUSAGE_SELF, &ru);
	timer = ru.ru_utime;

	return (&timer);
}

static void
timer_getrusage_stop(void *timerp_tmp, void *addtimerp_tmp)
{
	struct timeval now, *timerp, *addtimerp;
	struct rusage ru;

	getrusage(RUSAGE_SELF, &ru);
	now = ru.ru_utime;

	timerp = (struct timeval *)timerp_tmp;
	addtimerp = (struct timeval *)addtimerp_tmp;

	timeval_sub(&now, timerp);
	timeval_add(addtimerp, &now);
}

static void
timer_getrusage_print(void *timerp_tmp, char *msg, unsigned int avgnum)
{
	timer_gettimeofday_print(timerp_tmp, msg, avgnum);
}

#endif /* ENABLE_RUSAGE */

/**********************************************************************/

#ifdef ENABLE_RDTSC

static void
unsigned_long_long_int_malloc(void **val)
{
	*val = calloc(1, sizeof(unsigned long long int));
	if (*val == NULL) {
		print_errno(errno, "unsigned_long_long_int_malloc");
		exit(1);
	}
}

static unsigned long long int cpuclock;

static void
timer_rdtsc_init()
{
	unsigned long long int x;

	rdtsc(x);
	usleep(1000000);
	rdtsc(cpuclock);

	cpuclock = cpuclock - x;
	printf("CPU clock: %lld Hz\n", cpuclock);

	times_init(&unsigned_long_long_int_malloc);
}

static void *
timer_rdtsc_start()
{
	static unsigned long long int val;

	rdtsc(val);

	return (&val);
}

static void
timer_rdtsc_stop(void *start_tmp, void *add_tmp)
{
	unsigned long long int *start, *addcount, val;

	rdtsc(val);

	start = (unsigned long long int*) start_tmp;
	addcount = (unsigned long long int*) add_tmp;

	*addcount = *addcount + (val - *start) / RDTSC_RESOLUTION;
}

static void
timer_rdtsc_print(void *count_tmp, char *msg, unsigned int avgnum)
{
	unsigned long long int *count = (unsigned long long int*) count_tmp;
	double time = (double) *count / (cpuclock / RDTSC_RESOLUTION);
	double average = time / avgnum;

#if 0
	printf("%12lld clock | %10.6f s | %10.6f avg | %s\n",
	       *count, time, average, msg);
#else
	printf("%10.6f s | %10.6f avg | %s\n", time, average, msg);
#endif
}

#endif /* ENABLE_RDTSC */

/**********************************************************************/

static void* (*timer_start)(void);
static void (*timer_stop)(void *, void *);
static void (*timer_print)(void *, char *, unsigned int);

static void
timer_func_set(int timer_func_type)
{
	switch (timer_func_type) {
#ifdef ENABLE_RDTSC
	case TIMER_RDTSC:
		timer_rdtsc_init();
		timer_start = timer_rdtsc_start;
		timer_stop = timer_rdtsc_stop;
		timer_print = timer_rdtsc_print;
		break;
#endif
#ifdef ENABLE_RUSAGE
	case TIMER_GETRUSAGE:
		timer_getrusage_init();
		timer_start = timer_getrusage_start;
		timer_stop = timer_getrusage_stop;
		timer_print = timer_getrusage_print;
		break;
#endif
	case TIMER_GETTIMEOFDAY:
	default:
		timer_gettimeofday_init();
		timer_start = timer_gettimeofday_start;
		timer_stop = timer_gettimeofday_stop;
		timer_print = timer_gettimeofday_print;
	}
}

/**********************************************************************/

static int
check_mode(char *name, int mode)
{
	struct stat st;

	if (lstat(name, &st) != 0)
		return (-1);

	if ((st.st_mode & mode) == mode)
		return (0);
	else {
		errno = EACCES;
		return (-1);
	}
}

/**********************************************************************/

static int
test_creat(char *name, void *time)
{
	void *timer;
	int e, fd, save_errno;
	struct stat buf;

	timer = timer_start();
	fd = creat(name, 0600);
	if (fd < 0) goto error;
	e = close(fd);
	if (e != 0) goto error;
	if (conf.fuse_mode) { /* force RELEASE */
		e = lstat(".", &buf);
		if (e != 0) goto error;
	}
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_open(char *name, void *time)
{
	void *timer;
	int e, fd, save_errno;

	timer = timer_start();
	fd = open(name, O_RDONLY);
	if (fd < 0) goto error;
	e = close(fd);
	if (e != 0) goto error;
	if (conf.fuse_mode) { /* force RELEASE */
		e = utime(".", NULL);
		if (e != 0) goto error;
	}
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_read_common(char *name, int len, void *time)
{
	void *timer;
	int e, fd, save_errno;
	ssize_t l;
	unsigned char c[8];

	if (check_mode(name, 0400) != 0)
		if (chmod(name, 0400) != 0)
			return (1);

	fd = open(name, O_RDONLY);
	if (fd < 0) return (1);
	timer = timer_start();
	l = read(fd, c, len);
	save_errno = errno;
	timer_stop(timer, time);
	e = close(fd);
	if (l != len || e != 0) {
		errno = save_errno;
		return (-1);
	} else
		return (0);
}

static int
test_read0(char *name, void *time)
{
	return test_read_common(name, 0, time);
}

static int
test_read8(char *name, void *time)
{
	return test_read_common(name, 8, time);
}

static int
test_write_common(char *name, int len, void *time)
{
	void *timer;
	int e, fd, save_errno;
	ssize_t l;
	unsigned char c[8];

	if (check_mode(name, 0200) != 0)
		if (chmod(name, 0600) != 0)
			return (1);

	memset(c, 255, 8);
	fd = open(name, O_WRONLY);
	if (fd < 0) return (1);
	timer = timer_start();
	l = write(fd, c, len);
	save_errno = errno;
	timer_stop(timer, time);
	e = close(fd);
	if (l != len || e != 0) {
		errno = save_errno;
		return (-1);
	} else
		return (0);
}

static int
test_write0(char *name, void *time)
{
	return test_write_common(name, 0, time);
}

static int
test_write8(char *name, void *time)
{
	return test_write_common(name, 8, time);
}

static int
test_stat_noent(char *name, void *time)
{
	void *timer;
	struct stat buf;
	int e, save_errno;

	timer = timer_start();
	e = lstat(name, &buf);
	if (e != 0) goto error;
	timer_stop(timer, time);
	errno = EEXIST;
	return (-1);
error:
	save_errno = errno;
	timer_stop(timer, time);
	if (save_errno == ENOENT) {
		errno = 0;
		return (0);
	} else {
		errno = save_errno;
		return (-1);
	}
}

static int
test_stat(char *name, void *time)
{
	void *timer;
	struct stat buf;
	int e, save_errno;

	timer = timer_start();
	e = lstat(name, &buf);
	if (e != 0) goto error;
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_utime(char *name, void *time)
{
	void *timer;
	int e, save_errno;

	timer = timer_start();
	e = utime(name, NULL);
	if (e != 0) goto error;
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_chmod_common(char *name, mode_t mode, void *time)
{
	void *timer;
	int e, save_errno;

	timer = timer_start();
	e = chmod(name, mode);
	if (e != 0) goto error;
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_chmod_w(char *name, void *time)
{
	if (chmod(name, 0400) != 0) return (-1);
	return test_chmod_common(name, 0600, time);
}

static int
test_chmod_x(char *name, void *time)
{
	if (chmod(name, 0400) != 0) return (-1);
	return test_chmod_common(name, 0500, time);
}

static int
test_rename_common(char *name, int overwrite, void *time)
{
	void *timer;
	int e, save_errno;
	char newname[16];

	sprintf(newname, "%s_rename", name);
	if (overwrite && check_mode(newname, 0000) != 0) {
#if 0
		e = mknod(newname, 0600|S_IFREG, 0);
		if (e != 0) return (-1);
#else
		e = creat(newname, 0600);
		if (e >= 0)
			close(e);
		else
			return (-1);
#endif
	}
	timer = timer_start();
	e = rename(name, newname);
	if (e != 0) goto error;
	timer_stop(timer, time);
	rename(newname, name);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	errno = save_errno;
	return (-1);
}

static int
test_rename(char *name, void *time)
{
	return test_rename_common(name, 0, time);
}

static int
test_rename_overwrite(char *name, void *time)
{
	return test_rename_common(name, 1, time);
}

static int
test_unlink(char *name, void *time)
{
	void *timer;
	int e, save_errno;
	char newname[16];

	timer = timer_start();
	e = unlink(name);
	if (e != 0) goto error;
	timer_stop(timer, time);
	return (0);
error:
	save_errno = errno;
	timer_stop(timer, time);
	if (save_errno != 0) {
		sprintf(newname, "%s_rename", name);
		unlink(newname);
	}
	errno = save_errno;
	return (-1);
}

/**********************************************************************/
#define DELETE 1
#define NORMAL 0

static int
fsysbench_loop(struct fsb_op_st ops[], int deletemode)
{
	int i, j, n;
	char dirname[16], filename[16];
	char cwd[PATH_MAX];
	int retv, save_errno;

	if (getcwd(cwd, PATH_MAX) == NULL)
		return (1);
	errno = 0;
	retv = 0;
	save_errno = 0;
	for (i = 0; i < conf.dirs; i++) {
		sprintf(dirname, "%d%s", i, DIR_SUFFIX);
		if (chdir(dirname) != 0)
			return (1);
		for (j = 0; j < conf.files; j++) {
			sprintf(filename, "%d%s", j, FILE_SUFFIX);
			if (deletemode == DELETE) {
				if (test_unlink(filename, times.t_unlink)
				    != 0) {
					save_errno = errno;
					retv = 1;
				}
			} else {
				for (n = 0;
				     ops[n].testname != NULL &&
					     timer_interrupt == 0; n++) {
					errno = 0;
					if (ops[n].testfunc(
						    filename,
						    ops[n].time) != 0) {
						save_errno = errno;
						retv = 1;
						goto end;
					}
					ops[n].count++;
				}
			}
		}
		if (chdir(cwd) != 0)
			return (1);
	}
end:
	if (chdir(cwd) != 0)
		return (1);

	errno = save_errno;
	return (retv);
}

static int
fsysbench()
{
	int ret;
	int i;

	if (conf.files == 0)
		return (0);

	ret = fsysbench_loop(test_ops, NORMAL);

	for (i = 0; test_ops[i].testname != NULL; i++) {
		timer_print(test_ops[i].time, test_ops[i].testname,
			    test_ops[i].count);
	}

	return (ret);
}

static int
remove_all_files()
{
	int ret;

	if (conf.files == 0)
		return (0);

	ret = fsysbench_loop(test_ops, DELETE);
	timer_print(times.t_unlink, "unlink", conf.files * conf.dirs);

	return (ret);
}

static int
make_dirs()
{
	int i, ret = 0;
	char name[16];
	void *timer;

	timer = timer_start();
	for (i = 0; i < conf.dirs && timer_interrupt == 0; i++) {
		sprintf(name, "%d%s", i, DIR_SUFFIX);
		if (mkdir(name, 0700) != 0 && errno != EEXIST) {
			ret = 1;
			break;
		}
	}
	timer_stop(timer, times.t_mkdir);
	timer_print(times.t_mkdir, "mkdir", conf.dirs);

	return (ret);
}

static int
remove_dirs()
{
	int i, ret = 0;
	char name[16];
	void *timer;

	timer = timer_start();
	for (i = 0; i < conf.dirs; i++) {
		sprintf(name, "%d%s", i, DIR_SUFFIX);
		if (rmdir(name) != 0 && errno != ENOENT) {
			ret = 1;
			break;
		}
	}
	timer_stop(timer, times.t_rmdir);
	timer_print(times.t_rmdir, "rmdir", conf.dirs);

	return (ret);
}

/**********************************************************************/

static void
tests_init()
{
	int i = 0;

	test_op_set(&test_ops[i++], "stat ENOENT",
		    test_stat_noent, times.t_stat_noent);
	if (conf.fuse_mode) {
		test_op_set(&test_ops[i++], "creat+close+stat",
			    test_creat, times.t_creat);
		test_op_set(&test_ops[i++], "open+close+utime",
			    test_open, times.t_open);
	} else {
		test_op_set(&test_ops[i++], "creat+close",
			    test_creat, times.t_creat);
		test_op_set(&test_ops[i++], "open+close",
			    test_open, times.t_open);
	}
	test_op_set(&test_ops[i++], "stat EXIST",
		    test_stat, times.t_stat);
	test_op_set(&test_ops[i++], "utime",
		    test_utime, times.t_utime);
	test_op_set(&test_ops[i++], "chmod u+w",
		    test_chmod_w, times.t_chmod_w);
	test_op_set(&test_ops[i++], "chmod u+x",
		    test_chmod_x, times.t_chmod_x);
	if (conf.enable_io_test) {
		test_op_set(&test_ops[i++], "write 0 byte",
			    test_write0, times.t_write0);
		test_op_set(&test_ops[i++], "write 8 bytes",
			    test_write8, times.t_write8);
		test_op_set(&test_ops[i++], "read  0 byte",
			    test_read0, times.t_read0);
		test_op_set(&test_ops[i++], "read  8 bytes",
			    test_read8, times.t_read8);
	}
	test_op_set(&test_ops[i++], "rename",
		    test_rename, times.t_rename);
	test_op_set(&test_ops[i++], "rename overwrite",
		    test_rename_overwrite, times.t_rename_overwrite);

	/* if (i >= TEST_MAX) error */

	test_ops[i].testname = NULL; /* end of tests */
}

int
main(int argc, char **argv)
{
	char c;

	while ((c = getopt(argc, argv, "w:d:f:iruF")) != EOF) {
		switch (c) {
		case 'w':
			if (chdir(optarg) != 0) {
				print_errno(errno, "option -w");
				exit(1);
			}
			printf("working directory: %s\n", optarg);
			break;
		case 'd':
			conf.dirs = atoi(optarg);
			if (conf.dirs <= 0) {
				print_errno(EINVAL, "option -d");
				exit(1);
			}
			break;
		case 'f':
			conf.files = atoi(optarg);
			if (conf.files < 0) {
				print_errno(EINVAL, "option -f");
				exit(1);
			}
			break;
		case 'i':
			conf.enable_io_test = 1;
			break;
#ifdef ENABLE_RDTSC
		case 'r':
			conf.timer_type = TIMER_RDTSC;
			break;
#endif
#ifdef ENABLE_RUSAGE
		case 'u':
			/* worthless mode... */
			conf.timer_type = TIMER_GETRUSAGE;
			break;
#endif
		case 'F':
			conf.fuse_mode = 1;
			printf("for FUSE filesystem\n");
			break;
		default:
			print_help();
			exit(1);
		}
	}
	if (optind != argc) {
		print_help();
		exit(1);
	}

	signal(SIGINT, timer_interrupt_handler);
	timer_func_set(conf.timer_type);
	tests_init();

	printf("directories * files = %d * %d = %ld files\n",
	       conf.dirs, conf.files, (long)conf.dirs * (long)conf.files);

	if (make_dirs() != 0)
		check_errno(errno, "make_dirs");
	if (fsysbench() != 0)
		print_errno(errno, "fsysbench main");
	if (remove_all_files() != 0)
		print_errno(errno, "remove_all_files");
	if (remove_dirs() != 0)
		check_errno(errno, "remove_dirs");

	return (0);
}

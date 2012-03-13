/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

char *program_name = "gfiops";

#define TYPE_ALL    0
#define TYPE_DIR    1
#define TYPE_FILE0  2
#define TYPE_FILE1B 3
#define TYPE_FILE1K 4
#define TYPE_FILE1M 5
#define TYPE_STAT   6
#define TYPE_UTIME  7

#define BYTE_1 1
#define KILO_1 1024
#define MEGA_1 1048576

#define MAX_LINE 1024

static void
usage(void)
{
	fprintf(stderr, "Usage: %s gfarm_directory n_processes n_times type\n",
		program_name);
	fprintf(stderr,
"type=0: All\n"
"type=1: gfs_mkdir, gfs_opendir + gfs_readdir + gfs_closedir, gfs_rmdir\n"
"type=2: gfs_pio_create + gfs_pio_close, gfs_pio_open + gfs_pio_close\n"
"type=3: gfs_pio_create + gfs_pio_write(1B) + gfs_pio_close,\n"
"        gfs_pio_open + gfs_pio_read(1B) + gfs_pio_close\n"
"type=4: gfs_pio_create + gfs_pio_write(1KB) + gfs_pio_close,\n"
"        gfs_pio_open + gfs_pio_read(1KB) + gfs_pio_close\n"
"type=5: gfs_pio_create + gfs_pio_write(1MB) + gfs_pio_close,\n"
"        gfs_pio_open + gfs_pio_read(1MB) + gfs_pio_close\n"
"type=6: gfs_lstat(existing entry), gfs_lstat(no entry)\n"
"type=7: gfs_utimes(), gfs_utimes(same time [not update])\n"
);
}

struct function {
	const int id;
	char *name;
};

static struct function FUNC_INIT     = { .id = 1, .name = "init" };
static struct function FUNC_TERM     = { .id = 2, .name = "term" };
static struct function FUNC_MKDIR    = { .id = 3, .name = "mkdir" };
static struct function FUNC_OPENDIR  = { .id = 4, .name = "opendir" };
static struct function FUNC_RMDIR    = { .id = 5, .name = "rmdir" };

static struct function FUNC_SETINT64 = { .id = 6, .name = "set int64" };

static struct function FUNC_CREATE   = { .id = 7, .name = "create" };
static struct function FUNC_OPEN     = { .id = 8, .name = "open" };
static struct function FUNC_UNLINK   = { .id = 9, .name = "unlink" };

static struct function FUNC_LSTAT    = { .id = 10, .name = "lstat" };
static struct function FUNC_UTIMES   = { .id = 11, .name = "utimes" };

#define MAX_NAMELEN 37  /* /0/1/2/3/4/5/6/7/8/9/0/p100/c100000 */
#define MAX_DIRLEN 22

const static int ID_ERROR = -1;
const static int ID_SKIP = -2;

static void
send_error(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "error = ");
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	printf("%d\n", ID_ERROR);
}

static void
send_id(struct function *func) {
	printf("%d\n", func->id);
}

static void
do_initialize(const char *dname, int ntimes, char ***namesp)
{
	gfarm_error_t e;
	int i, j;
	char **names;

	GFARM_MALLOC_ARRAY(names, ntimes);
	if (names) {
		for (i = 0; i < ntimes; i++) {
			GFARM_MALLOC_ARRAY(names[i], MAX_NAMELEN);
			if (names[i] == NULL) {
				for (j = 0; j < i; j++)
					free(names[j]);
				free(names);
				names = NULL;
				break;
			}
			snprintf(names[i], MAX_NAMELEN, "%s/c%d", dname, i);
		}
	}
	if (names == NULL) {
		send_error("no memory");
		return;
	}
	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		send_error("gfarm_initialize(): %s", gfarm_error_string(e));
		return;
	}
	e = gfs_mkdir(dname, 0755);

	if (e != GFARM_ERR_ALREADY_EXISTS && e != GFARM_ERR_NO_ERROR) {
		send_error("gfs_mkdir() [init]: %s: %s",
			   dname, gfarm_error_string(e));
		return;
	}
	send_id(&FUNC_INIT);

	*namesp = names;
}

static void
do_terminate(const char *dname, char **names, int ntimes)
{
	gfarm_error_t e;
	int i;
	GFS_Dir dp;
	struct gfs_dirent *de;

	e = gfs_opendir(dname, &dp);
	if (e == GFARM_ERR_NO_ERROR) {
		while ((e = gfs_readdir(dp, &de)) == GFARM_ERR_NO_ERROR &&
		       de != NULL) {
			char name[2048];
			if (strcmp(de->d_name, ".") == 0 ||
			    strcmp(de->d_name, "..") == 0)
				continue;
			snprintf(name, 2048, "%s/%s", dname, de->d_name);
			if (de->d_type == GFS_DT_DIR)
				e = gfs_rmdir(name);
			else
				e = gfs_unlink(name);
			if (e != GFARM_ERR_NO_ERROR &&
			    e != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
				fprintf(stderr, "remove entries: %s\n",
					gfarm_error_string(e));
				break;
			}
		}
		gfs_closedir(dp);
	}

	e = gfs_rmdir(dname);
	/* no check */

	if (names) {
		for (i = 0; i < ntimes; i++)
			free(names[i]);
		free(names);
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		send_error("gfarm_terminate(): %s", gfarm_error_string(e));
		return;
	}

	send_id(&FUNC_TERM);
}

static void
do_mkdir(char **names, int nnames)
{
	gfarm_error_t e;
	int i;

	for (i = 0; i < nnames; i++) {
		e = gfs_mkdir(names[i], 0755);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_mkdir(): %s: %s", names[i],
				   gfarm_error_string(e));
			return;
		}
	}
	send_id(&FUNC_MKDIR);
}

static void
do_opendir(char **names, int nnames)
{
	gfarm_error_t e;
	int i;

	/* opendir, readdir, closedir */
	for (i = 0; i < nnames; i++) {
		GFS_Dir dp;
		struct gfs_dirent *de;
		e = gfs_opendir(names[i], &dp);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_opendir(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
		while ((e = gfs_readdir(dp, &de)) == GFARM_ERR_NO_ERROR &&
		       de != NULL)
			;
		gfs_closedir(dp);
	}
	send_id(&FUNC_OPENDIR);
}

static void
do_rmdir(char **names, int nnames)
{
	gfarm_error_t e;
	int i;

	for (i = 0; i < nnames; i++) {
		e = gfs_rmdir(names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_rmdir(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
	}
	send_id(&FUNC_RMDIR);
}

#define BUFSIZE 65536

static void
do_create(char **names, int nnames, gfarm_off_t size)
{
	int i;
	gfarm_error_t e;
	GFS_File gf;
	char buf[BUFSIZE];
	gfarm_off_t len;

	if (size == -2) { /* for gfsd connection */
		nnames = 1;
		size = 1;
	}

	for (i = 0; i < nnames; i++) {
		len = size;
		e = gfs_pio_create(names[i], GFARM_FILE_WRONLY,
				   0666 & GFARM_S_ALLPERM, &gf);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_pio_create(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
		while (len > 0) {
			int rv;
			e = gfs_pio_write(gf, buf,
					  len > BUFSIZE ? BUFSIZE : len,
					  &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_pio_close(gf);
				send_error("gfs_pio_write(): %s: %s",
					   names[i], gfarm_error_string(e));
				return;
			}
			len -= rv;
		}
		e = gfs_pio_close(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_pio_close(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
	}
	send_id(&FUNC_CREATE);
}

static void
do_open(char **names, int nnames, gfarm_off_t size)
{
	int i;
	gfarm_error_t e;
	GFS_File gf;
	char buf[BUFSIZE];
	gfarm_off_t len;

	for (i = 0; i < nnames; i++) {
		len = size;
		e = gfs_pio_open(names[i], GFARM_FILE_RDONLY, &gf);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_pio_open(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
		while (len > 0) {
			int rv;
			e = gfs_pio_read(gf, buf,
					 len > BUFSIZE ? BUFSIZE : len, &rv);
			if (e != GFARM_ERR_NO_ERROR) {
				gfs_pio_close(gf);
				send_error("gfs_pio_read(): %s: %s",
					   names[i], gfarm_error_string(e));
				return;
			}
			len -= rv;
		}
		e = gfs_pio_close(gf);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_pio_close(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
	}
	send_id(&FUNC_OPEN);
}

static void
do_unlink(char **names, int nnames)
{
	int i;
	gfarm_error_t e;

	for (i = 0; i < nnames; i++) {
		e = gfs_unlink(names[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			send_error("gfs_unlink(): %s: %s",
				   names[i], gfarm_error_string(e));
			return;
		}
	}
	send_id(&FUNC_UNLINK);
}

static void
do_lstat(char *dname, int ntimes, int exist)
{
	int i;
	struct gfs_stat st;
	gfarm_error_t e;

	if (exist == 1) {
		for (i = 0; i < ntimes; i++) {
			e = gfs_lstat(dname, &st);
			if (e != GFARM_ERR_NO_ERROR) {
				send_error("gfs_lstat(): %s: %s",
					   dname, gfarm_error_string(e));
				return;
			}
			gfs_stat_free(&st);
		}
	} else {
		for (i = 0; i < ntimes; i++) {
			e = gfs_lstat("/noent...", &st);
			if (e == GFARM_ERR_NO_ERROR) {
				gfs_stat_free(&st);
				send_error("gfs_lstat(noent) exists");
				return;
			}
		}
	}
	send_id(&FUNC_LSTAT);
}

static void
do_utimes(char *dname, int ntimes, int update)
{
	int i;
	gfarm_error_t e;
	struct gfarm_timespec gt[2];
	struct gfs_stat st;

	e = gfs_lstat(dname, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		send_error("gfs_utimes(): %s: %s",
			   dname, gfarm_error_string(e));
		return;
	}
	gt[0] = st.st_atimespec;
	gt[1] = st.st_mtimespec;
	gfs_stat_free(&st);
	if (update) {
		for (i = 0; i < ntimes; i++) {
			e = gfs_utimes(dname, gt);
			if (e != GFARM_ERR_NO_ERROR) {
				send_error("gfs_utimes(): %s: %s",
					   dname, gfarm_error_string(e));
				return;
			}
			gt[0].tv_sec++;
			gt[1].tv_sec++;
		}
	} else {
		for (i = 0; i < ntimes; i++) {
			e = gfs_utimes(dname, gt);
			if (e != GFARM_ERR_NO_ERROR) {
				send_error("gfs_utimes(): %s: %s",
					   dname, gfarm_error_string(e));
				return;
			}
		}
	}
	send_id(&FUNC_UTIMES);
}

static void
child_main(char *dname, int ntimes)
{
	char **names = NULL;
	int id, n;
	int64_t int64 = 0;
	char line[MAX_LINE];

	for (;;) {
		if (fgets(line, MAX_LINE, stdin) == NULL) {
			send_error("unexpected EOF from parent (1)");
			return;
		}
		n = sscanf(line, "%d", &id);
		if (n != 1) {
			send_error("unexpected sscanf (1): `%s'", line);
			return;
		}
		if (names == NULL && id != FUNC_TERM.id) {
			if (id == FUNC_INIT.id)
				do_initialize(dname, ntimes, &names);
			else if (id != FUNC_INIT.id)
				send_error("not initalized");
		} else if (id == FUNC_MKDIR.id)
			do_mkdir(names, ntimes);
		else if (id == FUNC_OPENDIR.id)
			do_opendir(names, ntimes);
		else if (id == FUNC_RMDIR.id)
			do_rmdir(names, ntimes);
		else if (id == FUNC_SETINT64.id) {
			long long int lld;
			if (fgets(line, MAX_LINE, stdin) == NULL) {
				send_error("unexpected EOF from parent (2)");
				return;
			}
			n = sscanf(line, "%lld", &lld);
			if (n != 1) {
				send_error("unexpected sscanf (2): `%s'",
					   line);
				return;
			}
			int64 = (int64_t) lld;
			send_id(&FUNC_SETINT64);
		} else if (id == FUNC_CREATE.id)
			do_create(names, ntimes, (gfarm_off_t)int64);
		else if (id == FUNC_OPEN.id)
			do_open(names, ntimes, (gfarm_off_t)int64);
		else if (id == FUNC_UNLINK.id)
			do_unlink(names, ntimes);
		else if (id == FUNC_LSTAT.id)
			do_lstat(dname, ntimes, (int)int64);
		else if (id == FUNC_UTIMES.id)
			do_utimes(dname, ntimes, (int)int64);
		else if (id == FUNC_TERM.id) {
			do_terminate(dname, names, ntimes);
			return; /* end */
		} else {
			send_error("unknown id = %d", id);
			return; /* end */
		}
	}
}

struct process {
	pid_t pid;
	int pipe_stdin[2];
	int pipe_stdout[2];
	int pipe_stderr[2];
	FILE *send_stdin;
	FILE *recv_stdout;
	FILE *recv_stderr;
};

static void
start(struct function *func, struct process *procs, int nprocs,
      struct timeval *timep)
{
	int i;

	if (timep)
		gettimeofday(timep, NULL);
	for (i = 0; i < nprocs; i++)
		fprintf(procs[i].send_stdin, "%d\n", func->id);
}

static void
result(const char *name, struct timeval *t1, struct timeval *t2,
       int nprocs, int ntimes)
{
	int n, num_persec;
	float usec;

	gfarm_timeval_sub(t2, t1);

	n = nprocs * ntimes;
	usec = ((float)(t2->tv_sec * 1000000L + t2->tv_usec)) / (float) n;
	num_persec = 1000000L / usec;

	printf("%9s: %6d /sec: %d proc * %4d: "
	       "%2ld.%06ld sec: %9.3f usec * %d\n",
	       name, num_persec, nprocs, ntimes,
	       (long int) t2->tv_sec, (long int) t2->tv_usec, usec, n);
}

static int
end_with_suffix(struct function *func, struct process *procs, int nprocs,
		int ntimes, struct timeval *timep, char *suffix)
{
	int i, ok = 1;
	char line[MAX_LINE];
	int id;

	for (i = 0; i < nprocs; i++) {
		if (fgets(line, MAX_LINE, procs[i].recv_stdout) == NULL) {
			fprintf(stderr, "error: unexpected EOF from child\n");
			ok = 0;
			continue;
		}
#if 0
		printf("[debug] line from child = %s", line);
#endif
		if (sscanf(line, "%d", &id) == 1) {
			if (id == func->id)
				continue;
			else if (id == ID_SKIP) {
				ok = 0;
				continue;
			} else if (id == ID_ERROR) {
				ok = 0;
				continue;
			}
		} else
			fprintf(stderr, "unexpected sscanf: %s\n", line);
	}
	if (timep && ok) {
		struct timeval time2;
		gettimeofday(&time2, NULL);
		if (suffix == NULL)
			result(func->name, timep, &time2, nprocs, ntimes);
		else {
			char name[64];
			snprintf(name, 64, "%s %s", func->name, suffix);
			result(name, timep, &time2, nprocs, ntimes);
		}
	}
	return (ok);
}

static int
end_with_size(struct function *func, struct process *procs, int nprocs,
	      int ntimes, struct timeval *timep, int64_t size)
{
	if (size == -1)
		return (end_with_suffix(func, procs, nprocs, ntimes, timep,
					NULL));
	else {
		char suf[64];
		if (size == 0)
			snprintf(suf, 64, "0B");
		else if (size == BYTE_1)
			snprintf(suf, 64, "1B");
		else if (size == KILO_1)
			snprintf(suf, 64, "1K");
		else if (size == MEGA_1)
			snprintf(suf, 64, "1M");
		else
			snprintf(suf, 64, "%lld", (long long int) size);
		return (end_with_suffix(func, procs, nprocs, ntimes, timep,
					suf));
	}
}

static int
end(struct function *func, struct process *procs, int nprocs, int ntimes,
    struct timeval *timep)
{
	return (end_with_suffix(func, procs, nprocs, ntimes, timep, NULL));
}

static void
send_setint64(struct process *procs, int nprocs, int64_t int64)
{
	int i;

	for (i = 0; i < nprocs; i++) {
		fprintf(procs[i].send_stdin, "%d\n%lld\n",
			FUNC_SETINT64.id, (long long int) int64);
	}
	end(&FUNC_SETINT64, procs, nprocs, 0, NULL);
}

struct watch_stderr {
	int n_procs;
	struct process *procs;
	int is_end;
};
static struct watch_stderr watch_stderr_arg;

static void *
watch_stderr(void *arg)
{
	fd_set fdset_tmp, fdset_orig;
	int fd, i, n_eof, retv;
	char line[MAX_LINE];
	struct process *procs = watch_stderr_arg.procs;
	int maxfd = 0;
	struct timeval tv;

	FD_ZERO(&fdset_orig);
	for (i = 0; i < watch_stderr_arg.n_procs; i++) {
		fd = fileno(procs[i].recv_stderr);
		FD_SET(fd, &fdset_orig);
		if (fd > maxfd)
			maxfd = fd;
	}
	setvbuf(stderr, (char *) NULL, _IOLBF, 0);
	n_eof = 0;
	while (n_eof < watch_stderr_arg.n_procs) {
		if (watch_stderr_arg.is_end)
			break;
		memcpy(&fdset_tmp, &fdset_orig, sizeof(fd_set));
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		retv = select(maxfd + 1, &fdset_tmp, NULL, NULL, &tv);
		if (retv == -1) {
			fprintf(stderr,
				"failed to watch stderr: select: %s\n",
				strerror(errno));
			continue;
		} else if (retv == 0)
			continue;
		for (i = 0; i < watch_stderr_arg.n_procs; i++) {
			fd = fileno(procs[i].recv_stderr);
			if (!FD_ISSET(fd, &fdset_tmp))
				continue;
			if (fgets(line, MAX_LINE, procs[i].recv_stderr)
			    == NULL) { /* EOF: child exited */
				n_eof++;
				FD_CLR(fd, &fdset_orig);
				continue;
			}
			fprintf(stderr, "[pid=%ld] %s",
				(long int) procs[i].pid, line);
		}
	}

	return (NULL);
}

int
main(int argc, char **argv)
{
	int type, i, c;
	char *dir;
	int nprocs, ntimes, ok;
	struct process *procs;
	struct timeval t;
	gfarm_error_t e;
	pthread_t thr_watch_stderr;

	if (argc > 0)
		program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
		case 'h':
		case '?':
		default:
			usage();
		return (0);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 3) {
		usage();
		exit(EXIT_FAILURE);
	}

	dir = argv[0];
	nprocs = atoi(argv[1]);
	ntimes = atoi(argv[2]);
	type = atoi(argv[3]);

#if 1
	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
#endif

	if (strlen(dir) > MAX_DIRLEN) {
		fprintf(stderr, "too long testdir: %s\n", dir);
		exit(EXIT_FAILURE);
	}
	if (nprocs <= 0) {
		fprintf(stderr, "invalid n_processes: %d\n", nprocs);
		exit(EXIT_FAILURE);
	}
	GFARM_MALLOC_ARRAY(procs, nprocs);
	if (procs == NULL) {
		fprintf(stderr, "no memory\n");
		exit(EXIT_FAILURE);
	}

	fflush(stdout);
	fflush(stderr);
	for (i = 0; i < nprocs; i++) {
		pid_t pid;
		if (pipe(procs[i].pipe_stdin) == -1) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}
		if (pipe(procs[i].pipe_stdout) == -1) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}
		if (pipe(procs[i].pipe_stderr) == -1) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}
		if ((pid = fork()) == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			char dname[MAX_NAMELEN];
			snprintf(dname, MAX_NAMELEN, "%s/p%d", dir, i);
			close(procs[i].pipe_stdin[1]);
			close(procs[i].pipe_stdout[0]);
			close(procs[i].pipe_stderr[0]);
			dup2(procs[i].pipe_stdin[0], 0);
			dup2(procs[i].pipe_stdout[1], 1);
			dup2(procs[i].pipe_stderr[1], 2);
			close(procs[i].pipe_stdin[0]);
			close(procs[i].pipe_stdout[1]);
			close(procs[i].pipe_stderr[1]);
			setvbuf(stdout, (char *) NULL, _IOLBF, 0);
			setvbuf(stderr, (char *) NULL, _IOLBF, 0);
			child_main(dname, ntimes);
			close(0);
			close(1);
			close(2);
			_exit(0);
		}
		close(procs[i].pipe_stdin[0]);
		close(procs[i].pipe_stdout[1]);
		close(procs[i].pipe_stderr[1]);
		procs[i].pid = pid;
		procs[i].send_stdin = fdopen(procs[i].pipe_stdin[1], "w");
		procs[i].recv_stdout = fdopen(procs[i].pipe_stdout[0], "r");
		procs[i].recv_stderr = fdopen(procs[i].pipe_stderr[0], "r");
		if (procs[i].send_stdin == NULL ||
		    procs[i].recv_stdout == NULL ||
		    procs[i].recv_stderr == NULL) {
			perror("fdopen");
			exit(EXIT_FAILURE);
		}
		setvbuf(procs[i].send_stdin, (char *) NULL, _IOLBF, 0);
	}

	watch_stderr_arg.n_procs = nprocs;
	watch_stderr_arg.procs = procs;
	watch_stderr_arg.is_end = 0;
	pthread_create(&thr_watch_stderr, NULL, watch_stderr, NULL);

	start(&FUNC_INIT, procs, nprocs, NULL);
	ok = end(&FUNC_INIT, procs, nprocs, ntimes, NULL);
	if (!ok)
		goto term;

	if (type == TYPE_DIR || type == TYPE_ALL) {
		start(&FUNC_MKDIR, procs, nprocs, &t);
		ok = end(&FUNC_MKDIR, procs, nprocs, ntimes, &t);
		if (ok) {
			start(&FUNC_OPENDIR, procs, nprocs, &t);
			ok = end(&FUNC_OPENDIR, procs, nprocs, ntimes, &t);
		}
		start(&FUNC_RMDIR, procs, nprocs, &t);
		ok = end(&FUNC_RMDIR, procs, nprocs, ntimes, &t);
	}
	if (!ok)
		goto term;

	if (type == TYPE_FILE0 || type == TYPE_FILE1B || type == TYPE_FILE1K
	    || type == TYPE_FILE1M || type == TYPE_ALL) {
		int64_t sizes[4] = {-1, -1, -1, -1};
		int i;
		if (type == TYPE_FILE0)
			sizes[0] = 0;
		else if (type == TYPE_FILE1B)
			sizes[0] = BYTE_1;
		else if (type == TYPE_FILE1K)
			sizes[0] = KILO_1;
		else if (type == TYPE_FILE1M)
			sizes[0] = MEGA_1;
		else if (type == TYPE_ALL) {
			sizes[0] = 0;
			sizes[1] = BYTE_1;
			sizes[2] = KILO_1;
			sizes[3] = MEGA_1;
		}

		/* connect gfsd */
		send_setint64(procs, nprocs, -2);
		start(&FUNC_CREATE, procs, nprocs, &t);
		ok = end_with_size(&FUNC_CREATE, procs, nprocs, 1, &t, BYTE_1);
		if (!ok)
			goto term;
		for (i = 0; i < 4; i++) {
			int64_t size = sizes[i];
			if (size == -1)
				break;
			send_setint64(procs, nprocs, size);

			start(&FUNC_CREATE, procs, nprocs, &t);
			ok = end_with_size(&FUNC_CREATE, procs, nprocs,
					     ntimes, &t, size);
			if (ok) {
				start(&FUNC_OPEN, procs, nprocs, &t);
				end_with_size(&FUNC_OPEN, procs, nprocs,
					      ntimes, &t, size);
			}
			start(&FUNC_UNLINK, procs, nprocs, &t);
			end_with_size(&FUNC_UNLINK, procs, nprocs, ntimes,
				      &t, size);
		}
	}
	if (!ok)
		goto term;

	if (type == TYPE_STAT || type == TYPE_ALL) {
		send_setint64(procs, nprocs, 1); /* entry exists */
		start(&FUNC_LSTAT, procs, nprocs, &t);
		end_with_suffix(&FUNC_LSTAT, procs, nprocs, ntimes, &t, "EX");

		send_setint64(procs, nprocs, 0); /* entry does not exist */
		start(&FUNC_LSTAT, procs, nprocs, &t);
		end_with_suffix(&FUNC_LSTAT, procs, nprocs, ntimes, &t, "NE");
	}
	if (type == TYPE_UTIME || type == TYPE_ALL) {
		send_setint64(procs, nprocs, 1); /* utimes with update */
		start(&FUNC_UTIMES, procs, nprocs, &t);
		end_with_suffix(&FUNC_UTIMES, procs, nprocs, ntimes, &t, "UP");

		send_setint64(procs, nprocs, 0); /* utimes without update */
		start(&FUNC_UTIMES, procs, nprocs, &t);
		end_with_suffix(&FUNC_UTIMES, procs, nprocs, ntimes, &t, "NU");
	}

term:
	start(&FUNC_TERM, procs, nprocs, NULL);
	end(&FUNC_TERM, procs, nprocs, ntimes, NULL);

	watch_stderr_arg.is_end = 1;
	pthread_join(thr_watch_stderr, NULL);

	/* waitpid */
	for (i = 0; i < nprocs; i++) {
		int status;
		waitpid(procs[i].pid, &status, 0);
#if 0 /* ignore: _exit(0) only */
		if (WEXITSTATUS(status) != 0)
			fprintf(stderr, "[pid=%ld] error\n",
				(long int) procs[i].pid);
#endif
		close(procs[i].pipe_stdin[1]);
		close(procs[i].pipe_stdout[0]);
		close(procs[i].pipe_stderr[0]);
	}

	free(procs);

	return (0);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h" /* gfarm_sigpipe_ignore() */
#include "nanosec.h"
#include "thrsubr.h"

#include "gfprep.h"
#include "gfarm_parallel.h"

#define GFPARA_HANDLE_LIST_MAX 32
static int is_parent = 1;
static int n_handle_list = 0;
static gfpara_t *handle_list[GFPARA_HANDLE_LIST_MAX];

struct gfpara {
	pthread_t thread;
	int n_procs;
	gfpara_proc_t *procs;
	int (*func_send)(FILE *, gfpara_proc_t *, void *, int);
	void *param_send;
	int (*func_recv)(FILE *, gfpara_proc_t *, void *);
	void *param_recv;
	void *(*func_end)(void *);
	void *param_end;
	int started;
	int interrupt;
	int timeout_msec;

	pthread_t watch_stderr;
	int watch_stderr_end;
	pthread_mutex_t watch_stderr_mutex;
};


static void gfpara_watch_stderr_stop(gfpara_t *handle);
static void gfpara_fatal(const char *, ...) GFLOG_PRINTF_ARG(1, 2);
static void
gfpara_fatal(const char *format, ...)
{
	va_list ap;
	int i;

	if (is_parent)
		for (i = 0; i < n_handle_list; i++)
			if (handle_list[i] != NULL &&
			    handle_list[i]->watch_stderr_end == 0)
				gfpara_watch_stderr_stop(handle_list[i]);
	fprintf(stderr, "EXIT: error occurred: ");
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

gfpara_proc_t *
gfpara_procs_get(gfpara_t *handle)
{
	return (handle->procs);
}

struct gfpara_proc {
	pthread_t thread;
	gfpara_t *handle;
	pid_t pid;
	FILE *in;
	FILE *out;
	FILE *err;
	void *data; /* any */
	int working;
};

pid_t
gfpara_pid_get(gfpara_proc_t *proc)
{
	return (proc->pid);
}

void *
gfpara_data_get(gfpara_proc_t *proc)
{
	return (proc->data);
}

void
gfpara_data_set(gfpara_proc_t *proc, void *data)
{
	proc->data = data;
}

static void *
gfpara_watch_stderr(void *arg)
{
	gfpara_t *handle = arg;
	fd_set fdset_tmp, fdset_orig;
	int fd, i, retv, n_eof;
	char line[4096];
	int size = sizeof(line);
	int maxfd = 0;
	struct timeval tv;

	FD_ZERO(&fdset_orig);
	for (i = 0; i < handle->n_procs; i++) {
		fd = fileno(handle->procs[i].err);
		FD_SET(fd, &fdset_orig);
		if (fd > maxfd)
			maxfd = fd;
	}
	setvbuf(stderr, (char *) NULL, _IOLBF, 0);
	n_eof = 0;
	while (n_eof < handle->n_procs) {
		memcpy(&fdset_tmp, &fdset_orig, sizeof(fd_set));
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		retv = select(maxfd + 1, &fdset_tmp, NULL, NULL, &tv);
		if (retv == -1) {
			fprintf(stderr,
				"failed to watch stderr: select: %s\n",
				strerror(errno));
			continue;
		} else if (retv == 0) { /* timeout */
			int is_end;
			gfarm_mutex_lock(&handle->watch_stderr_mutex,
					 "gfpara_watch_stderr",
					 "watch_stderr_mutex");
			is_end = handle->watch_stderr_end;
			gfarm_mutex_unlock(&handle->watch_stderr_mutex,
					   "gfpara_watch_stderr",
					   "watch_stderr_mutex");
			if (is_end)
				break;
			continue;
		}
		for (i = 0; i < handle->n_procs; i++) {
			fd = fileno(handle->procs[i].err);
			if (!FD_ISSET(fd, &fdset_tmp))
				continue;
			if (fgets(line, size, handle->procs[i].err) == NULL) {
				/* EOF: child exited */
				n_eof++;
				FD_CLR(fd, &fdset_orig);
				continue;
			}
			fprintf(stderr, "[pid=%ld] %s",
				(long) handle->procs[i].pid, line);
		}
	}
	return (NULL);
}

static void
gfpara_watch_stderr_start(gfpara_t *handle)
{
	int eno;

	gfarm_mutex_init(&handle->watch_stderr_mutex,
			 "gfpara_watch_stderr_start", "watch_stderr_mutex");
	handle->watch_stderr_end = 0;
	eno = pthread_create(&handle->watch_stderr, NULL,
	    gfpara_watch_stderr, handle);
	if (eno != 0)
		gfpara_fatal("pthread_create: %s", strerror(eno));
}

static void
gfpara_watch_stderr_stop(gfpara_t *handle)
{
	gfarm_mutex_lock(&handle->watch_stderr_mutex,
			 "gfpara_watch_stderr_stop", "watch_stderr_mutex");
	handle->watch_stderr_end = 1;
	gfarm_mutex_unlock(&handle->watch_stderr_mutex,
			 "gfpara_watch_stderr_stop", "watch_stderr_mutex");
	pthread_join(handle->watch_stderr, NULL);
}

/* If gfarm_initialize() will be called in children, do not call this
 * function after gfarm_initialize(). */
/* The func_send()/func_recv() must be multi-thread safe. */
gfarm_error_t
gfpara_init(gfpara_t **handlep, int n_procs,
	int (*func_child)(void *param, FILE *from_parent, FILE *to_parent),
	void *param_child,
	int (*func_send)(FILE *child_in, gfpara_proc_t *proc, void *param,
			 int stop),
	void *param_send,
	int (*func_recv)(FILE *child_out, gfpara_proc_t *proc, void *param),
	void *param_recv,
	void *(*func_end)(void *param), void *param_end)
{
	int i, j;
	gfpara_proc_t *procs;
	gfpara_t *handle;

	if (n_handle_list >= GFPARA_HANDLE_LIST_MAX) {
		fprintf(stderr, "too many called gfpara_init()\n");
		return (GFARM_ERR_TOO_MANY_OPEN_FILES);
	}

	GFARM_MALLOC(handle);
	GFARM_MALLOC_ARRAY(procs, n_procs);
	if (handle == NULL || procs == NULL)
		gfpara_fatal("no memory: n_procs=%d", n_procs);
	handle->watch_stderr_end = 1;

	fflush(stdout); /* Don't send buffer to child */
	fflush(stderr); /* Don't send buffer to child */
	gfarm_sigpipe_ignore();
	for (i = 0; i < n_procs; i++) {
		/* Don't write to stdout and stderr in this loop */
		pid_t pid;
		int pipe_in[2];
		int pipe_out[2];
		int pipe_stderr[2];

		if (pipe(pipe_in) == -1)
			gfpara_fatal("pipe: %s", strerror(errno));
		if (pipe(pipe_out) == -1)
			gfpara_fatal("pipe: %s", strerror(errno));
		if (pipe(pipe_stderr) == -1)
			gfpara_fatal("pipe: %s", strerror(errno));
		if ((pid = fork()) == -1)
			gfpara_fatal("fork: %s", strerror(errno));
		else if (pid == 0) {
			int fd;
			FILE *from_parent;
			FILE *to_parent;

			for (j = 0; j < i; j++) {
				fclose(procs[j].in);
				fclose(procs[j].out);
				fclose(procs[j].err);
			}
			free(handle);
			free(procs);
			is_parent = 0;
			close(pipe_in[1]);
			close(pipe_out[0]);
			close(pipe_stderr[0]);
			close(0);
			fd = open("/dev/null", O_RDONLY);
			dup2(fd, 1);
			close(1);
			from_parent = fdopen(pipe_in[0], "r");
			to_parent = fdopen(pipe_out[1], "w");
			dup2(pipe_stderr[1], 2);
			close(pipe_stderr[1]);
			setvbuf(to_parent, (char *) NULL, _IOLBF, 0);
			setvbuf(stderr, (char *) NULL, _IOLBF, 0);

			func_child(param_child, from_parent, to_parent);

			close(pipe_in[0]);
			close(pipe_out[1]);
			close(2);
			_exit(0);
		}
		close(pipe_in[0]);
		close(pipe_out[1]);
		close(pipe_stderr[1]);
		procs[i].pid = pid;
		procs[i].in = fdopen(pipe_in[1], "w");
		if (procs[i].in == NULL)
			gfpara_fatal("fdopen: %s", strerror(errno));
		procs[i].out = fdopen(pipe_out[0], "r");
		if (procs[i].out == NULL)
			gfpara_fatal("fdopen: %s", strerror(errno));
		procs[i].err = fdopen(pipe_stderr[0], "r");
		if (procs[i].err == NULL)
			gfpara_fatal("fdopen: %s", strerror(errno));
		setvbuf(procs[i].in, (char *) NULL, _IOLBF, 0);
		procs[i].data = NULL;
		procs[i].working = 0;
		procs[i].handle = handle;
	}
	handle->n_procs = n_procs;
	handle->procs = procs;
	handle->func_send = func_send;
	handle->param_send = param_send;
	handle->func_recv = func_recv;
	handle->param_recv = param_recv;
	handle->func_end = func_end;
	handle->param_end = param_end;
	handle->interrupt = GFPARA_INTR_RUN;
	handle->started = 0;

	*handlep = handle;

	handle_list[n_handle_list++] = handle;

	return (GFARM_ERR_NO_ERROR);
}

static int
is_available(int fd, int pid, int is_readable)
{
	fd_set fdset;
	struct timeval tv;
	int retv;

	if (pid >= 0) {
		retv = kill(pid, 0);
		if (retv == -1)
			return (0);
		/* alive */
	}
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	tv.tv_sec = 0;
	tv.tv_usec = 100;
	if (is_readable)
		retv = select(fd + 1, &fdset, NULL, NULL, &tv);
	else
		retv = select(fd + 1, NULL, &fdset, NULL, &tv);
	if (retv != 1)
		return (0);

	return (1); /* available */
}

#define IS_READABLE(fd, pid) is_available(fd, pid, 1)
#define IS_WRITABLE(fd, pid) is_available(fd, pid, 0)

void
gfpara_recv_purge(FILE *in)
{
	size_t retv;
	int fd;
	char b;

	fd = fileno(in);
	do {
		if (!IS_READABLE(fd, -1))
			break;
		retv = fread(&b, 1, 1, in);
	} while (retv > 0);
}

void
gfpara_recv_int(FILE *in, gfarm_int32_t *valp)
{
	size_t retv = fread(valp, sizeof(gfarm_int32_t), 1, in);
	if (retv != 1)
		gfpara_fatal("cannot receive message (int32)");
}

void
gfpara_recv_int64(FILE *in, gfarm_int64_t *valp)
{
	size_t retv = fread(valp, sizeof(gfarm_int64_t), 1, in);
	if (retv != 1)
		gfpara_fatal("cannot receive message (int64)");
}

void
gfpara_recv_string(FILE *in, char **strp)
{
	int recv_size, str_size, retv;
	char *str;

	gfpara_recv_int(in, &recv_size);
	if (recv_size < 0)
		gfpara_fatal("unexpected receive buffer size: %d", recv_size);
	str_size = recv_size + 1; /* for '\0' */
	if (str_size <= recv_size)
		gfpara_fatal("receive buffer size is orverflow");
	GFARM_MALLOC_ARRAY(str, str_size);
	if (str == NULL)
		gfpara_fatal("cannot allocate buffer for receive");
	if (recv_size > 0) {
		retv = fread(str, recv_size, 1, in);
		if (retv != 1)
			gfpara_fatal("cannot receive message (string): %d",
				     retv);
	} /* else: recv_size == 0 */
	str[recv_size] = '\0';
	*strp = str;
}

void
gfpara_send_int(FILE *out, gfarm_int32_t i)
{
	size_t retv = fwrite(&i, sizeof(gfarm_int32_t), 1, out);
	fflush(out);
	if (retv != 1)
		gfpara_fatal("cannot send message (int32)");
}

void
gfpara_send_int64(FILE *out, gfarm_int64_t i)
{
	size_t retv = fwrite(&i, sizeof(gfarm_int64_t), 1, out);
	fflush(out);
	if (retv != 1)
		gfpara_fatal("cannot send message (int64)");
}

void
gfpara_send_string(FILE *out, const char *format, ...)
{
	va_list ap;
	char *str = NULL;
	int len;
	size_t retv;

	va_start(ap, format);
	len = gfprep_vasprintf(&str, format, ap);
	va_end(ap);
	if (len == -1)
		gfpara_fatal("cannot allocate buffer for send");
	gfpara_send_int(out, len);
	if (len > 0) {
		retv = fwrite(str, (size_t) len, 1, out);
		if (retv != 1)
			gfpara_fatal("cannot send message (string): "
				     "fwrite=%ld", (long) retv);
	}
	fflush(out);
	free(str);
}

static void *
gfpara_thread(void *param)
{
	gfpara_proc_t *proc = param;
	gfpara_t *handle = proc->handle;
	int (*func_send)(FILE *, gfpara_proc_t *, void *, int)
		= handle->func_send;
	void *param_send = handle->param_send;
	int (*func_recv)(FILE *, gfpara_proc_t *, void *) = handle->func_recv;
	void *param_recv = handle->param_recv;
	fd_set fdset_tmp, fdset_orig;
	struct timeval tv;
	int retv;
	int fd_in = fileno(proc->in);
	int fd_out = fileno(proc->out);

	FD_ZERO(&fdset_orig);
	/* watch output of child */
	FD_SET(fd_out, &fdset_orig);
	for (;;) {
		if (!IS_WRITABLE(fd_in, proc->pid))
			gfpara_fatal("no child process: pid=%ld\n",
				(long int) proc->pid);
		retv = func_send(proc->in, proc, param_send,
		    handle->interrupt != GFPARA_INTR_RUN ? 1 : 0);
		if (retv == GFPARA_END)
			goto end;
		else if (retv == GFPARA_FATAL)
			gfpara_fatal("error before sending to subprocess");
		assert(retv == GFPARA_NEXT);
		for (;;) {
			if (handle->interrupt == GFPARA_INTR_TERM) {
				tv.tv_sec = handle->timeout_msec / 1000;
				tv.tv_usec = (handle->timeout_msec % 1000)
					* 1000;
			} else {
				tv.tv_sec = 2;
				tv.tv_usec = 0;
			}
			memcpy(&fdset_tmp, &fdset_orig, sizeof(fd_set));
			retv = select(fd_out + 1, &fdset_tmp, NULL, NULL, &tv);
			if (retv > 0)
				break;  /* readable */
			else if (retv == 0) { /* timeout */
				if (handle->interrupt == GFPARA_INTR_TERM)
					goto end;
			} else
				gfpara_fatal("select error: %s\n",
					strerror(errno));
		}
		/* check stdout */
		assert(FD_ISSET(fd_out, &fdset_tmp));
		retv = func_recv(proc->out, proc, param_recv);
		if (retv == GFPARA_END)
			goto end;
		else if (retv == GFPARA_FATAL)
			gfpara_fatal("error from subprocess");
		assert(retv == GFPARA_NEXT);
	}
end:
	return (NULL);
}

static void *
gfpara_communicate(void *param)
{
	int i;
	gfpara_t *handle = param;
	int n_procs = handle->n_procs;
	gfpara_proc_t *procs = handle->procs;

	gfpara_watch_stderr_start(handle);

	for (i = 0; i < n_procs; i++) {
		int eno = pthread_create(&procs[i].thread, NULL,
					 gfpara_thread, &procs[i]);
		if (eno == 0)
			procs[i].working = 1;
		else
			fprintf(stderr, "pthread_create failed: %s\n",
				strerror(eno));
	}
	for (i = 0; i < n_procs; i++) {
		if (procs[i].working)
			pthread_join(procs[i].thread, NULL);
	}
	/* child processes done */

	gfpara_watch_stderr_stop(handle);

	if (handle->func_end != NULL)
		handle->func_end(handle->param_end);

	return (NULL);
}

gfarm_error_t
gfpara_start(gfpara_t *handle)
{
	int eno = pthread_create(&handle->thread, NULL,
	    gfpara_communicate, handle); /* thread for func_end */

	if (eno == 0)
		handle->started = 1;
	return (gfarm_errno_to_error(eno));
}

static int
waitpid_timeout(pid_t pid, int *status, int options, long long timeout_msec)
{
	int rv;
	long long n = 0;
#define SLEEP_MSEC 10 /* 10 msec. */

	for (;;) {
		rv = waitpid(pid, status, options | WNOHANG);
		if (rv != 0) /* success or error */
			return (rv);
		if (n > timeout_msec)
			return (0); /* timeout */
		/* child is running */
		gfarm_nanosleep(SLEEP_MSEC * GFARM_MILLISEC_BY_NANOSEC);
		n += SLEEP_MSEC;
	}
}

gfarm_error_t
gfpara_join(gfpara_t *handle)
{
	int eno, i, n_procs = handle->n_procs;
	gfpara_proc_t *procs = handle->procs;

	if (handle->started)
		eno = pthread_join(handle->thread, NULL);
	else
		eno = 0;

	for (i = 0; i < n_procs; i++) {
		int rv = waitpid_timeout(procs[i].pid, NULL, WNOHANG, 10);

		if (rv > 0) {
			procs[i].pid = 0;
			fclose(procs[i].in);
			fclose(procs[i].out);
			fclose(procs[i].err);
		}
	}

	for (i = 0; i < n_procs; i++) {
		int rv;

		if (procs[i].pid == 0)
			continue;

		rv = waitpid_timeout(procs[i].pid, NULL, 0, 1000); /* 1 sec */
		if (rv == 0) {
			fprintf(stderr, "pid=%ld: terminated\n",
			    (long)procs[i].pid);
			kill(procs[i].pid, SIGKILL);
		} else if (rv == -1) {
			if (errno != ECHILD)
				fprintf(stderr, "wait_pid(%ld): %s\n",
				    (long)procs[i].pid, strerror(errno));
		}
		fclose(procs[i].in);
		fclose(procs[i].out);
		fclose(procs[i].err);
	}

	free(handle->procs);
	free(handle);
	return (gfarm_errno_to_error(eno));
}

gfarm_error_t
gfpara_terminate(gfpara_t *handle, int timeout_msec)
{
	handle->timeout_msec = timeout_msec;
	handle->interrupt = GFPARA_INTR_TERM;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfpara_stop(gfpara_t *handle)
{
	handle->interrupt = GFPARA_INTR_STOP;
	return (GFARM_ERR_NO_ERROR);
}

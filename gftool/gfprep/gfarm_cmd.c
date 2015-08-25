/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

pid_t
gfarm_popen3(char *const cmd_args[], int *fd_in, int *fd_out, int *fd_err)
{
	int p_stdin[2], p_stdout[2], p_stderr[2];
	pid_t pid;

	if (pipe(p_stdin) != 0) {
		perror("pipe");
		return (-1);
	}
	if (pipe(p_stdout) != 0) {
		perror("pipe");
		close(p_stdin[0]);
		close(p_stdin[1]);
		return (-1);
	}
	if (pipe(p_stderr) != 0) {
		perror("pipe");
		close(p_stdin[0]);
		close(p_stdin[1]);
		close(p_stdout[0]);
		close(p_stdout[1]);
		return (-1);
	}

	pid = fork();
	if (pid < 0)
		return (pid);
	else if (pid == 0) {
		close(p_stdin[1]);
		close(p_stdout[0]);
		close(p_stderr[0]);
		dup2(p_stdin[0], STDIN_FILENO);
		dup2(p_stdout[1], STDOUT_FILENO);
		dup2(p_stderr[1], STDERR_FILENO);
		close(p_stdin[0]);
		close(p_stdout[1]);
		close(p_stderr[1]);

		execvp(cmd_args[0], cmd_args);
		perror("execvp");
		_exit(1);
	}

	close(p_stdin[0]);
	if (fd_in == NULL)
		close(p_stdin[1]);
	else
		*fd_in = p_stdin[1];

	close(p_stdout[1]);
	if (fd_out == NULL)
		close(p_stdout[0]);
	else
		*fd_out = p_stdout[0];

	close(p_stderr[1]);
	if (fd_err == NULL)
		close(p_stderr[0]);
	else
		*fd_err = p_stderr[0];

	return (pid);
}

#define BUFSIZE 65536

struct cmd_out {
	int fd_out;
	int fd_err;
	int enable_out;
	int enable_err;
};

static void *
print_stdout_stderr(void *arg)
{
	int stdout_eof, stderr_eof;
	char buf[BUFSIZE];
	int r_len;
	struct cmd_out *o = arg;
	fd_set fdset_r;
	int nfds, retv;

	signal(SIGPIPE, SIG_IGN);

	stdout_eof = stderr_eof = 0;
	while (stdout_eof == 0 || stderr_eof == 0) {
		FD_ZERO(&fdset_r);
		nfds = 0;
		if (stdout_eof == 0) {
			FD_SET(o->fd_out, &fdset_r);
			nfds = o->fd_out;
		}
		if (stderr_eof == 0) {
			FD_SET(o->fd_err, &fdset_r);
			if (o->fd_err > nfds)
				nfds = o->fd_err;
		}
		retv = select(nfds + 1, &fdset_r, NULL, NULL, NULL);
		if (retv == -1) {
			perror("select");
			break;
		}

		/* receive STDOUT of the command */
		if (stdout_eof == 0 && FD_ISSET(o->fd_out, &fdset_r)) {
			r_len = read(o->fd_out, buf, BUFSIZE);
			if (r_len == 0) { /* EOF */
				stdout_eof = 1;
			} else if (r_len > 0) {
				if (o->enable_out)
					fwrite(buf, r_len, 1, stdout);
			} else {
				perror("read stdout from command");
				stdout_eof = 1;
			}
		}

		/* receive STDERR of the command */
		if (stderr_eof == 0 && FD_ISSET(o->fd_err, &fdset_r)) {
			r_len = read(o->fd_err, buf, BUFSIZE);
			if (r_len == 0) {
				stderr_eof = 1;
			} else if (r_len > 0) {
				if (o->enable_err)
					fwrite(buf, r_len, 1, stderr);
			} else {
				perror("read stderr from command");
				stderr_eof = 1;
			}
		}
	}
	close(o->fd_out);
	close(o->fd_err);

	return (NULL);
}

int
gfarm_cmd_exec(char *const args[], int (*func_stdin)(int fd, void *arg),
	void *func_stdin_arg, int enable_stdout, int enable_stderr)
{
	pid_t pid;
	int cmd_in, cmd_out, cmd_err, eno, retv, status;
	pthread_t t;
	struct cmd_out out;

	pid = gfarm_popen3(args, &cmd_in, &cmd_out, &cmd_err);
	if (pid == -1)
		return (-1);

	out.fd_out = cmd_out;
	out.fd_err = cmd_err;
	out.enable_out = enable_stdout;
	out.enable_err = enable_stderr;
	eno = pthread_create(&t, NULL, print_stdout_stderr, &out);
	if (eno != 0) {
		fprintf(stderr, "pthread_create: %s", strerror(eno));
		return (eno);
	}

	gfarm_sigpipe_ignore();
	retv = func_stdin(cmd_in, func_stdin_arg);

	pthread_join(t, NULL);

	if (waitpid(pid, &status, 0) > 0) {
		if (retv != 0)
			return (retv);
		else
			return (WEXITSTATUS(status));
	}
	perror("waitpid");
	return (-1);
}

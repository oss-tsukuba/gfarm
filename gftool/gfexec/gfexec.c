/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <gfarm/gfarm.h>
#include <openssl/evp.h> /* "gfs_pio.h" needs this for now */
#include "gfs_pio.h"

#define PROGRAM_NAME "gfexec"
static char progname[] = PROGRAM_NAME;

static void
print_usage()
{
	fprintf(stderr, "usage: %s <program> <args> ...\n", progname);
	exit(2);
}

char *
search_path(char *command)
{
	int command_len, dir_len;
	char *result, *path;
	struct stat st;
	char buf[PATH_MAX];

	if (strchr(command, '/') != NULL) { /* absolute or relative path */
		result = command;
		goto finish;
	}
	command_len = strlen(command);
	path = getenv("PATH");
	if (path == NULL)
		path = "/bin:/usr/bin:/usr/ucb:/usr/local/bin";
	do {
		dir_len = strcspn(path, ":");
		if (dir_len == 0) {
			if (command_len + 1 > sizeof(buf)) {
				fprintf(stderr, "%s: %s: path too long\n",
				    progname, command);
				exit(1);
			}
			result = command;
		} else {
			if (dir_len + command_len + 2 > sizeof(buf)) {
				fprintf(stderr,
				    "%s: %s: path too long (%s)\n",
				    progname, command, path);
				exit(1);
			}
			memcpy(buf, path, dir_len);
			buf[dir_len] = '/';
			strcpy(buf + dir_len + 1, command);
			result = buf;
		}
		if (access(result, X_OK) == 0 &&
		   stat(result, &st) == 0 &&
		   (st.st_mode & S_IFMT) == S_IFREG)
			goto finish;
		path += dir_len;
	} while (*path++ == ':');
	/* not found in $PATH */
	result = command;
finish:
	result = strdup(result);
	if (result == NULL) {
		fprintf(stderr, "%s: no memory\n", progname);
		exit(1);
	}
	return (result);
}

int
main(int argc, char *argv[], char *envp[])
{
	char *e, *gfarm_url, *local_path, **new_env, *cwd_env;
	int i, j, status, envc, rank, nodes;
	pid_t pid;
	char rankbuf[sizeof("gfarm_index_") + GFARM_INT64STRLEN * 2 + 2];
	char nodesbuf[sizeof("gfarm_nfrags_") + GFARM_INT64STRLEN * 2 + 2];
	char flagsbuf[sizeof("gfarm_flags_") + GFARM_INT64STRLEN + 1 + 2 + 1];
	char cwdbuf[PATH_MAX * 2];

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n", progname, e);
		exit(1);
	}

	/*
	 * don't use getopt(3) here, because getopt(3) in glibc refers
	 * argv[] passed to main(), instead of argv[] passed to getopt(3).
	 */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			break;
		for (j = 1; argv[i][j] != '\0'; j++) {
			switch (argv[i][j]) {
			case 'h':
			case '?':
				print_usage();
			default:
				fprintf(stderr, "%s: invalid option -- %c\n",
				    progname, argv[i][j]);
				print_usage();
			}
		}
	}
	argc -= i;
	argv += i;
	if (argc == 0)
		print_usage();

	e = gfs_realpath(argv[0], &gfarm_url);
	if (e != NULL) {
		/* XXX check `e' */
		local_path = search_path(argv[0]);
	} else {
		e = gfarm_url_program_get_local_path(gfarm_url, &local_path);
		if (e != NULL) {
			fprintf(stderr, "%s: replicating %s: %s\n",
			    progname, gfarm_url, e);
			exit(1);
		}
	}


	/*
	 * the followings are only needed for pid==0 case.
	 * but isn't it better to check errors before fork(2)?
	 *
	 * If gfs_pio_get_node_{rank,size}() fails, continue to
	 * execute as a single process (not parallel processes).
	 */
	e = gfs_pio_get_node_rank(&rank);
	if (e != NULL)
		rank = 0;
	e = gfs_pio_get_node_size(&nodes);
	if (e != NULL)
		nodes = 1;
	for (envc = 0; envp[envc] != NULL; envc++)
		;
	new_env = malloc(sizeof(*new_env) * (envc + 4 + 1));
	e = gfs_getcwd(cwdbuf, sizeof cwdbuf);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot get current directory: %s\n",
		    progname, e);
		exit(1);
	}
	if ((cwd_env = malloc(strlen(cwdbuf) + sizeof("GFS_PWD="))) == NULL) {
		fprintf(stderr, "%s: no memory for GFS_PWD=%s\n",
		    progname, cwdbuf);
		exit(1);
	}
	sprintf(cwd_env, "GFS_PWD=%s", cwdbuf);


	if (gf_stdout == NULL && gf_stderr == NULL) {
		/* what we need is to call exec(2) */
		pid = 0;
	} else {
		/*
		 * we have to call fork(2) and exec(2), to close
		 * gf_stdout and gf_stderr by calling gfarm_terminate()
		 * after the child program finished.
		 */
		pid = fork();
	}

	switch (pid) {
	case -1:
		perror(PROGRAM_NAME ": fork");
		status = 255;
		break;
	case 0:
		pid = getpid();
		for (envc = 0; envp[envc] != NULL; envc++)
			new_env[envc] = envp[envc];
		sprintf(rankbuf, "gfarm_index_%lu=%d", (long)pid, rank);
		new_env[envc++] = rankbuf;
		sprintf(nodesbuf, "gfarm_nfrags_%lu=%d", (long)pid, nodes);
		new_env[envc++] = nodesbuf;
		sprintf(flagsbuf, "gfarm_flags_%lu=%s%s", (long)pid,
		    gf_profile ? "p" : "",
		    gf_on_demand_replication ? "r" : "");
		new_env[envc++] = flagsbuf;
		new_env[envc++] = cwd_env;
		new_env[envc++] = NULL;

		/*
		 * don't call gfarm_terminate() here, because:
		 * - it closes gf_stdout and gf_stderr.
		 * - it causes "gfarm_terminate: Can't contact LDAP server"
		 *   on the parent process.
		 */
		execve(local_path, argv, new_env);
		perror(local_path);
		_exit(255);
	default:
		if (waitpid(pid, &status, 0) == -1) {
			perror(PROGRAM_NAME ": waitpid");
			status = 255;
		} else if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: signal %d received%s.\n",
			    gfarm_host_get_self_name(), WTERMSIG(status),
			    WCOREDUMP(status) ? " (core dumped)" : "");
			status = 255;
		} else {
			status = WEXITSTATUS(status);
		}
		break;
	}

	/* not to display profile statistics on gfarm_terminate() */
	gfs_profile(gf_profile = 0);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n", progname, e);
		exit(1);
	}
	exit(status);
}

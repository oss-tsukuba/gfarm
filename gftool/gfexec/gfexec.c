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
	fprintf(stderr, "usage: %s <program> [-N <nodes>] [-I <index>] [-s] <args> ...\n", progname);
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
	int i, j, status, envc, rank = -1, nodes = -1;
	pid_t pid;
	static const char env_node_rank[] = "GFARM_NODE_RANK=";
	static const char env_node_size[] = "GFARM_NODE_SIZE=";
	static const char env_flags[] = "GFARM_FLAGS=";
	static const char env_gfs_pwd[] = "GFS_PWD=";
	char rankbuf[sizeof(env_node_rank) + GFARM_INT64STRLEN];
	char nodesbuf[sizeof(env_node_size) + GFARM_INT64STRLEN];
	char flagsbuf[sizeof(env_flags) + 3];
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
			case 'I':
				if (argv[i][j + 1] != '\0') {
					rank = strtol(&argv[i][j+1], NULL, 0);
					j += strlen(&argv[i][j + 1]);
				} else if (i + 1 < argc) {
					rank = strtol(argv[++i], NULL, 0);
					j = strlen(argv[i]) - 1;
				} else {
					fprintf(stderr,
					    "%s: -I: missing argument\n",
					    progname);
					print_usage();
				}
				break;
			case 'N':
				if (argv[i][j + 1] != '\0') {
					nodes = strtol(&argv[i][j+1], NULL, 0);
					j += strlen(&argv[i][j + 1]);
				} else if (i + 1 < argc) {
					nodes = strtol(argv[++i], NULL, 0);
					j = strlen(argv[i]) - 1;
				} else {
					fprintf(stderr,
					    "%s: -N: missing argument\n",
					    progname);
					print_usage();
				}
				break;
			case 's':
				rank = 0;
				nodes = 1;
				break;
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
	if (rank == -1) {
		e = gfs_pio_get_node_rank(&rank);
		if (e != NULL)
			rank = 0;
	}
	if (nodes == -1) {
		e = gfs_pio_get_node_size(&nodes);
		if (e != NULL)
			nodes = 1;
	}
	for (envc = 0; envp[envc] != NULL; envc++)
		;
	new_env = malloc(sizeof(*new_env) * (envc + 4 + 1));
	e = gfs_getcwd(cwdbuf, sizeof cwdbuf);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot get current directory: %s\n",
		    progname, e);
		exit(1);
	}
	if ((cwd_env = malloc(strlen(cwdbuf) + sizeof(env_gfs_pwd))) == NULL) {
		fprintf(stderr, "%s: no memory for %s%s\n",
		    progname, env_gfs_pwd, cwdbuf);
		exit(1);
	}
	envc = 0;
	for (i = 0; (e = envp[i]) != NULL; i++) {
		if (memcmp(e, env_node_rank, sizeof(env_node_rank) -1 ) != 0 &&
		    memcmp(e, env_node_size, sizeof(env_node_size) -1 ) != 0 &&
		    memcmp(e, env_flags, sizeof(env_flags) - 1 ) != 0 &&
		    memcmp(e, env_gfs_pwd, sizeof(env_gfs_pwd) - 1) != 0)
			new_env[envc++] = e;
	}
	sprintf(rankbuf, "%s%d", env_node_rank, rank);
	new_env[envc++] = rankbuf;
	sprintf(nodesbuf, "%s%d", env_node_size, nodes);
	new_env[envc++] = nodesbuf;
	sprintf(flagsbuf, "%s%s%s%s", env_flags,
	    gf_profile ? "p" : "",
	    gf_on_demand_replication ? "r" : "",
	    gf_hook_default_global ? "g" : "");
	new_env[envc++] = flagsbuf;
	sprintf(cwd_env, "%s%s", env_gfs_pwd, cwdbuf);
	new_env[envc++] = cwd_env;
	new_env[envc++] = NULL;


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
#if 0	/*
	 * gfarm_terminate() should not be called here
	 * because we need to keep the LDAP connection.
	 */
		if (gf_stdout == NULL && gf_stderr == NULL) {
			/* not to display profile statistics on gfarm_terminate() */
			gfs_profile(gf_profile = 0);

			e = gfarm_terminate();
			if (e != NULL)
				fprintf(stderr,
				    "%s (child): gfarm_terminate: %s\n",
				    progname, e);
		} else {
			/*
			 * otherwise don't call gfarm_terminate(), because:
			 * - it closes gf_stdout and gf_stderr.
			 * - it causes:
			 *   "gfarm_terminate: Can't contact LDAP server"
			 *   on the parent process.
			 */
		}
#endif
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

/*
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <gfarm/gfarm.h>
#include <openssl/evp.h> /* "gfs_pio.h" needs this for now */
#include "gfs_pio.h"

#define PROGRAM_NAME "gfexec"
static char progname[] = PROGRAM_NAME;

#define BOURNE_SHELL "/bin/sh"

static void
print_usage()
{
	fprintf(stderr, "usage: %s <program> [-N <nodes>] [-I <index>] [-s] "
			"<args> ...\n", progname);
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

static char gfarm_prefix[] = "/gfarm";

/*
 * Shared objects in gfarm file system cannot be dynamically linked 
 * because _dl_*() defined in /lib/ld-linux cannot be hooked.
 * So, we replicate shared objects to local machine and rewrite paths in
 * LD_LIBRARY_PATH to local spool directory path, for example,
 * /gfarm/lib -> /var/spool/gfarm/lib.
 */
char *
replicate_so_from_dir_to_local(char **dirp)
{
	char *e, *so_pat, *local_path, *local_dir;
	int i, rv;
	gfarm_stringlist paths;
	gfs_glob_t types;
	static char so_pat_template[] = "gfarm:%s/*.so*";

	e = gfarm_stringlist_init(&paths);
	if (e != NULL)
		goto finish;
	e = gfs_glob_init(&types);
	if (e != NULL)
		goto free_paths;
	so_pat = malloc(strlen(*dirp) + sizeof(so_pat_template)
			- sizeof(gfarm_prefix) + 1);	
	if (so_pat == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_types;
	}
	sprintf(so_pat, so_pat_template, *dirp + sizeof(gfarm_prefix) - 1);
	e = gfs_glob(so_pat, &paths, &types);
	if (e != NULL)
		goto free_so_pat;
	local_dir = NULL;
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *p;

		p = gfarm_stringlist_elem(&paths, i);
		if (strcmp(p, so_pat) == 0) /* no "*.so*" file in dir */
			goto free_so_pat;
		e = gfarm_url_execfile_replicate_to_local(p, &local_path);
		if (e == GFARM_ERR_NO_MEMORY) {
			if (local_dir != NULL)
				free (local_dir);
			goto free_so_pat;
		} else if (e != NULL) {
			/* XXX - error message should be displayed */
			continue;
		}
		p = strdup(local_path);
		if (p == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			if (local_dir != NULL)
				free (local_dir);
			goto free_so_pat;
		}
		p[strcspn(local_path, ":")] = '\0';
		unlink(p);
		rv = symlink(local_path, p);
		if (rv == -1) {
			perror(p);
			free(local_path);
			free(p);
			continue;
		}
		free(local_path);
		if (local_dir == NULL) {
			local_dir = strdup(dirname(p));
			if (local_dir == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				free(p);
				goto free_so_pat;
			}
		}
		free(p);
	}
	if (local_dir != NULL) {
		free(*dirp);	
		*dirp = local_dir;
	}

free_so_pat:
	free(so_pat);
free_types:
	gfs_glob_free(&types);
free_paths:
	gfarm_stringlist_free_deeply(&paths);
finish:
	return (e);
}

static char *
replicate_so()
{
	char *e, *ld_path, *new_ld_path, *dir;
	int new_ld_path_len, last_len, new_dir_len, rv;
	size_t dir_len;

	e = NULL;
	new_ld_path_len = sizeof("LD_LIBRARY_PATH=") - 1;
	new_ld_path = malloc(new_ld_path_len + 1);
	if (new_ld_path == NULL)
		return (GFARM_ERR_NO_MEMORY);
	strcpy(new_ld_path, "LD_LIBRARY_PATH=");
	ld_path = getenv("LD_LIBRARY_PATH");
	while (*ld_path != '\0') {
		char *p;

		dir_len = strcspn(ld_path, ":");
		dir = malloc(dir_len + 1);
		if (dir == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			goto finish;
		}
		memcpy(dir, ld_path, dir_len);
		dir[dir_len] = '\0';
		new_dir_len = dir_len;
		if (memcmp(dir, gfarm_prefix, sizeof(gfarm_prefix) - 1) == 0) {
			e = replicate_so_from_dir_to_local(&dir);
			if (e != NULL) {
				free(dir);
				goto finish;
			}
			new_dir_len = strlen(dir);
		}			
		last_len = new_ld_path_len;
		new_ld_path_len += new_dir_len;
		p = realloc(new_ld_path, new_ld_path_len + 1);		
		if (p == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			free(dir);
			goto finish;
		}
		new_ld_path = p;
		strcpy(new_ld_path + last_len, dir);
		free(dir);
		ld_path += dir_len;
		if (*ld_path == '\0') {
			new_ld_path[new_ld_path_len] = '\0';
		} else {
			new_ld_path[new_ld_path_len] = ':';
			new_ld_path_len += 1;
			ld_path += 1;
		}
	}
	rv = putenv(new_ld_path);
	if (rv == -1)
		e = strerror(errno);
finish:
	return (e);
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

	e = replicate_so();
	if (e != NULL) {
		fprintf(stderr, "%s: replicate_so: %s\n",
		    progname, e);
		exit(1);
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
		if (gf_stdout == NULL && gf_stderr == NULL) {
			/*
			 * not to display profile statistics
			 * on gfarm_terminate()
			 */
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
		execve(local_path, argv, new_env);
		if (errno != ENOEXEC) {
			perror(local_path);
		} else {
			/*
			 * argv[-1] must be available,
			 * because there should be "gfexec" at least.
			 */
			argv[-1] = BOURNE_SHELL;
			argv[0] = local_path;
			execve(BOURNE_SHELL, argv - 1, new_env);
		}
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

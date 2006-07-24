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

#include "config.h"	/* gfs_profile, ... */

#if !defined(WCOREDUMP) && defined(_AIX)
#define WCOREDUMP(status)	((status) & 0x80)
#endif

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

/*
 * Shared objects in gfarm file system cannot be dynamically linked 
 * because _dl_*() defined in /lib/ld-linux cannot be hooked.
 * So, we replicate shared objects to local machine and rewrite paths in
 * LD_LIBRARY_PATH to local spool directory path, for example,
 * /gfarm/lib -> /var/spool/gfarm/lib.
 */
/* XXX - the following two static functions are defined in hooks_subr.c. */
static char gfs_mntdir[] = "/gfarm";

static int
is_null_or_slash(const char c)
{
	return (c == '\0' || c == '/' || c == ':');
}

static int
is_mount_point(const char *path)
{
	return (*path == '/' &&
		memcmp(path, gfs_mntdir, sizeof(gfs_mntdir) - 1) == 0 &&
		is_null_or_slash(path[sizeof(gfs_mntdir) - 1]));
}

static char *
gfarm_url_localize(char *url, char **local_path)
{
	char *e, *canonic_path;

	*local_path = NULL;

	e = gfarm_url_make_path(url, &canonic_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_localize(canonic_path, local_path);
	free(canonic_path);
	return (e);
}

/* convert to the canonical path */
static char *
gfs_mntpath_canonicalize(char *dir, size_t size, char **canonic_path)
{
	char *e, *gfarm_file;
	
	*canonic_path = NULL;

	dir += sizeof(gfs_mntdir) - 1;
	size -= sizeof(gfs_mntdir) - 1;
	/* in '/gfarm/~' case, skip the first '/' */
	if (dir[0] == '/' && dir[1] == '~') {
		++dir;
		--size;
	}
	/*
	 * in '/gfarm' case, just convert to '\0'.
	 * ':' is a separator of the environment variable.
	 */
	else if (dir[0] == '\0' || dir[0] == ':') {
		*canonic_path = strdup("");
		if (*canonic_path == NULL)
			return (GFARM_ERR_NO_MEMORY);
		return (NULL);
	}
	GFARM_MALLOC_ARRAY(gfarm_file, size + 1);
	if (gfarm_file == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(gfarm_file, "%.*s", (int)size, dir);
	
	e = gfarm_canonical_path(gfarm_file, canonic_path);
	free(gfarm_file);
	return (e);
}

static char *
gfs_mntpath_localize(char *dir, size_t size, char **local_path)
{
	char *e, *canonic_path;

	*local_path = NULL;

	e = gfs_mntpath_canonicalize(dir, size, &canonic_path);
	if (e != NULL)
		return (e);

	e = gfarm_path_localize(canonic_path, local_path);
	free(canonic_path);
	return (e);
}

static char *
gfs_mntpath_to_url(char *path, size_t size, char **url)
{
	char *e, *canonic_path;

	*url = NULL;

	e = gfs_mntpath_canonicalize(path, size, &canonic_path);
	if (e != NULL)
		return (e);

	e = gfarm_path_canonical_to_url(canonic_path, url);
	free(canonic_path);
	return (e);
}


static char *
replicate_so_and_symlink(char *dir, size_t size)
{
	char *gfarm_url, *so_pat, *lpath;
	char *e, *e_save;
	int i, rv;
	gfarm_stringlist paths;
	gfs_glob_t types;
	static char so_pat_template[] = "/*.so*";

	e = gfs_mntpath_to_url(dir, size, &gfarm_url);
	if (e != NULL)
		return (e);

	GFARM_MALLOC_ARRAY(so_pat,
		strlen(gfarm_url) + sizeof(so_pat_template));
	if (so_pat == NULL) {
		free(gfarm_url);
		return (GFARM_ERR_NO_MEMORY);
	}
	sprintf(so_pat, "%s%s", gfarm_url, so_pat_template);
	free(gfarm_url);

	e_save = gfarm_stringlist_init(&paths);
	if (e_save != NULL)
		goto free_so_pat;
	e_save = gfs_glob_init(&types);
	if (e_save != NULL)
		goto free_paths;
	e_save = gfs_glob(so_pat, &paths, &types);
	if (e_save != NULL)
		goto free_types;

	/* XXX - should replicate so files in parallel */
	for (i = 0; i < gfarm_stringlist_length(&paths); i++) {
		char *gfarm_so, *local_so;

		gfarm_so = gfarm_stringlist_elem(&paths, i);
		if (strcmp(gfarm_so, so_pat) == 0) /* no "*.so*" file in dir */
			break;
		e = gfarm_url_execfile_replicate_to_local(gfarm_so, &lpath);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", gfarm_so, e);
			if (e_save == NULL)
				e_save = e;
			if (e == GFARM_ERR_NO_MEMORY)
				break;
			continue;
		}

		/* create a symlink */
		e = gfarm_url_localize(gfarm_so, &local_so);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", gfarm_so, e);
			if (e_save == NULL)
				e_save = e;
			if (e == GFARM_ERR_NO_MEMORY)
				break;
			free(lpath);
			continue;
		}
		unlink(local_so);
		rv = symlink(lpath, local_so);
		if (rv == -1) {
			perror(local_so);
			if (e_save == NULL)
				e_save = gfarm_errno_to_error(errno);
		}
		free(local_so);
		free(lpath);
	}
free_types:
	gfs_glob_free(&types);
free_paths:
	gfarm_stringlist_free_deeply(&paths);
free_so_pat:
	free(so_pat);
	return (e_save);
}

static char *
alloc_ldpath(size_t size)
{
	static char *ldpath = NULL;
	static int ldpath_len = 0;

	if (ldpath_len < size) {
		char *p;

		GFARM_REALLOC_ARRAY(p, ldpath, size);
		if (p == NULL)
			return (NULL);
		ldpath = p;
		ldpath_len = size;
	}
	return (ldpath);
}

static char *
modify_ld_library_path(void)
{
	char *e, *e_save = NULL, *ldpath, *nldpath;
	static char env_ldlibpath[] = "LD_LIBRARY_PATH";
	size_t len, dirlen;
	int rv;

	ldpath = getenv(env_ldlibpath);
	if (ldpath == NULL)
		return (NULL);

	len = sizeof(env_ldlibpath) + 1;
	nldpath = alloc_ldpath(1024); /* specify bigger size than len */
	if (nldpath == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(nldpath, "%s=", env_ldlibpath);
	while (*ldpath != '\0') {
		char *local_dir;
		int is_valid_local_dir = 0;

		dirlen = strcspn(ldpath, ":");
		if (is_mount_point(ldpath)) {
			e = gfs_mntpath_localize(ldpath, dirlen, &local_dir);
			if (e != NULL) {
				if (e != GFARM_ERR_NO_SUCH_OBJECT) {
					if (e_save == NULL)
						e_save = e;
				}
				goto skip_copying;
			}
			e = replicate_so_and_symlink(ldpath, dirlen);
			if (e != NULL)
				if (e_save == NULL)
					e_save = e;
			len += strlen(local_dir);
			is_valid_local_dir = 1;
		}
		else
			len += dirlen;

		nldpath = alloc_ldpath(len);
		if (nldpath == NULL) {
			if (is_valid_local_dir)
				free(local_dir);
			return (GFARM_ERR_NO_MEMORY);
		}
		if (is_valid_local_dir) {
			strcat(nldpath, local_dir);
			free(local_dir);
		}
		else
			strncat(nldpath, ldpath, dirlen);
	skip_copying:
		ldpath += dirlen;
		if (*ldpath) {
			++len;
			nldpath = alloc_ldpath(len);
			if (nldpath == NULL)
				return (GFARM_ERR_NO_MEMORY);
			strncat(nldpath, ldpath, 1);
			++ldpath;
		}
	}
	rv = putenv(nldpath);
	if (rv == -1)
		return (gfarm_errno_to_error(errno));

	return (e_save);
}

static void
errmsg(char *func, char *msg)
{
	fprintf(stderr, "%s (%s): %s: %s\n",
		gfarm_host_get_self_name(), progname, func, msg);
}

int
main(int argc, char *argv[], char *envp[])
{
	char *e, *gfarm_url, *local_path, **new_env, *cwd_env, *pwd_env;
	char *path;
	int i, j, status, envc, rank = -1, nodes = -1;
	pid_t pid;
	static const char env_node_rank[] = "GFARM_NODE_RANK=";
	static const char env_node_size[] = "GFARM_NODE_SIZE=";
	static const char env_flags[] = "GFARM_FLAGS=";
	static const char env_gfs_pwd[] = "GFS_PWD=";
	static const char env_pwd[] = "PWD=";
	char rankbuf[sizeof(env_node_rank) + GFARM_INT64STRLEN];
	char nodesbuf[sizeof(env_node_size) + GFARM_INT64STRLEN];
	char flagsbuf[sizeof(env_flags) + 3];
	char cwdbuf[PATH_MAX * 2], pwdbuf[PATH_MAX * 2];

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		errmsg("gfarm_initialize", e);
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
					errmsg("-I", "missing argument");
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
					errmsg("-N", "missing argument");
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

	path = argv[0];
	if (is_mount_point(path))
		e = gfs_mntpath_to_url(path, strlen(path), &gfarm_url);
	else
		e = gfs_realpath(path, &gfarm_url);
	if (e == NULL) {
		e = gfarm_url_program_get_local_path(gfarm_url, &local_path);
		if (e != NULL) {
			errmsg(gfarm_url, e);
			exit(1);
		}
		free(gfarm_url);
	}
	else {
		struct stat sb;

		local_path = search_path(path);
		if (stat(local_path, &sb)) {
			errmsg(local_path, strerror(errno));
			exit(1);
		}
	}

	e = modify_ld_library_path();
	if (e != NULL) {
		errmsg("modify_ld_library_path", e);
		/* continue */
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
	GFARM_MALLOC_ARRAY(new_env, envc + 5 + 1);
	memcpy(cwdbuf, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH);
	e = gfs_getcwd(cwdbuf + GFARM_URL_PREFIX_LENGTH,
		sizeof(cwdbuf) - GFARM_URL_PREFIX_LENGTH);
	if (e != NULL) {
		errmsg("cannot get current directory", e);
		exit(1);
	}
	GFARM_MALLOC_ARRAY(cwd_env, strlen(cwdbuf) + sizeof(env_gfs_pwd));
	if (cwd_env == NULL) {
		fprintf(stderr, "%s: no memory for %s%s\n",
		    progname, env_gfs_pwd, cwdbuf);
		exit(1);
	}
	(void)chdir(cwdbuf); /* rely on syscall hook. it is ok if it fails */
	getcwd(pwdbuf, sizeof pwdbuf);
	GFARM_MALLOC_ARRAY(pwd_env, strlen(pwdbuf) + sizeof(env_pwd));
	if (pwd_env == NULL) {
		fprintf(stderr, "%s: no memory for %s%s\n",
		    progname, env_pwd, pwdbuf);
		exit(1);
	}
	envc = 0;
	for (i = 0; (e = envp[i]) != NULL; i++) {
		if (memcmp(e, env_node_rank, sizeof(env_node_rank) -1 ) != 0 &&
		    memcmp(e, env_node_size, sizeof(env_node_size) -1 ) != 0 &&
		    memcmp(e, env_flags, sizeof(env_flags) - 1 ) != 0 &&
		    memcmp(e, env_gfs_pwd, sizeof(env_gfs_pwd) - 1) != 0 &&
		    memcmp(e, env_pwd, sizeof(env_pwd) - 1 ) != 0)
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
	sprintf(pwd_env, "%s%s", env_pwd, pwdbuf);
	new_env[envc++] = pwd_env;
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
				errmsg("(child) gfarm_terminate", e);
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
		errmsg("gfarm_terminate", e);
		exit(1);
	}
	exit(status);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gfarm/gfarm.h>

static char progname[] = "gfexec";

static void
print_usage()
{
	fprintf(stderr, "usage: %s [-u|-h] program args ...\n", progname);
	exit(2);
}

int
fork_and_execvp(char *path, char *argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid == 0) {
		execvp(path, argv);
		perror(path);
		_exit(1);
	}
	else
		waitpid(pid, &status, 0);

	return (status);
}

int
main(int argc, char *argv[], char *envp[])
{
	char *e, *gfarm_url, **saved_argv;
	int i, saved_argc;
	int gfarm_program = 1;

	--argc, ++argv;
	if (argc < 1)
		print_usage();

	while (argv[0] && argv[0][0] == '-') {
		i = 1;
		while (argv[0][i]) {
			switch (argv[0][i]) {
			case 'u':
				gfarm_program = 0;
				break;
			case 'h':
				print_usage();
			default:
				fprintf(stderr, "%s: invalid option -- %c\n",
					progname, argv[0][i]);
				print_usage();
			}
			++i;
		}
		--argc, ++argv;
	}
	if (argc < 1)
		print_usage();

	/* save argv because it may be modified in gfarm_initialize() */
	saved_argc = argc;
	saved_argv = malloc(argc * sizeof(char *) + 1);
	if (saved_argv == NULL) {
		fprintf(stderr, "not enough memory\n");
		exit(1);
	}
	for (i = 0; i < argc; ++i) {
		saved_argv[i] = strdup(argv[i]);
		if (saved_argv[i] == NULL) {
			fprintf(stderr, "not enough memory\n");
			exit(1);
		}
	}
	saved_argv[i] = '\0';

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
		exit(1);
	}

	e = gfs_realpath(*saved_argv, &gfarm_url);
	if (e != NULL) {
		/* not found in Gfarm file system */
		if (gfarm_program) {
			execvp(*saved_argv, saved_argv);
			perror(*saved_argv);
			exit(1);
		}
		else {
			fork_and_execvp(*argv, argv);
			e = gfarm_terminate();
			if (e != NULL)
				fprintf(stderr, "%s: %s\n", *argv, e);
			exit(0);
		}
  	}

	if (gfarm_program)
		e = gfs_execve(gfarm_url, saved_argv, envp);
	else
		e = gfs_execve_legacy(gfarm_url, argv, envp);
	fprintf(stderr, "%s: %s\n", gfarm_url, e);
	free(gfarm_url);
	for (i = 0; i < saved_argc; ++i)
		free(saved_argv[i]);
	free(saved_argv);
	/*
	 * gfarm_terminate() may fail because it might be already
	 * called in gfs_execve().
	 */
	(void)gfarm_terminate();
	exit(1);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gfarm/gfarm.h>

static char progname[] = "gfexec";

int
main(int argc, char *argv[], char *envp[])
{
	char *e, *gfarm_url, **saved_argv;
	int i, saved_argc;

	--argc, ++argv;
	if (argc < 1) {
		printf("usage: %s program args ...\n", progname);
		exit(2);
	}
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
		execvp(*saved_argv, saved_argv);
		perror(*saved_argv);
		exit(1);
	}
	e = gfs_execve(gfarm_url, saved_argv, envp);
	fprintf(stderr, "%s: %s\n", gfarm_url, e);
	free(gfarm_url);
	for (i = 0; i < saved_argc; ++i)
		free(saved_argv[i]);
	free(saved_argv);
	/*
	 * Should not call gfarm_terminate() here, because it has been
	 * already called in gfs_execve().
	 */

	exit(1);
}

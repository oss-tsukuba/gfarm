/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>

static char progname[] = "gfexec";

int
main(int argc, char *argv[], char *envp[])
{
	char *e, *gfarm_url;

	--argc, ++argv;
	if (argc < 1) {
		printf("usage: %s program args ...\n", progname);
		exit(2);
	}

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
		exit(1);
	}

	e = gfs_realpath(*argv, &gfarm_url);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", *argv, e);
		exit(1);
	}
	e = gfs_execve(gfarm_url, argv, envp);
	fprintf(stderr, "%s: %s\n", gfarm_url, e);
	free(gfarm_url);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
		exit(1);
	}

	exit(1);
}

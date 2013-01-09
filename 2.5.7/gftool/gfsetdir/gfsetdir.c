/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/socket.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

char *program_name = "gfsetdir";

void
usage()
{
	fprintf(stderr, "Usage: %s [-s|-c] [directory]\n", program_name);
	fprintf(stderr, "\t-c\t output string for *csh\n");
        fprintf(stderr, "\t-s\t                   otherwise\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct gfs_stat gstat;
	char *e, *nwdir, *gfarm_path;
	extern int optind;
	int ch;
	enum { UNDECIDED,
	       B_SHELL_LIKE,
	       C_SHELL_LIKE
	} shell_type = UNDECIDED;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "sc")) != -1) {
		switch (ch) {
		case 's':
			shell_type = B_SHELL_LIKE;
			break;
		case 'c':
			shell_type = C_SHELL_LIKE;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * Get absolute path from the argument directory name.
	 * If no arugument is passed, generate gfarm:/"global username".
	 */
	nwdir = "gfarm:~"; /* home directory */
	switch (argc) {
	case 0:
		break;
	case 1:
		nwdir = argv[0];
		break;
	default:
		usage();
	}

	/* check whether it is a directory or not. */
	e = gfs_stat(nwdir, &gstat);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", nwdir, e);
		exit(1);
	}
	if (!GFARM_S_ISDIR(gstat.st_mode)) {
		fprintf(stderr, "%s: not a directory\n", nwdir);
		gfs_stat_free(&gstat);
		exit(1);
	}
	gfs_stat_free(&gstat);
		
	/* expand the path name */
	e = gfs_realpath(nwdir, &gfarm_path);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", nwdir, e);
		exit(1);
	}

	if (shell_type == UNDECIDED) {
		char *shell = getenv("SHELL");
		int shell_len = strlen(shell);
		
		if (shell_len < 3 || 
		    memcmp(shell + shell_len - 3, "csh", 3) != 0)
			shell_type = B_SHELL_LIKE;
		else
			shell_type = C_SHELL_LIKE;
	}
	if (shell_type == B_SHELL_LIKE)
		printf("GFS_PWD=%s; export GFS_PWD\n", gfarm_path);
	else
		printf("setenv GFS_PWD %s\n", gfarm_path);
	fflush(stdout);
	free(gfarm_path);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

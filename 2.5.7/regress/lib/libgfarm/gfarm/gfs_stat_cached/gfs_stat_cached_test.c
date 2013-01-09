#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>

char *program_name = "gfs_stat_cached_test";
const char *opt_local_filepath;
const char *opt_gfarm_filepath;

#define HELP_OPTS	"P?"
#define GETOPT_OPTS	"P?"

static void
usage(void)
{
	fprintf(stderr, "Usage: %s -" HELP_OPTS
	    " <local filepath> <gfarm filepath>\n",
	    program_name);
	exit(EXIT_FAILURE);
}

static int
test_purge(gfarm_error_t (*stat_cached)(const char *, struct gfs_stat *),
	const char *diag)
{
	gfarm_error_t e;
	struct gfs_stat st;
	int r, rs;
	char cmd[BUFSIZ];

	sprintf(cmd, "gfreg %s %s", opt_local_filepath, opt_gfarm_filepath);
	rs = system(cmd);
	if (rs == -1) {
		gflog_error(GFARM_MSG_UNUSED, "%s : system(\"%s\"): %s",
			    diag, cmd, strerror(errno));
		return (0);
	} else if ((r = WEXITSTATUS(rs)) != 0) {
		gflog_error(GFARM_MSG_UNUSED, "%s : gfreg returns %d",
		    diag, r);
		return (0);
	}
	if ((e = gfs_lstat_cached(opt_gfarm_filepath, &st))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNUSED, "%s : gfs_lstat_cached : %s",
		    diag, gfarm_error_string(e));
		return (0);
	}
	if ((e = gfs_unlink(opt_gfarm_filepath)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNUSED, "%s : gfs_unlink : %s",
		    diag, gfarm_error_string(e));
		return (0);
	}
	if ((e = gfs_stat_cache_purge(opt_gfarm_filepath)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNUSED, "%s : gfs_stat_cache_purge : %s",
		    diag, gfarm_error_string(e));
		return (0);
	}
	if ((e = gfs_stat_cached(opt_gfarm_filepath, &st))
	    != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		gflog_error(GFARM_MSG_UNUSED,
		    "%s : expected gfs_stat_cached returns \"%s\" "
		    "but return \"%s\"", diag,
		    gfarm_error_string(GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY),
		    gfarm_error_string(e));
		return (0);
	}
	if ((e = gfs_lstat_cached(opt_gfarm_filepath, &st))
	    != GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		gflog_error(GFARM_MSG_UNUSED,
		    "%s : expected gfs_stat_cached returns \"%s\" "
		    "but return \"%s\"", diag,
		    gfarm_error_string(GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY),
		    gfarm_error_string(e));
		return (0);
	}
	return (1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, r = 0, op = 0;

	gflog_set_message_verbose(99);
	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, GETOPT_OPTS)) != -1) {
		switch (c) {
		case 'P':
			op = c;
			break;
		case '?':
			usage(); /* exit */
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage(); /* exit */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc != 2)
		usage(); /* exit */
	opt_local_filepath = argv[0];
	opt_gfarm_filepath = argv[1];

	switch (op) {
	case 'P':
		r = test_purge(gfs_stat_cached, "gfs_stat_cached");
		if (r == 0)
			return (EXIT_FAILURE);
		r = test_purge(gfs_lstat_cached, "gfs_lstat_cached");
		break;
	}

	if (r == 0)
		return (EXIT_FAILURE);

	if ((e = gfarm_terminate()) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

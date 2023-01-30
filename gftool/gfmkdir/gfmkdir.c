/*
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_path.h"

char *program_name = "gfmkdir";

static void
usage(void)
{
	fprintf(stderr,
"Usage: %s"
#if 0
" [-p] [-m <mode>] [-M <mtime>] [-u <owner>] [-g <group>] directory...\n",
#else
" [-p] [-m <mode>] directory...\n",
#endif
		program_name);
	exit(1);
}

static gfarm_error_t
gfmkdir_plus(char *path, gfarm_mode_t mode,
    const struct gfarm_timespec *tsp,
    const char *username, const char *groupname)
{
	gfarm_error_t e, e2 = GFARM_ERR_NO_ERROR;

	e = gfs_mkdir(path, mode);
	if (e != GFARM_ERR_NO_ERROR) {
		return (e);
	}
	if (tsp != NULL) {
		e = gfs_lutimes(path, tsp);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n", program_name,
			   path, gfarm_error_string(e));
			e2 = e;
		}
	}
	if (username != NULL || groupname != NULL) {
		e = gfs_lchown(path, username, groupname);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s%s%s: %s\n", program_name,
			    path,
			    username != NULL ? username : "",
			    username != NULL && groupname != NULL ? ":" : "",
			    groupname != NULL ? groupname : "",
			    gfarm_error_string(e));
			e2 = e;
		}
	}
	if (e2 != GFARM_ERR_NO_ERROR) {
		e = e2;
	}
	return (e);
}

static gfarm_error_t
gfmkdir_parent_plus(char *path, gfarm_mode_t mode,
    const struct gfarm_timespec *tsp,
    const char *username, const char *groupname)
{
	gfarm_error_t e;
	struct gfs_stat sb;
	char *parent, *realpath = NULL;
	int is_dir = 0;

	e = gfarm_realpath_by_gfarm2fs(path, &realpath);
	if (e == GFARM_ERR_NO_ERROR)
		path = realpath;
	parent = gfarm_url_dir(path);
	if (parent == NULL) {
		free(realpath);
		return (GFARM_ERR_NO_MEMORY);
	}
	e = gfs_stat(parent, &sb);
	if (e == GFARM_ERR_NO_ERROR) {
		is_dir = GFARM_S_ISDIR(sb.st_mode);
		gfs_stat_free(&sb);
	} else if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY) {
		e = gfmkdir_parent_plus(parent, mode | 0700,
		    tsp, username, groupname);
		is_dir = 1;
	}
	free(parent);
	if (e != GFARM_ERR_NO_ERROR)
		;
	else if (!is_dir)
		e = GFARM_ERR_ALREADY_EXISTS;
	else if ((e = gfmkdir_plus(path, mode, tsp, username, groupname))
		  == GFARM_ERR_ALREADY_EXISTS &&
	    gfs_stat(path, &sb) == GFARM_ERR_NO_ERROR &&
	    GFARM_S_ISDIR(sb.st_mode))
		e = GFARM_ERR_NO_ERROR;
	free(realpath);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	int option_parent = 0;
	const char *option_mode = NULL;
	const char *option_mtime = NULL;
	const char *option_user = NULL;
	const char *option_group = NULL;
	gfarm_mode_t mode = 0755;
	char *path = NULL, *ep;
	struct gfarm_timespec ts[2], *tsp = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "g:hm:M:pu:?")) != -1) {
		switch (c) {
		case 'g':
			option_group = optarg;
			break;
		case 'm':
			option_mode = optarg;
			break;
		case 'M':
			option_mtime = optarg;
			break;
		case 'p':
			option_parent = 1;
			break;
		case 'u':
			option_user = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0)
		usage();

	if (option_mode) {
		mode = strtol(option_mode, &ep, 8);
		if (errno != 0 || ep == option_mode || *ep != '\0') {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[0],
			    errno != 0 ? strerror(errno)
			    : "<mode> must be an octal number");
			exit(EXIT_FAILURE);
		}
	}
	if (option_mtime) {
		long mtime;

		mtime = strtol(option_mtime, &ep, 0);;
		if (errno != 0 || ep == option_mtime || *ep != '\0') {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[0],
			    errno != 0 ? strerror(errno)
			    : "<mtime> must be an integer number"
			    " of seconds since UNIX epoch");
			exit(EXIT_FAILURE);
		}
		ts[0].tv_sec = mtime;  /* atime */
		ts[1].tv_sec = mtime;
		ts[0].tv_nsec = 0;
		ts[1].tv_nsec = 0;
		tsp = ts;
	}

	for (i = 0; i < argc; i++) {
		if (option_parent) {
			e = gfmkdir_parent_plus(argv[i], mode, tsp,
			    option_user, option_group);
		} else {
			e = gfarm_realpath_by_gfarm2fs(argv[i], &path);
			if (e == GFARM_ERR_NO_ERROR)
				argv[i] = path;
			e = gfmkdir_plus(argv[i], mode, tsp,
			    option_user, option_group);
		}
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
		free(path);
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

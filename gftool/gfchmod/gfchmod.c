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
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfchmod";

static int opt_follow_symlink = 1;
static int opt_recursive = 0;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <mode> <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h\t"
	    "affect symbolic links instead of referenced files\n");
	fprintf(stderr, "\t-R\t"
	    "change mode recursively\n");
#if 0
	fprintf(stderr, "\t-M SECONDS\t"
	    "change modification time (since UNIX epoch)\n");
	fprintf(stderr, "\t-u USERNAME\t"
	    "change owner\n");
	fprintf(stderr, "\t-g GROUPNAME\t"
	    "change group\n");
#endif
	exit(2);
}

struct gfchmod_arg {
	gfarm_mode_t mode;
	struct gfarm_timespec *tsp;
	const char *user;
	const char *group;
};

static gfarm_error_t
gfchmod_plus(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e, e2 = GFARM_ERR_NO_ERROR;
	struct gfchmod_arg *a = arg;
	gfarm_mode_t mode = a->mode;
	struct gfarm_timespec *tsp = a->tsp;
	const char *username = a->user;
	const char *groupname = a->group;

	e = (opt_follow_symlink ? gfs_chmod : gfs_lchmod)(path, mode);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s: %s\n", program_name, path,
			gfarm_error_string(e));
		return (e);
	}
	if (tsp != NULL) {
		e = (opt_follow_symlink ? gfs_utimes : gfs_lutimes)(path, tsp);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n", program_name,
			   path, gfarm_error_string(e));
			e2 = e;
		}
	}
	if (username != NULL || groupname != NULL) {
		e = (opt_follow_symlink ?
		     gfs_chown : gfs_lchown)(path, username, groupname);
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

int
main(int argc, char **argv)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int c, i, n;
	char *si = NULL, *s, *ep;
	struct gfchmod_arg arg;
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct gfs_stat st;
	const char *option_mtime = NULL;
	const char *option_user = NULL;
	const char *option_group = NULL;
	struct gfarm_timespec ts[2], *tsp = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "g:hM:Ru:?")) != -1) {
		switch (c) {
		case 'g':
			option_group = optarg;
			break;
		case 'h':
			opt_follow_symlink = 0;
			break;
		case 'M':
			option_mtime = optarg;
			break;
		case 'R':
			opt_recursive = 1;
			break;
		case 'u':
			option_user = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 1)
		usage();

	if (option_mtime) {
		long mtime;

		errno = 0;
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

	errno = 0;
	arg.tsp = tsp;
	arg.user = option_user;
	arg.group = option_group;
	arg.mode = strtol(argv[0], &ep, 8);
	if (errno != 0 || ep == argv[0] || *ep != '\0') {
		fprintf(stderr, "%s: %s: %s\n", program_name, argv[0],
		    errno != 0 ? strerror(errno)
		    : "<mode> must be an octal number");
		exit(EXIT_FAILURE);
	}
	if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 1; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		s = gfarm_stringlist_elem(&paths, i);
		e = gfarm_realpath_by_gfarm2fs(s, &si);
		if (e == GFARM_ERR_NO_ERROR)
			s = si;
		if ((e = gfs_lstat(s, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", s, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISDIR(st.st_mode) && opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
				    gfchmod_plus, gfchmod_plus,
				    NULL, s, &arg);
			else
				e = gfchmod_plus(s, &st, &arg);
			gfs_stat_free(&st);
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(si);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}

/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gfarm/gfarm.h>

/*
 *
 */

static char *
gfarm_path_info_set_from_file(char *pathname, int nfrags)
{
	struct stat sb;
	struct passwd *pw;
	struct gfarm_path_info pi;
	char *e, *p, *username;

	if (stat(pathname, &sb))
		return "no such file";

	/* remove a section part. */

	p = pathname;
	while (*p) {
		if (*p == ':') {
			*p = '\0';
			break;
		}
		++p;
	}

	pw = getpwuid(sb.st_uid);
	if (pw == NULL)
		return "no such user";

	if (gfarm_path_info_get(pathname, &pi) == NULL) {
		gfarm_path_info_free(&pi);
		return (GFARM_ERR_ALREADY_EXISTS);
	}

	e = gfarm_local_to_global_username(pw->pw_name, &username);
	if (e != NULL)
		return (e);

	pi.pathname = pathname;
	pi.status.st_mode = GFARM_S_IFREG | (sb.st_mode & GFARM_S_ALLPERM);
	pi.status.st_user = username;
	pi.status.st_group = "*"; /* XXX for now */
	pi.status.st_atimespec.tv_sec = sb.st_atime;
	pi.status.st_mtimespec.tv_sec = sb.st_mtime;
	pi.status.st_ctimespec.tv_sec = sb.st_ctime;
	pi.status.st_atimespec.tv_nsec =
	pi.status.st_mtimespec.tv_nsec =
	pi.status.st_ctimespec.tv_nsec = 0;
	pi.status.st_size = 0;
	pi.status.st_nsections = nfrags;

	e = gfarm_path_info_set(pi.pathname, &pi);
	free(username);

	return (e);
}

static char *
gfarm_path_info_remove_all(char *pathname)
{
	char *e, *e_save = NULL;
#if 0
	struct gfarm_path_info pi;

	/*
	 * It is not necessary to check whether pathname is a
	 * directory or not.  This function would like to force to
	 * remove the path information from file system metadata.
	 */
	if (gfarm_path_info_get(pathname, &pi) == NULL) {
		if (GFARM_S_ISDIR(pi.status.st_mode)) {
			gfarm_path_info_free(&pi);
			return (GFARM_ERR_IS_A_DIRECTORY);
		}
		gfarm_path_info_free(&pi);
	}
#endif
	e = gfarm_file_section_copy_info_remove_all_by_file(pathname);
	if (e != NULL)
		e_save = e;
	e = gfarm_file_section_info_remove_all_by_file(pathname);
	if (e != NULL)
		e_save = e;
	e = gfarm_path_info_remove(pathname);
	if (e != NULL)
		e_save = e;
	return (e_save);	
}

/*
 *
 */

static char *progname = "addpath";

void
usage()
{
	fprintf(stderr, "usage: %s pathname:sec nfrags\n",
		progname);
	fprintf(stderr, "       %s -d pathname\n",
		progname);
}

int
main(int argc, char *argv[])
{
	char *pathname;
	int nfrags;
	char *e;
	extern int optind;
	int c;
	enum { add, delete } mode = add;
	
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", progname, e);
	}

	while ((c = getopt(argc, argv, "d")) != EOF) {
		switch (c) {
		case 'd':
			mode = delete;
			break;
		default:
			usage();
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		pathname = argv[0];
	else {
		fprintf(stderr, "%s: too few arguments\n", progname);
		usage();
		exit(1);
	}
	--argc;
	++argv;

	switch (mode) {
	case add:
		if (argc == 1)
			nfrags = atoi(argv[0]);
		else {
			usage();
			exit(1);
		}
		e = gfarm_path_info_set_from_file(pathname, nfrags);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", pathname, e);
			exit(1);
		}
		break;
	case delete:
		e = gfarm_path_info_remove_all(pathname);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", pathname, e);
			exit(1);
		}
		break;
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", pathname, e);
	}

	exit(0);
}

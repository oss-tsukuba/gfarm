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

int
main(int argc, char *argv[])
{
	struct stat sb;
	char *pathname, *c;
	struct passwd *pw;
	struct gfarm_path_info pi;
	int nfrags;
	char *e;
	
	if (argc != 3) {
		fprintf(stderr, "usage: %s pathname nfrags\n",
			argv[0]);
		exit(1);
	}

	pathname = argv[1];
	nfrags = atoi(argv[2]);

	if (stat(pathname, &sb)) {
		perror(pathname);
		exit(1);
	}

	/* remove a section part. */

	c = pathname;
	while (*c) {
		if (*c == ':') {
			*c = '\0';
			break;
		}
		++c;
	}

	pw = getpwuid(sb.st_uid);
	if (pw == NULL) {
		fprintf(stderr, "no such user: uid = %d\n",
			sb.st_uid);
		exit(1);
	}

	pi.pathname = pathname;
	pi.status.st_mode = GFARM_S_IFREG | (sb.st_mode & GFARM_S_ALLPERM);
	pi.status.st_user = strdup(pw->pw_name); /* XXX NULL check */
	pi.status.st_group = strdup("*"); /* XXX for now */
	pi.status.st_atimespec.tv_sec = sb.st_atime;
	pi.status.st_mtimespec.tv_sec = sb.st_mtime;
	pi.status.st_ctimespec.tv_sec = sb.st_ctime;
	pi.status.st_atimespec.tv_nsec =
	pi.status.st_mtimespec.tv_nsec =
	pi.status.st_ctimespec.tv_nsec = 0;
	pi.status.st_size = 0;
	pi.status.st_nsections = nfrags;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", pathname, e);
	}

	e = gfarm_path_info_set(pi.pathname, &pi);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", pathname, e);
		exit(1);
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", pathname, e);
	}

	exit(0);
}

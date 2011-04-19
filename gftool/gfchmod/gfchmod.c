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

char *program_name = "gfchmod";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-h] <mode> <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h\t"
	    "affect symbolic links instead of referenced files\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, i, n, follow_symlink = 1, status = 0;
	char *s;
	long mode;
	gfarm_stringlist paths;
	gfs_glob_t types;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
		case 'h':
			follow_symlink = 0;
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

	errno = 0;
	mode = strtol(argv[0], &s, 8);
	if (errno != 0 || s == argv[0] || *s != '\0') {
		fprintf(stderr, "%s: %s: %s\n", program_name, argv[0],
		    errno != 0 ? strerror(errno)
		    : "<mode> must be an octal number");
		status = 1;
	} else if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	} else if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	} else {
		for (i = 1; i < argc; i++)
			gfs_glob(argv[i], &paths, &types);

		n = gfarm_stringlist_length(&paths);
		for (i = 0; i < n; i++) {
			s = gfarm_stringlist_elem(&paths, i);
			e = (follow_symlink ? gfs_chmod : gfs_lchmod)(s,
			    (gfarm_mode_t)mode);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, s, gfarm_error_string(e));
				status = 1;
			}
		}
		gfs_glob_free(&types);
		gfarm_stringlist_free_deeply(&paths);
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

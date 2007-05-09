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
	fprintf(stderr, "Usage: %s <mode> <path>...\n", program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, i, n, status = 0;
	char *s;
	long mode;
#if 0 /* not yet in gfarm v2 */
	gfarm_stringlist paths;
	gfs_glob_t types;
#endif
	extern int optind;

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
#if 0 /* not yet in gfarm v2 */
	} else if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		status = 1;
	} else if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name, e);
		status = 1;
#endif
	} else {
#if 0 /* not yet in gfarm v2 */
		for (i = 1; i < argc; i++)
			gfs_glob(argv[i], &paths, &types);

		n = gfarm_stringlist_length(&paths);
		for (i = 0; i < n; i++) {
			s = gfarm_stringlist_elem(&paths, i);
#else
		n = argc;
		for (i = 1; i < n; i++) {
			s = argv[i];
#endif
			e = gfs_chmod(s, (gfarm_mode_t)mode);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, s, gfarm_error_string(e));
				status = 1;
			}
		}
#if 0 /* not yet in gfarm v2 */
		gfs_glob_free(&types);
		gfarm_stringlist_free_deeply(&paths);
#endif
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

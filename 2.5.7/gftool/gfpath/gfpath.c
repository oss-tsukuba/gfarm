#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

const char *program_name = "gfpath";

void
usage()
{
	fprintf(stderr, "Usage: %s -b pathname\n", program_name);
	fprintf(stderr, "       %s -d pathname\n", program_name);
	exit(2);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int ch, mode = 0;
	const char *s;
	char *path, *allocated = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}
	while ((ch = getopt(argc, argv, "bdDE")) != -1) {
		switch (ch) {
		case 'b':
		case 'd':
		case 'D':
		case 'E':
			mode = ch;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();
	path = argv[0];

	switch (mode) {
	case 'b':
		s = gfarm_url_dir_skip(path);
		break;
	case 'd':
		s = allocated = gfarm_url_dir(path);
		break;
	case 'B':
		s = gfarm_path_dir_skip(path);
		break;
	case 'D':
		s = allocated = gfarm_path_dir(path);
		break;
	default:
		usage();
		/*NOTREACHED*/
#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
		s = NULL;
#endif
	}
	if (s == NULL) {
		fprintf(stderr, "%s: no memory for \"%s\"\n",
		    program_name, path);
		exit(1);
	}
	printf("%s\n", s);
	free(allocated);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}
	return (0);
}

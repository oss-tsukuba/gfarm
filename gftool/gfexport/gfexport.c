#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "gfs_profile.h"
#include "host.h"
#include "config.h"
#include "gfarm_path.h"
#include "gfs_pio.h"

char *program_name = "gfexport";

gfarm_error_t
gfexport(char *gfarm_url, char *host, FILE *ofp,
	gfarm_off_t off, gfarm_off_t size)
{
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(gfarm_url, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	e = gfs_pio_internal_set_view_section(gf, host);
	if (e == GFARM_ERR_NO_ERROR)
		e = gfs_pio_recvfile(gf, off, STDOUT_FILENO, 0, size, NULL);
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-h <hostname>\n");
#if 0
	fprintf(stderr, "\t-o <offset>\tskip bytes at start of input\n");
	fprintf(stderr, "\t-s <size>\tinput size\n");
#endif
	fprintf(stderr, "\t%s\t%s\n", "-p", "turn on profiling");
	exit(1);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *url, *realpath, *hostname = NULL;
	int ch, i;
	gfarm_off_t off = 0, size = -1;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize(): %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((ch = getopt(argc, argv, "h:o:ps:?")) != -1) {
		switch (ch) {
		case 'h':
			hostname = optarg;
			break;
		case 'p':
			gfs_profile_set();
			break;
		case 'o':
			off = atoll(optarg);
			break;
		case 's':
			size = atoll(optarg);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		fprintf(stderr, "%s: %s\n", program_name,
		    "error: missing input file name");
		usage();
	}

	for (i = 0; i < argc; i++) {
		e = gfarm_realpath_by_gfarm2fs(argv[i], &realpath);
		if (e == GFARM_ERR_NO_ERROR) {
			url = realpath;
		} else {
			url = argv[i];
			realpath = NULL;
		}

		e = gfexport(url, hostname, stdout, off, size);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n", program_name, argv[i],
			    gfarm_error_string(e));
			exit(1);
		}
		free(realpath);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate(): %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	return (0);
}

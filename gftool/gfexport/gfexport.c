#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "queue.h" /* for gfs_file */

#include "gfs_profile.h"
#include "host.h"
#include "config.h"
#include "gfarm_path.h"

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include <openssl/evp.h>
#include "gfs_pio.h"

char *program_name = "gfexport";

/* from GFS_FILE_BUFSIZE in lib/libgfarm/gfarm/gfs_pio.h */
#define BUFFER_SIZE (1048576 - 8)
static char buffer[BUFFER_SIZE];

gfarm_error_t
gfprint(GFS_File gf, FILE *ofp, gfarm_off_t size)
{
	gfarm_error_t e;
	int n;

	if (size > 0) {
		for (;;) {
			int req = sizeof buffer > size ? size : sizeof buffer;

			if ((e = gfs_pio_read(gf, buffer, req, &n)) !=
			    GFARM_ERR_NO_ERROR)
				break;
			if (n == 0) /* EOF */
				break;
			if (fwrite(buffer, 1, n, ofp) != n) {
				e = GFARM_ERR_INPUT_OUTPUT;
				break;
			}
			size -= n;
			if (size <= 0)
				break;
		}
		return (e);
	}

	while ((e = gfs_pio_read(gf, buffer, sizeof buffer, &n)) ==
	       GFARM_ERR_NO_ERROR) {
		if (n == 0) /* EOF */
			break;
		if (fwrite(buffer, 1, n, ofp) != n) {
			e = GFARM_ERR_INPUT_OUTPUT;
			break;
		}
	}
	return (e);
}

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
	if (e != GFARM_ERR_NO_ERROR)
		goto close;
	if (off > 0) {
		e = gfs_pio_seek(gf, off, GFARM_SEEK_SET, NULL);
		if (e != GFARM_ERR_NO_ERROR)
			goto close;
	}

	e = gfprint(gf, ofp, size);
 close:
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
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
	char *url, *realpath = NULL, *hostname = NULL;
	int ch;
	gfarm_off_t off = -1, size = -1;

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
	if (argc != 1) {
		fprintf(stderr, "%s: %s\n", program_name,
		    "error: only one input file name expected");
		usage();
	}
	e = gfarm_realpath_by_gfarm2fs(argv[0], &realpath);
	if (e == GFARM_ERR_NO_ERROR)
		url = realpath;
	else
		url = argv[0];

	e = gfexport(url, hostname, stdout, off, size);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s: %s\n", program_name, url,
		    gfarm_error_string(e));
		exit(1);
	}
	free(realpath);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate(): %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	return (0);
}

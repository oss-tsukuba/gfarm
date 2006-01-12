/*
 * $Id$
 *
 * Copy a file
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>

char *program_name = "gfcp";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file> <output_file>\n",
		program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-p\t\t\tpreserve file status\n");
	fprintf(stderr, "\t-I fragment-index\tspecify a fragment index\n");
	exit(1);
}

#define BUFSIZE	65536

int
main(int argc, char *argv[])
{
	int flag_preserve = 0;
	char *e, c, *input, *output, *gfarm_index = NULL;
	GFS_File igf, ogf;
	struct gfs_stat gstat;
	gfarm_mode_t mode;
	struct gfarm_timespec gtspec[2];
	extern char *optarg;
	extern int optind;

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (argc >= 1)
		program_name = argv[0];

	while ((c = getopt(argc, argv, "hI:p?")) != -1) {
		switch (c) {
		case 'p':
			flag_preserve = 1;
			break;
		case 'I':
			gfarm_index = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 2 || argc > 3 /* XXX */)
		usage();
	output = argv[argc - 1];
	--argc;

	e = gfs_stat(output, &gstat);
	if (e == NULL) {
		if (GFARM_S_ISDIR(gstat.st_mode)) {
			fprintf(stderr, "%s: is a directory, "
				"not supported yet\n", output);
			exit(1);
		}
		/*
		 * XXX - gfs_stat() may return a non-null pointer because
		 * a process in the same parallel process might create
		 * it but a different file fragment already.
		 */
		/* fprintf(stderr, "%s: already exist\n", output); */
		gfs_stat_free(&gstat);
	}

	e = gfs_realpath(*argv, &input);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", *argv, e);
		exit(1);
	}
	e = gfs_stat(input, &gstat);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", input, e);
		exit(1);
	}
	if (flag_preserve) {
		mode = gstat.st_mode;
		gtspec[0] = gstat.st_atimespec;
		gtspec[1] = gstat.st_mtimespec;
	}
	else
		mode = 0666;
	gfs_stat_free(&gstat);

	/** **/

	e = gfs_pio_open(input, GFARM_FILE_RDONLY, &igf);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", input, e);
		exit(1);
	}
	if (gfarm_index == NULL) {
		e = gfs_pio_set_view_local(igf, GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			fprintf(stderr, "%s: set_view_local(%s): %s\n",
				program_name, input, e);
			exit(1);
		}
	}
	else {
		e = gfs_pio_set_view_section(
			igf, gfarm_index, NULL, GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			fprintf(stderr, "%s: set_view_section(%s, %s): %s\n",
				program_name, input, gfarm_index, e);
			exit(1);
		}
	}

	/** **/

	e = gfs_pio_create(output, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, mode,
	    &ogf);
	if (e != NULL) {
		fprintf(stderr, "%s: cannot open %s: %s\n",
			program_name, output, e);
		exit(1);
	}
	if (gfarm_index == NULL) {
		e = gfs_pio_set_view_local(ogf, GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			fprintf(stderr, "%s: set_view_local(%s): %s\n",
				program_name, output, e);
			exit(1);
		}
	}
	else {
		e = gfs_pio_set_view_section(
			ogf, gfarm_index, NULL, GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			fprintf(stderr, "%s: set_view_section(%s, %s): %s\n",
				program_name, output, gfarm_index, e);
			exit(1);
		}
	}

	/* copy this fragment */
	for (;;) {
		int rv, wv;
		char buffer[BUFSIZE];
		e = gfs_pio_read(igf, buffer, sizeof(buffer), &rv);
		if (e != NULL || rv == 0)
			break;
		e = gfs_pio_write(ogf, buffer, rv, &wv);
		if (e != NULL)
			break;
	}
	if (e != NULL)
		fprintf(stderr, "%s\n", e);

	e = gfs_pio_close(igf);
	if (e != NULL)
		fprintf(stderr, "%s: close failed: %s\n", input, e);
	e = gfs_pio_close(ogf);
	if (e != NULL)
		fprintf(stderr, "%s: close failed: %s\n", output, e);

	if (flag_preserve) {
		e = gfs_utimes(output, gtspec);
		if (e != NULL)
			fprintf(stderr, "%s: utimes failed: %s\n", output, e);
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

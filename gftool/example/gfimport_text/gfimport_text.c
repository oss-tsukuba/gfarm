#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

char *program_name = "gfimport_text";

char *
import_text(FILE *ifp, char *output,
	int nfrags, char **hosttab, file_offset_t *sizetab)
{
	int i, c;
	char *e;
	struct gfs_file *of;
	file_offset_t size;

	e = gfs_pio_create(output, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC, 0666,
	    &of);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", output, e);
		return (e);
	}

	for (i = 0; i < nfrags; i++) {
		e = gfs_pio_set_view_index(of, nfrags, i, hosttab[i],
		    GFARM_FILE_SEQUENTIAL);
		if (e != NULL)
			goto error_on_fragment;
		size = 0;
		for (;;) {
			if (size >= sizetab[i]) /* wrote enough */
				break;

			for (;;) {
				c = getc(ifp);
				if (c == EOF)
					break;
				e = gfs_pio_putc(of, c);
				if (e != NULL)
					goto error_on_fragment;
				size++;
				if (c == '\n')
					break;
			}
			if (c == EOF)
				break;
		}
	}
	e = gfs_pio_close(of);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", output, e);
		goto error_on_close;
	}
	return (NULL);

error_on_fragment:
	fprintf(stderr, "%s, fragment %d: %s\n", output, i, e);
	gfs_pio_close(of);
error_on_close:
	gfarm_url_fragment_cleanup(output, nfrags, hosttab);
	gfs_unlink(output);
	return (e);
}

void
check_file_size(FILE *ifp, char *iname, off_t *size)
{
	struct stat is;

	if (fstat(fileno(ifp), &is) == -1) {
		perror(iname);
		exit(1);
	}
	if (!S_ISREG(is.st_mode)) {
		fprintf(stderr, "%s: size unknown\n", iname);
		exit(1);
	}
	*size = is.st_size;
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\n");
	fprintf(stderr, "\t-N <number of hosts>\n");
	fprintf(stderr, "\t-f <configfile>\n");
	fprintf(stderr, "\t-o <output_gfarm_file>\n");
	exit(1);
}

static void
conflict_check(int *mode_ch_p, int ch)
{
	if (*mode_ch_p) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
			program_name, ch, *mode_ch_p);
		usage();
	}
	*mode_ch_p = ch;
}

int
main(int argc, char *argv[])
{
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *config = NULL, *hostfile = NULL, *output = NULL, *iname;
	int ch, nhosts = -1, error_line, mode_ch = 0;
	FILE *ifp;
	char **hosttab;
	file_offset_t *sizetab;
	off_t filesize;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:N:f:o:")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case 'N':
			nhosts = atoi(optarg);
			conflict_check(&mode_ch, ch);
			break;
		case 'f':
			config = optarg;
			conflict_check(&mode_ch, ch);
			break;
		case 'o':
			output = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (output == NULL) {
		fprintf(stderr, "%s: -o <output gfarm file> expected\n",
			program_name);
		exit(1);
	}
	if (argc == 0) {
		iname = "stdin";
		ifp = stdin;
	} else if (argc != 1) {
		fprintf(stderr,
			"%s: error: multiple input file name specified\n",
			program_name);
		exit(1);
	} else {
		iname = argv[0];
		ifp = fopen(iname, "r");
		if (ifp == NULL) {
			perror(iname);
			exit(1);
		}
	}

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (config != NULL) {
		e = gfarm_import_fragment_config_read(
		    config, &nhosts, &hosttab, &sizetab, &error_line);
	} else {
		check_file_size(ifp, iname, &filesize);
		if (hostfile != NULL) {
			e = gfarm_hostlist_read(hostfile,
				&nhosts, &hosttab, &error_line);
		} else if (nhosts != -1) {
			GFARM_MALLOC_ARRAY(hosttab, nhosts);
			if (hosttab == NULL) {
				fprintf(stderr,
					"%s: no memory\n", program_name);
				exit(1);
			}
			e = gfarm_schedule_search_idle_by_all(nhosts, hosttab);
		} else
			usage();
		if (e == NULL) {
			sizetab = gfarm_import_fragment_size_alloc(
			    filesize, nhosts);
			if (sizetab == NULL) {
				fprintf(stderr,
					"%s: not enough memory for %d hosts\n",
					program_name, nhosts);
				exit(1);
			}
		}
	}
	if (e != NULL) {
		if (config != NULL)
			fprintf(stderr, "%s: line %d: %s\n",
				config, error_line, e);
		else
			fprintf(stderr, "%s: %s\n",
				program_name, e);
		return (1);
	}
	import_text(ifp, output, nhosts, hosttab, sizetab);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

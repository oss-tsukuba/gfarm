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

void
import_text(FILE *ifp, char *output,
	int nfrags, char **hosttab, file_offset_t *sizetab)
{
	int i, c;
	char *e;
	struct gfs_file *of;
	file_offset_t size;

	e = gfs_pio_create(output, O_WRONLY, 0666, &of);
	if (e != NULL) {
		fprintf(stderr, "%s, %s\n", output, e);
		return;
	}

	for (i = 0; i < nfrags; i++) {
		c = getc(ifp);
		if (c == EOF)
			break;
		ungetc(c, ifp);

		e = gfs_pio_set_view_index(of, nfrags, i, hosttab[i],
					   GFARM_FILE_SEQUENTIAL);
		if (e != NULL) {
			fprintf(stderr, "%s, fragment %d: %s\n", output, i, e);
			e = gfarm_url_fragment_cleanup(output,
			    nfrags, hosttab);
			gfs_pio_close(of);
			return;
		}
		size = 0;
		for (;;) {
			if (size >= sizetab[i]) /* wrote enough */
				break;

			for (;;) {
				c = getc(ifp);
				if (c == EOF)
					break;
				gfs_pio_putc(of, c);
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
		fprintf(stderr, "%s, %s\n", output, e);
		e = gfarm_url_fragment_cleanup(output,
		    nfrags, hosttab);
		return;
	}
}

void
usage()
{
	fprintf(stderr, "Usage: %s [option] <input_file>\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-H <hostfile>\n");
	fprintf(stderr, "\t-f <configfile>\n");
	fprintf(stderr, "\t-o <output_gfarm_file>\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *config = NULL, *hostfile = NULL, *output = NULL, *iname;
	int ch, nhosts, error_line;
	FILE *ifp;
	char **hosttab;
	file_offset_t *sizetab;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "H:f:o:")) != -1) {
		switch (ch) {
		case 'H':
			hostfile = optarg;
			break;
		case 'f':
			config = optarg;
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

	if (hostfile != NULL && config != NULL) {
		fprintf(stderr,
			"%s: ambiguous. both -H %s and -f %s specified\n",
			program_name, hostfile, config);
		exit(1);
	} else if (config != NULL) {
		e = gfarm_import_fragment_config_read(
		    config, &nhosts, &hosttab, &sizetab, &error_line);
	} else if (hostfile != NULL) {
		struct stat is;

		if (fstat(fileno(ifp), &is) == -1) {
			perror(iname);
			exit(1);
		}
		if (!S_ISREG(is.st_mode)) {
			fprintf(stderr, "%s: size unknown\n", iname);
			exit(1);
		}
		e = gfarm_hostlist_read(hostfile,
		    &nhosts, &hosttab, &error_line);
		if (e == NULL) {
			sizetab = gfarm_import_fragment_size_alloc(
			    is.st_size, nhosts);
			if (sizetab == NULL) {
				fprintf(stderr,
					"%s: not enough memory for %d hosts\n",
					program_name, nhosts);
				exit(1);
			}
		}
	} else /* if (hostfile == NULL && config == NULL) */ {
		fprintf(stderr,
			"%s: either -H <hostfile> or -f <config> expected\n",
			program_name);
		exit(1);
	}
	if (e != NULL) {
		if (error_line != -1)
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include "gfarm.h"

char *program_name = "gfimport_text";

void
import_text(FILE *ifp, char *output,
	int nfrags, struct gfarm_import_fragment_table *fragtab)
{
	int i, c, rv;
	char *e;
	struct gfs_file *of;
	file_offset_t size;

	for (i = 0; i < nfrags; i++) {
		c = getc(ifp);
		if (c == EOF)
			break;
		ungetc(c, ifp);

		e = gfs_pio_create(output, i, fragtab[i].hostname, &of);
		if (e != NULL) {
			fprintf(stderr, "%s, fragment %d: %s\n", output, i, e);
			e = gfarm_url_fragment_cleanup(output, nfrags);
			return;
		}
		size = 0;
		for (;;) {
			if (size >= fragtab[i].size) /* wrote enough */
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
		e = gfs_pio_close(of);
		if (e != NULL) {
			fprintf(stderr, "%s, fragment %d: %s\n", output, i, e);
			e = gfarm_url_fragment_cleanup(output, nfrags);
			return;
		}
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
	char *e, *config = NULL, *hostfile = NULL, *output = NULL, *iname;
	int ch, nhosts, error_line;
	FILE *ifp;
	struct gfarm_import_fragment_table *fragtab;

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

	e = gfarm_initialize();
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
		e = gfarm_import_fragment_table_config(
		    config, &nhosts, &fragtab, &error_line);
	} else if (hostfile != NULL) {
		int i;
		struct stat is;

		if (fstat(fileno(ifp), &is) == -1) {
			perror(iname);
			exit(1);
		}
		if (!S_ISREG(is.st_mode)) {
			fprintf(stderr, "%s: size unknown\n", iname);
			exit(1);
		}
		e = gfarm_import_fragment_table_config_host(
		    hostfile, is.st_size, &nhosts, &fragtab, &error_line);
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
	import_text(ifp, output, nhosts, fragtab);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

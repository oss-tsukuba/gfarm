/*
 * $Id$
 */

#include <stdio.h>
#include <stdarg.h> /* gfurl.h: va_list */
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfurl.h"
#include "gfmsg.h"

#include "gfpconcat.h"

static char *program_name = "gfpconcat";

static char *optstring = "cfh:i:j:m:o:pqvdt?";

static void
gfpconcat_usage(int exit_on_error, struct gfpconcat_option *opt)
{
	fprintf(stderr,
"Usage: %s [-?] [-q (quiet)] [-v (verbose)] [-d (debug)]\n"
"\t[-c (compare after copy)]\n"
"\t[-f (overwrite existing file)]\n"
"\t[-h <destination hostname>]\n"
"\t[-j <#parallel(to copy parts)(connections)(default: %d)>]\n"
"\t[-m <minimum data size per a child process for parallel copying\n"
"\t     (default: %lld)>\n"
"\t[-p (report performance)]\n"
"\t[-i <list file of input URLs> (instead of input-file arguments)]\n"
"\t-o <output file(gfarm:... or file:...) (required)>\n"
"\tinput-file(gfarm:... or file:...)...\n"
		, opt->program_name, opt->n_para, (long long)opt->minimum_size);
	if (exit_on_error) {
		exit(EXIT_FAILURE);
	}
}

static void
gfpconcat_getopt(int argc, char **argv, struct gfpconcat_option *opt)
{
	gfarm_error_t e;
	int ch;

	while ((ch = getopt(argc, argv, optstring)) != -1) {
		switch (ch) {
		case 'c':
			opt->compare = 1;
			break;
		case 'f':
			opt->force = 1;
			break;
		case 'h':
			opt->dst_host = optarg;
			break;
		case 'i':
			opt->input_list = optarg;
			break;
		case 'j':
			opt->n_para = strtol(optarg, NULL, 0);
			break;
		case 'm':
			opt->minimum_size = strtoll(optarg, NULL, 0);
		case 'o':
			opt->out_file = optarg;
			break;
		case 'p':
			opt->performance = 1;
			break;
		case 'q':
			opt->quiet = 1;
			break;
		case 'v':
			opt->verbose = 1;
			break;
		case 'd':
			opt->debug = 1;
			break;
		case 't':	/* hidden */
			opt->test = 1;
			opt->verbose = 1;
			opt->performance = 1;
			break;
		case '?':
		default:
			opt->usage_func(0, opt);
			exit(EXIT_SUCCESS);
		}
	}
	argc -= optind;
	argv += optind;
	opt->n_part = argc;
	opt->parts = argv;

	if (opt->input_list != NULL) {
		int error_line = -1, n_part;
		char **parts;

		if (opt->n_part > 0) {
			gfmsg_error("When -i is specified, "
				    "input-file arguments are not required");
			opt->usage_func(1, opt);
			/* NOTREACHED */
		}
		e = gfarm_filelist_read(opt->input_list, &n_part, &parts,
		    &error_line);
		if (e != GFARM_ERR_NO_ERROR) {
			if (error_line != -1) {
				gfmsg_error_e(e, "%s: line %d",
				    opt->input_list, error_line);
			} else {
				gfmsg_error_e(e, "%s", opt->input_list);
			}
			exit(EXIT_FAILURE);
		}
		opt->n_part = n_part;
		opt->parts = parts;
	}
}

int
main(int argc, char *argv[])
{
	struct gfpconcat_option opt;

	gfpconcat_init(argc, argv, program_name,
	   gfpconcat_usage, gfpconcat_getopt, &opt);
	return (gfpconcat_main(&opt));
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfurl.h"
#include "gfmsg.h"

#include "gfpconcat.h"

static char *program_name = "gfcp";

static char *optstring = "cfh:j:m:pqvdt?";

static void
gfcp_usage(int error, struct gfpconcat_option *opt)
{
	fprintf(stderr,
"Usage: %s [-?] [-q (quiet)] [-v (verbose)] [-d (debug)]\n"
"\t[-c (compare after copy)]\n"
"\t[-f (overwrite existing file)]\n"
"\t[-h <destination hostname>]\n"
"\t[-j <#parallel(to copy parts)(connections)(default: %d)>]\n"
"\t[-m <minimum data size per a child process for parallel copying>]\n"
"\t[-p (report performance)]\n"
"\tinput-file(gfarm:... or file:...)\n"
"\toutput-file(gfarm:... or file:...)\n"
		, program_name, opt->n_para);
	if (error) {
		exit(EXIT_FAILURE);
	}
}

static void
gfcp_getopt(int argc, char **argv, struct gfpconcat_option *opt)
{
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
		case 'j':
			opt->n_para = strtol(optarg, NULL, 0);
			break;
		case 'm':
			opt->minimum_size = strtoll(optarg, NULL, 0);
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

	if (argc != 2) {
		switch (argc) {
		case 0:
			gfmsg_error("missing file operand");
			break;
		case 1:
			gfmsg_error("missing destination file operand");
			break;
		default:
			gfmsg_error("invalid file operand");
			break;
		}
		opt->usage_func(1, opt);
		/* NOTREACHED */
	}
	opt->n_part = 1;
	opt->parts = argv;
	opt->out_file = argv[1];
}

int
main(int argc, char *argv[])
{
	struct gfpconcat_option opt;

	gfpconcat_init(argc, argv, program_name,
	   gfcp_usage, gfcp_getopt, &opt);
	return (gfpconcat_main(&opt));
}

/*  mem.c - To compile: cc +DAportable -o mem mem.c  */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/param.h>
#include <sys/pstat.h>

extern char *optarg;
extern int optind, optopt;

int
main(int argc, char **argv)
{
	struct pst_static pst;
	union pstun pu;
	int64_t memsize;
	int c, unit = 1, verbose = 1;
	char *unit_string = " bytes";

	while ((c = getopt(argc, argv, "kmq")) != -1) {
		switch (c) {
		case 'k': unit = 1024; unit_string = " KB"; break;
		case 'm': unit = 1024 * 1024; unit_string = " MB"; break;
		case 'q': verbose = 0; break; /* i.e. quiet */
		case '?':
			fprintf(stderr, "%s: unrecognized option: - %c\n",
			    argv[0], optopt);
			return 1;
		}
	}

	pu.pst_static = &pst;
	if (pstat(PSTAT_STATIC, pu, sizeof(pst), (size_t)0, 0) == -1) {
		perror("pstat(PSTAT_GETSTATIC)");
		exit(1);
	}
	memsize = pst.physical_memory;
	memsize *= pst.page_size;
	memsize /= unit;

	printf("%s%" PRId64 "%s\n",
	    verbose ? "Physical RAM = " : "",
	    memsize,
	    verbose ? unit_string : "");
	return 0;
}

/*
 * From:
 *	comp.sys.hp.hpux FAQ
 *	8.32 How can I determine how much RAM my system has?
 *
 * Archive-name: hp/hpux-faq
 * Comp-sys-hpux-archive-name: hp/hpux-faq
 * Version: 11.23.0312.00
 * Last-modified: 2003/12/30
 * Maintainer: Ian P. Springer <ian_springer@hp.com>
 * URL: ftp://rtfm.mit.edu/pub/faqs/hp/hpux-faq
 * Revision-Frequency: monthly
 * Posting-Frequency: every 10 days
 * Disclaimer: Approval for *.answers is based on form, not content.
 * Copyright: (c)2001-2003 Ian P. Springer
 */

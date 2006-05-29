#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

char *program_name = "gfdf";

void
inode_title(void)
{
	printf("%12s%12s%12s%7s %s\n",
	    "Inodes", "IUsed", "IAvail", "%IUsed", "Host");
}

void
size_title(void)
{
	printf("%12s%12s%12s%9s %s\n",
	    "1K-blocks", "Used", "Avail", "Capacity", "Host");
}

char *
inode_print(char *host)
{
	char *e;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail, files, ffree, favail;

	e = gfs_statfsnode(host, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", host, e);
		return (e);
	}
	printf("%12.0f%12.0f%12.0f  %3.0f%%  %s\n", 
	    (double)files, (double)files - ffree, (double)favail,
	    (double)(files - ffree)/(files - (ffree - favail))*100.0, host);
	return (NULL);
}

char *
size_print(char *host)
{
	char *e;
	gfarm_int32_t bsize;
	file_offset_t blocks, bfree, bavail, files, ffree, favail;

	e = gfs_statfsnode(host, &bsize,
	    &blocks, &bfree, &bavail, &files, &ffree, &favail);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", host, e);
		return (e);
	}
	printf("%12.0f%12.0f%12.0f   %3.0f%%   %s\n", 
	    (double)(blocks * bsize) / 1024.0,
	    (double)(blocks - bfree) * bsize / 1024.,
	    (double)bavail * bsize / 1024.0,
	    (double)(blocks - bfree)/(blocks - (bfree - bavail))*100.0,
	    host);
	return (NULL);
}

#define EXIT_INVALID_ARGUMENT	2

void
usage(void)
{
	fprintf(stderr, "Usage:\t%s -h <host>\n", program_name);
	fprintf(stderr, "\t%s -H <hostfile>\n", program_name);
	exit(EXIT_INVALID_ARGUMENT);
}

int
main(int argc, char **argv)
{
	int status = EXIT_SUCCESS;
	void (*title)(void) = size_title;
	char *(*print)(char *) = size_print;
	char *e, **hosts = NULL;
	int c, i, lineno, ninfos, nhosts = 0;
	gfarm_stringlist hostlist;
	struct gfarm_host_info *infos;

	if ((e = gfarm_initialize(&argc, &argv)) != NULL) {
		fprintf(stderr, "%s: gfarm initialize: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}

	gfarm_stringlist_init(&hostlist);

	while ((c = getopt(argc, argv, "H:h:i?")) != -1) {
		switch (c) {
		case 'H':
			e = gfarm_hostlist_read(optarg, &nhosts, &hosts,
			    &lineno);
			if (e != NULL) {
				if (lineno != -1) {
					fprintf(stderr,
					    "%s: %s: line %d: %s\n",
					    program_name, optarg, lineno, e);
				} else {
					fprintf(stderr, "%s: %s: %s\n",
					    program_name, optarg, e);
				}
				exit(EXIT_INVALID_ARGUMENT);
			}
			break;
		case 'h':
			gfarm_stringlist_add(&hostlist, optarg);
			break;
		case 'i':
			title = inode_title;
			print = inode_print;
			break;
		case '?':
			/*FALLTHROUGH*/
		default:
			usage();
		}
	}
	if (optind < argc)
		usage();

	(*title)();

	if (nhosts == 0 && gfarm_stringlist_length(&hostlist) == 0) {
		/* list all filesystem node */
		e = gfarm_host_info_get_all(&ninfos, &infos);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			status = EXIT_FAILURE;
		} else {
			for (i = 0; i < ninfos; i++) {
				if ((*print)(infos[i].hostname) != NULL)
					status = EXIT_FAILURE;
			}
		}
	} else {
		for (i = 0; i < nhosts; i++) {
			if ((*print)(hosts[i]) != NULL)
				status = EXIT_FAILURE;
		}
		for (i = 0; i < gfarm_stringlist_length(&hostlist); i++) {
			if (print(gfarm_stringlist_elem(&hostlist, i)) != NULL)
				status = EXIT_FAILURE;
		}
	}
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(&hostlist);


	if ((e = gfarm_terminate()) != NULL) {
		fprintf(stderr, "%s: gfarm terminate: %s\n", program_name, e);
		exit(EXIT_FAILURE);
	}
	return (status);
}

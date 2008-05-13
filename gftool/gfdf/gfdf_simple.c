/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "config.h"

char *program_name = "gfdf";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-a] [-D domain]\n", program_name);
	exit(1);
}

gfarm_error_t
display_statfs(const char *dummy)
{
	gfarm_error_t e;
	gfarm_off_t used, avail, files;

	e = gfm_client_statfs(gfarm_metadb_server, &used, &avail, &files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	printf("%12s%12s%12s%9s%12s\n",
	       "1K-blocks", "Used", "Avail", "Capacity", "Files");
	printf("%12" GFARM_PRId64 "%12" GFARM_PRId64
	       "%12" GFARM_PRId64 "   %3.0f%%  %12" GFARM_PRId64 "\n",
	       used + avail, used, avail,
	       (double)used / (used + avail) * 100, files);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_statfs_nodes(const char *domain)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;
	gfarm_uint64_t used, avail;
	gfarm_uint64_t total_used = 0, total_avail = 0;

	e = gfm_client_schedule_host_domain(gfarm_metadb_server, domain,
		&nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	printf("%12s%12s%12s%9s %s\n",
	       "1K-blocks", "Used", "Avail", "Capacity", "Host");
	for (i = 0; i < nhosts; ++i) {
		used = hosts[i].disk_used;
		avail = hosts[i].disk_avail;
		printf("%12" GFARM_PRId64 "%12" GFARM_PRId64
		       "%12" GFARM_PRId64 "   %3.0f%%   %s\n",
		       used + avail, used, avail,
		       (double)used / (used + avail) * 100,
		       hosts[i].host);
		total_used += used;
		total_avail += avail;
	}
	if (nhosts > 0) {
		puts("---------------------------------------------");
		printf("%12" GFARM_PRId64 "%12" GFARM_PRId64
		       "%12" GFARM_PRId64 "   %3.0f%%\n",
		       total_used + total_avail, total_used, total_avail,
		       (double)total_used / (total_used + total_avail) * 100);
	}
	else
		puts("No file system node");

	return (GFARM_ERR_NO_ERROR);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char c, *domain = "";
	gfarm_error_t (*statfs)(const char *) = display_statfs_nodes;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "aD:?")) != -1) {
		switch (c) {
		case 'a':
			statfs = display_statfs;
			break;
		case 'D':
			domain = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	e = statfs(domain);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	exit (0);
}

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
	fprintf(stderr, "Usage: %s [-a] [-n] [-D domain]\n", program_name);
	exit(1);
}

gfarm_error_t
display_statfs(const char *dummy)
{
	gfarm_error_t e;
	gfarm_off_t used, avail, files;

	/* XXX FIXME: should implement and use gfs_statvfs */
	e = gfs_statfs(&used, &avail, &files);
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

/* XXX FIXME: should traverse all mounted metadata servers */
gfarm_error_t
schedule_host_domain(const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **hostsp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;

	if ((e = gfm_client_connection_and_process_acquire(
	    gfarm_metadb_server_name, gfarm_metadb_server_port,
	    &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    nhostsp, hostsp);
	gfm_client_connection_free(gfm_server);
	return (e);
}

gfarm_error_t
display_statfs_nodes(const char *domain)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;
	gfarm_uint64_t used, avail;
	gfarm_uint64_t total_used = 0, total_avail = 0;

	e = schedule_host_domain(domain, &nhosts, &hosts);
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

	gfarm_host_sched_info_free(nhosts, hosts);
 
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_nodes(const char *domain)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;

	e = schedule_host_domain(domain, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < nhosts; ++i)
		printf("%s\n", hosts[i].host);

	gfarm_host_sched_info_free(nhosts, hosts);
	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char *domain = "";
	gfarm_error_t (*statfs)(const char *) = display_statfs_nodes;
	int c;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "anD:?")) != -1) {
		switch (c) {
		case 'a':
			statfs = display_statfs;
			break;
		case 'n':
			statfs = display_nodes;
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

/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "lookup.h"

char *program_name = "gfdf";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-a] [-n] [-P path] [-D domain]\n",
		program_name);
	exit(1);
}

#define TITLE_FORMAT "%13s %13s %13s %4s"
#define DATA_FORMAT "%13lld %13lld %13lld %3.0f%%"

gfarm_error_t
display_statfs(const char *path, const char *dummy)
{
	gfarm_error_t e;
	gfarm_off_t used, avail, files;

	/* XXX FIXME: should implement and use gfs_statvfs */
	e = gfs_statfs(&used, &avail, &files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	printf(TITLE_FORMAT " %12s\n",
	       "1K-blocks", "Used", "Avail", "Use%", "Files");
	printf(DATA_FORMAT " %12lld\n",
	       (unsigned long long)used + avail, (unsigned long long)used,
	       (unsigned long long)avail,
	       (double)used / (used + avail) * 100, (unsigned long long)files);

	return (GFARM_ERR_NO_ERROR);
}

/* XXX FIXME: should traverse all mounted metadata servers */
gfarm_error_t
schedule_host_domain(const char *path, const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **hostsp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;

	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    nhostsp, hostsp);
	gfm_client_connection_free(gfm_server);
	return (e);
}

gfarm_error_t
display_statfs_nodes(const char *path, const char *domain)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;
	gfarm_uint64_t used, avail;
	gfarm_uint64_t total_used = 0, total_avail = 0;

	e = schedule_host_domain(path, domain, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	printf(TITLE_FORMAT " %s\n",
	       "1K-blocks", "Used", "Avail", "Use%", "Host");
	for (i = 0; i < nhosts; ++i) {
		used = hosts[i].disk_used;
		avail = hosts[i].disk_avail;
		printf(DATA_FORMAT " %s\n",
		       (unsigned long long)used + avail,
		       (unsigned long long)used, (unsigned long long)avail,
		       (double)used / (used + avail) * 100,
		       hosts[i].host);
		total_used += used;
		total_avail += avail;
	}
	if (nhosts > 0) {
		puts("----------------------------------------------");
		printf(DATA_FORMAT "\n",
		       (unsigned long long)total_used + total_avail,
		       (unsigned long long)total_used,
		       (unsigned long long)total_avail,
		       (double)total_used / (total_used + total_avail) * 100);
	}
	else
		puts("No file system node");

	gfarm_host_sched_info_free(nhosts, hosts);
 
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_nodes(const char *path, const char *domain)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;

	e = schedule_host_domain(path, domain, &nhosts, &hosts);
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
	const char *domain = "", *path = GFARM_PATH_ROOT;
	gfarm_error_t (*statfs)(const char *, const char *) =
		display_statfs_nodes;
	int c;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "anD:P:?")) != -1) {
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
		case 'P':
			path = optarg;
			break;
		case '?':
		default:
			usage();
		}
	}
	e = statfs(path, domain);
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

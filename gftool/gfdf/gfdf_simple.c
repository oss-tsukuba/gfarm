/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "lookup.h"

char *program_name = "gfdf";

enum sort_order {
	SO_NAME,
	SO_SIZE
} option_sort_order = SO_NAME;
static int option_reverse_sort = 0;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-anrS] [-P path] [-D domain]\n",
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

static int
compare_hostname(const void *s1, const void *s2)
{
	const struct gfarm_host_sched_info *h1 = s1;
	const struct gfarm_host_sched_info *h2 = s2;

	return (strcoll(h1->host, h2->host));
}

static int
compare_hostname_r(const void *s1, const void *s2)
{
	return (-compare_hostname(s1, s2));
}

static int
compare_available_capacity(const void *s1, const void *s2)
{
	const struct gfarm_host_sched_info *h1 = s1;
	const struct gfarm_host_sched_info *h2 = s2;
	gfarm_uint64_t a1, a2;

	a1 = h1->disk_avail;
	a2 = h2->disk_avail;

	if (a1 < a2)
		return (-1);
	else if (a1 > a2)
		return (1);
	else
		return (0);
}

static int
compare_available_capacity_r(const void *s1, const void *s2)
{
	return (-compare_available_capacity(s1, s2));
}

/* XXX FIXME: should traverse all mounted metadata servers */
gfarm_error_t
schedule_host_domain(const char *path, const char *domain,
	int *nhostsp, struct gfarm_host_sched_info **hostsp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int (*compare)(const void *, const void *);

	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    nhostsp, hostsp);
	gfm_client_connection_free(gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

#ifdef __GNUC__ /* workaround gcc warning: unused variable */
	compare = NULL;
#endif
	switch (option_sort_order) {
	case SO_NAME:
		compare = !option_reverse_sort ?
		    compare_hostname : compare_hostname_r;
		break;
	case SO_SIZE:
		compare = !option_reverse_sort ?
		    compare_available_capacity : compare_available_capacity_r;
		break;
	}
	qsort(*hostsp, *nhostsp, sizeof(**hostsp), compare);
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

	while ((c = getopt(argc, argv, "anrD:P:S?")) != -1) {
		switch (c) {
		case 'a':
			statfs = display_statfs;
			break;
		case 'n':
			statfs = display_nodes;
			break;
		case 'r':
			option_reverse_sort = 1;
			break;
		case 'D':
			domain = optarg;
			break;
		case 'P':
			path = optarg;
			break;
		case 'S':
			option_sort_order = SO_SIZE;
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

/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "liberror.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

char *program_name = "gfdf";

enum sort_order {
	SO_NAME,
	SO_SIZE
} option_sort_order = SO_NAME;
static int option_reverse_sort = 0;
static int option_formatting_flags = 0;

struct formatter {
	char *summary_title_format;
	char *summary_data_format;
	char *nodes_title_format;
	char *nodes_data_format;
	char *nodes_separator;
	size_t (*number_to_string)(char *, size_t, long long);
	size_t (*blocks_to_string)(char *, size_t, long long);
};

#define PRECISE_TITLE_FORMAT	"%13s %13s %13s %4s"
#define PRECISE_DATA_FORMAT	"%13s %13s %13s %3.0f%%"

#define READABLE_TITLE_FORMAT	"%9s %6s %6s %4s"
#define READABLE_DATA_FORMAT	"%9s %6s %6s %3.0f%%"

static size_t
precise_number(char *buf, size_t len, long long number)
{
	return (snprintf(buf, len, "%13lld", number));
}

static size_t
readable_number(char *buf, size_t len, long long number)
{
	return (gfarm_humanize_signed_number(buf, len, number,
	    option_formatting_flags));
}

static size_t
readable_blocks(char *buf, size_t len, long long number)
{
	return (gfarm_humanize_signed_number(buf, len, number * 1024,
	    option_formatting_flags));
}

const struct formatter precise_formatter = {
	PRECISE_TITLE_FORMAT	" %13s\n",
	PRECISE_DATA_FORMAT	" %13s\n",
	PRECISE_TITLE_FORMAT	" %s\n",
	PRECISE_DATA_FORMAT	" %s\n",
	"----------------------------------------------",
	precise_number,
	precise_number
};

const struct formatter readable_formatter = {
	READABLE_TITLE_FORMAT	" %6s\n",
	READABLE_DATA_FORMAT	" %6s\n",
	READABLE_TITLE_FORMAT	" %s\n",
	READABLE_DATA_FORMAT	" %s\n",
	"----------------------------",
	readable_number,
	readable_blocks
};

const struct formatter *formatter = &precise_formatter;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-ahHnrS] [-P path] [-D domain]\n",
		program_name);
	exit(1);
}

gfarm_error_t
display_statfs(const char *path, const char *dummy, int *ndisplayed)
{
	gfarm_error_t e;
	gfarm_off_t used, avail, files;
	char capbuf[GFARM_INT64STRLEN];
	char usedbuf[GFARM_INT64STRLEN];
	char availbuf[GFARM_INT64STRLEN];
	char filesbuf[GFARM_INT64STRLEN];

	*ndisplayed = 0;
	/* XXX FIXME: should implement and use gfs_statvfs */
	e = gfs_statfs(&used, &avail, &files);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	(*formatter->blocks_to_string)(capbuf, sizeof capbuf,
	    (long long)used + avail);
	(*formatter->blocks_to_string)(usedbuf, sizeof usedbuf,
	    (long long)used);
	(*formatter->blocks_to_string)(availbuf, sizeof availbuf,
	    (long long)avail);
	(*formatter->number_to_string)(filesbuf, sizeof filesbuf,
	    (long long)files);

	printf(formatter->summary_title_format,
	       "1K-blocks", "Used", "Avail", "Use%", "Files");
	printf(formatter->summary_data_format,
	       capbuf, usedbuf, availbuf,
	       (double)used / (used + avail) * 100,
	       filesbuf);

	*ndisplayed = 1;
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
display_statfs_nodes(const char *path, const char *domain, int *ndisplayed)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;
	gfarm_uint64_t used, avail;
	gfarm_uint64_t total_used = 0, total_avail = 0;
	char capbuf[GFARM_INT64STRLEN];
	char usedbuf[GFARM_INT64STRLEN];
	char availbuf[GFARM_INT64STRLEN];

	*ndisplayed = 0;
	e = schedule_host_domain(path, domain, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	printf(formatter->nodes_title_format,
	       "1K-blocks", "Used", "Avail", "Use%", "Host");
	for (i = 0; i < nhosts; ++i) {
		used = hosts[i].disk_used;
		avail = hosts[i].disk_avail;
		(*formatter->blocks_to_string)(capbuf, sizeof capbuf,
		    (long long)used + avail);
		(*formatter->blocks_to_string)(usedbuf, sizeof usedbuf,
		    (long long)used);
		(*formatter->blocks_to_string)(availbuf, sizeof availbuf,
		    (long long)avail);
		printf(formatter->nodes_data_format,
		       capbuf, usedbuf, availbuf,
		       (double)used / (used + avail) * 100,
		       hosts[i].host);
		total_used += used;
		total_avail += avail;
	}
	if (nhosts > 0) {
		puts(formatter->nodes_separator);
		(*formatter->blocks_to_string)(capbuf, sizeof capbuf,
		    (long long)total_used + total_avail);
		(*formatter->blocks_to_string)(usedbuf, sizeof usedbuf,
		    (long long)total_used);
		(*formatter->blocks_to_string)(availbuf, sizeof availbuf,
		    (long long)total_avail);
		printf(formatter->nodes_data_format,
		       capbuf, usedbuf, availbuf,
		       (double)total_used / (total_used + total_avail) * 100,
		       "");
	} else {
		fprintf(stderr, "%s\n",
		    gfarm_error_string(GFARM_ERRMSG_NO_FILESYSTEM_NODE));
	}

	gfarm_host_sched_info_free(nhosts, hosts);
	*ndisplayed = nhosts;
 
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_nodes(const char *path, const char *domain, int *ndisplayed)
{
	gfarm_error_t e;
	int nhosts, i;
	struct gfarm_host_sched_info *hosts;

	*ndisplayed = 0;
	e = schedule_host_domain(path, domain, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < nhosts; ++i)
		printf("%s\n", hosts[i].host);

	gfarm_host_sched_info_free(nhosts, hosts);
	*ndisplayed = nhosts;
	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	const char *domain = "", *path = ".";
	char *p = NULL;
	gfarm_error_t (*statfs)(const char *, const char *, int *) =
		display_statfs_nodes;
	int ndisplayed;
	int c;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "ahHnrD:P:S?")) != -1) {
		switch (c) {
		case 'a':
			statfs = display_statfs;
			break;
		case 'h':
			formatter = &readable_formatter;
			option_formatting_flags = GFARM_HUMANIZE_BINARY;
			break;
		case 'H':
			formatter = &readable_formatter;
			option_formatting_flags = 0;
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
	if (gfarm_realpath_by_gfarm2fs(path, &p) == GFARM_ERR_NO_ERROR)
		path = p;
	e = statfs(path, domain, &ndisplayed);
	free(p);
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
	if (ndisplayed == 0)
		exit(1);
	exit (0);
}

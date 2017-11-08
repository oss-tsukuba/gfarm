/*
 * $Id$
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>

#include <gfarm/gfarm.h>

#include "hash.h"

#include "liberror.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

char *program_name = "gfdf";

#define HOST_HASHTAB_SIZE	3079	/* prime number */

enum sort_order {
	SO_NONE,
	SO_NAME,
	SO_SIZE
} option_sort_order = SO_NAME;
static int option_reverse_sort = 0;
static int option_formatting_flags = 0;
#if 0
static int option_concurrency;
#endif

struct formatter {
	char *summary_title_format;
	char *summary_data_format;
	char *nodes_with_inode_title_format;
	char *nodes_with_inode_data_format;
	char *nodes_title_format;
	char *nodes_data_format;
	char *nodes_with_inode_separator;
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
	PRECISE_TITLE_FORMAT	PRECISE_TITLE_FORMAT	 " %s\n",
	PRECISE_DATA_FORMAT	PRECISE_DATA_FORMAT	"  %s\n",
	PRECISE_TITLE_FORMAT	" %s\n",
	PRECISE_DATA_FORMAT	" %s\n",
	"----------------------------------------------"
	"----------------------------------------------",
	"----------------------------------------------",
	precise_number,
	precise_number
};

const struct formatter readable_formatter = {
	READABLE_TITLE_FORMAT	" %6s\n",
	READABLE_DATA_FORMAT	" %6s\n",
	READABLE_TITLE_FORMAT	READABLE_TITLE_FORMAT	 " %s\n",
	READABLE_DATA_FORMAT	READABLE_DATA_FORMAT	"  %s\n",
	READABLE_TITLE_FORMAT	" %s\n",
	READABLE_DATA_FORMAT	" %s\n",
	"----------------------------"
	"----------------------------",
	"----------------------------",
	readable_number,
	readable_blocks
};

const struct formatter *formatter = &precise_formatter;

static void
usage(void)
{
	fprintf(stderr,
#if 0
	    "Usage: %s [-ahiHnruSUV] [-P path] [-D domain] [-j concurrency] "
#else
	    "Usage: %s [-ahiHnruSUV] [-P path] [-D domain] "
#endif
	    "[<host>...]\n",
	    program_name);
	exit(1);
}

gfarm_error_t
display_statfs_total(const char *path, const char *dummy,
	int argc_ignored, char **argv_ignored, int *ndisplayed)
{
	gfarm_error_t e;
	gfarm_off_t used, avail, files;
	char capbuf[GFARM_INT64STRLEN];
	char usedbuf[GFARM_INT64STRLEN];
	char availbuf[GFARM_INT64STRLEN];
	char filesbuf[GFARM_INT64STRLEN];

	*ndisplayed = 0;
	e = gfs_statfs_by_path(path, &used, &avail, &files);
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

struct df_info {
	gfarm_off_t block_root, block_non_root, block_used, block_avail;
	gfarm_off_t inode_root, inode_non_root, inode_used, inode_avail;
	char *host;
};

static int
df_info_compare_hostname(const void *s1, const void *s2)
{
	const struct df_info *h1 = s1;
	const struct df_info *h2 = s2;

	return (strcasecmp(h1->host, h2->host));
}

static int
df_info_compare_hostname_r(const void *s1, const void *s2)
{
	return (-df_info_compare_hostname(s1, s2));
}

static int
df_info_compare_available_capacity(const void *s1, const void *s2)
{
	const struct df_info *h1 = s1;
	const struct df_info *h2 = s2;
	gfarm_uint64_t a1, a2;

	a1 = h1->block_avail;
	a2 = h2->block_avail;

	if (a1 < a2)
		return (-1);
	else if (a1 > a2)
		return (1);
	else
		return (0);
}

static int
df_info_compare_available_capacity_r(const void *s1, const void *s2)
{
	return (-df_info_compare_available_capacity(s1, s2));
}

static void
df_info_sort(int nhosts, struct df_info *hosts)
{
	switch (option_sort_order) {
	case SO_NONE:
		break;
	case SO_NAME:
		qsort(hosts, nhosts, sizeof(*hosts),
		    !option_reverse_sort ?
		    df_info_compare_hostname :
		    df_info_compare_hostname_r);
		break;
	case SO_SIZE:
		qsort(hosts, nhosts, sizeof(*hosts),
		    !option_reverse_sort ?
		    df_info_compare_available_capacity :
		    df_info_compare_available_capacity_r);
		break;
	}
}

static void
df_info_free(int nhosts, struct df_info *hosts)
{
	int i;

	for (i = 0; i < nhosts; i++)
		free(hosts[i].host);
	free(hosts);
}

gfarm_error_t
statfsnode(const char *path, const char *domain,
	int argc, char **argv, int *nhostsp, struct df_info **hostsp)
{
	gfarm_error_t e;
	int i, n_host_info, nhosts;
	struct gfarm_host_info *host_info, *hi;
	struct df_info *hosts, *h;
	struct gfm_connection *gfm_server;

	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	if (argc <= 0) {
		e = gfm_client_host_info_get_all(gfm_server,
		    &n_host_info, &host_info);
		if (e != GFARM_ERR_NO_ERROR) {
			gfm_client_connection_free(gfm_server);
			return (e);
		}
	} else {
		gfarm_error_t *errors;

		GFARM_MALLOC_ARRAY(errors, argc);
		GFARM_MALLOC_ARRAY(host_info, argc);
		if (errors == NULL || host_info == NULL) {
			free(errors);
			free(host_info);
			gfm_client_connection_free(gfm_server);
			return (GFARM_ERR_NO_MEMORY);
		}
		e = gfm_client_host_info_get_by_namealiases(
		    gfm_server, argc, (const char **)argv, errors, host_info);
		if (e != GFARM_ERR_NO_ERROR) {
			free(errors);
			free(host_info);
			gfm_client_connection_free(gfm_server);
			return (e);
		}
		n_host_info = 0;
		for (i = 0; i < argc; i++) {
			if (errors[i] != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: %s\n",
				    argv[i], gfarm_error_string(errors[i]));
				continue;
			}
			if (n_host_info < i)
				host_info[n_host_info] = host_info[i];
			++n_host_info;
		}
		free(errors);
	}
	gfm_client_connection_free(gfm_server);

	if (n_host_info == 0) {
		gfarm_host_info_free_all(n_host_info, host_info);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}
	GFARM_MALLOC_ARRAY(hosts, n_host_info);
	if (hosts == NULL) {
		gfarm_host_info_free_all(n_host_info, host_info);
		return (GFARM_ERR_NO_MEMORY);
	}

	nhosts = 0;
	for (i = 0; i < n_host_info; i++) {
		gfarm_int32_t bsize;
		gfarm_off_t blocks, bfree, bavail, files, ffree, favail;

		hi = &host_info[i];
		e = gfs_statfsnode_by_path(path, hi->hostname, hi->port,
		    &bsize, &blocks, &bfree, &bavail, &files, &ffree, &favail);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s:%d: %s\n",
			    hi->hostname, hi->port, gfarm_error_string(e));
			continue;
		}
		h = &hosts[nhosts++];
		h->block_root = (double)blocks * bsize / 1024.0;
		h->block_non_root =
		    (double)(blocks - (bfree - bavail)) * bsize / 1024.0;
		h->block_used = (double)(blocks - bfree) * bsize / 1024.0;
		h->block_avail = (double)bavail * bsize / 1024.0;
		h->inode_root = files;
		h->inode_non_root = files - (ffree - favail);
		h->inode_used = files - ffree;
		h->inode_avail = favail;
		h->host = strdup(hi->hostname);
		if (h->host == NULL) {
			df_info_free(nhosts, hosts);
			gfarm_host_info_free_all(n_host_info, host_info);
			return (GFARM_ERR_NO_MEMORY);
		}

	}
	gfarm_host_info_free_all(n_host_info, host_info);

	df_info_sort(nhosts, hosts);
	*nhostsp = nhosts;
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
df_info_alloc_by_sched_info(int nsched, struct gfarm_host_sched_info *scheds,
	int argc, char **argv, int *nhostsp, struct df_info **hostsp)
{
	int i, j, nhosts = 0;
	struct gfarm_host_sched_info *s;
	struct df_info *hosts, *h;
	struct gfarm_hash_table *argv_set;
	struct gfarm_hash_entry *entry;
	struct { char exist; } *arg_data;

	if (argc <= 0) {
		argv_set = NULL;
		nhosts = nsched;
	} else {
		argv_set = gfarm_hash_table_alloc(HOST_HASHTAB_SIZE,
		    gfarm_hash_casefold_strptr,
		    gfarm_hash_key_equal_casefold_strptr);
		if (argv_set == NULL)
			return (GFARM_ERR_NO_MEMORY);
		for (i = 0; i < argc; i++) {
			entry = gfarm_hash_enter(argv_set, &argv[i],
			    sizeof(argv[i]), sizeof(*arg_data), NULL);
			if (entry == NULL) {
				gfarm_hash_table_free(argv_set);
				return (GFARM_ERR_NO_MEMORY);
			}
			arg_data = gfarm_hash_entry_data(entry);
			arg_data->exist = 0;
		}

		nhosts = 0;
		for (i = 0; i < nsched; i++) {
			s = &scheds[i];
			entry = gfarm_hash_lookup(argv_set,
			    &s->host, sizeof(s->host));
			if (entry == NULL)
				continue;
			++nhosts;
			arg_data = gfarm_hash_entry_data(entry);
			arg_data->exist = 1;
		}

		for (i = 0; i < argc; i++) {
			entry = gfarm_hash_lookup(argv_set,
			    &argv[i], sizeof(argv[i]));
			arg_data = gfarm_hash_entry_data(entry);
			if (!arg_data->exist) {
				fprintf(stderr, "%s: not registered in gfmd\n",
				    argv[i]);
			}
		}
	}
	if (nhosts == 0) {
		if (argv_set != NULL)
			gfarm_hash_table_free(argv_set);
		return (GFARM_ERR_NO_SUCH_OBJECT);
	}

	GFARM_MALLOC_ARRAY(hosts, nhosts);
	if (hosts == NULL) {
		if (argv_set != NULL)
			gfarm_hash_table_free(argv_set);
		return (GFARM_ERR_NO_MEMORY);
	}
	j = 0;
	for (i = 0; i < nsched; i++) {
		s = &scheds[i];
		if (argv_set != NULL &&
		    gfarm_hash_lookup(argv_set,
		    &s->host, sizeof(s->host)) == NULL)
			continue;
		h = &hosts[j++];
		h->block_root = h->block_non_root =
		    s->disk_used + s->disk_avail;
		h->block_used = s->disk_used;
		h->block_avail = s->disk_avail;
		h->inode_root = h->inode_non_root = 0;
		h->inode_used = 0;
		h->inode_avail = 0;
		h->host = strdup(s->host);
		if (h->host == NULL) {
			df_info_free(j, hosts);
			if (argv_set != NULL)
				gfarm_hash_table_free(argv_set);
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	if (argv_set != NULL)
		gfarm_hash_table_free(argv_set);

	*nhostsp = nhosts;
	*hostsp = hosts;
	return (GFARM_ERR_NO_ERROR);
}

/* XXX FIXME: should traverse all mounted metadata servers */
gfarm_error_t
schedule_host_domain(const char *path, const char *domain,
	int argc, char **argv, int *nhostsp, struct df_info **hostsp)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	struct gfarm_host_sched_info *scheds;
	int nscheds;

	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR)
		return (e);

	e = gfm_client_schedule_host_domain(gfm_server, domain,
	    &nscheds, &scheds);
	gfm_client_connection_free(gfm_server);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = df_info_alloc_by_sched_info(nscheds, scheds, argc, argv,
	    nhostsp, hostsp);
	gfarm_host_sched_info_free(nscheds, scheds);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	df_info_sort(*nhostsp, *hostsp);
	return (e);
}

static gfarm_error_t (*df_info_get)(const char *, const char *,
	int, char **, int *, struct df_info **) = schedule_host_domain;
static int option_uncached = 0;

gfarm_error_t
display_statfs_nodes_with_inode(const char *path, const char *domain,
	int argc, char **argv, int *ndisplayed)
{
	gfarm_error_t e;
	int nhosts, i;
	struct df_info *hosts, *h;
	gfarm_uint64_t total_block_root = 0, total_block_non_root = 0;
	gfarm_uint64_t total_block_used = 0, total_block_avail = 0;
	gfarm_uint64_t total_inode_root = 0, total_inode_non_root = 0;
	gfarm_uint64_t total_inode_used = 0, total_inode_avail = 0;
	char block_capbuf[GFARM_INT64STRLEN];
	char block_usedbuf[GFARM_INT64STRLEN];
	char block_availbuf[GFARM_INT64STRLEN];
	char inode_capbuf[GFARM_INT64STRLEN];
	char inode_usedbuf[GFARM_INT64STRLEN];
	char inode_availbuf[GFARM_INT64STRLEN];

	*ndisplayed = 0;
	e = df_info_get(path, domain, argc, argv, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	printf(formatter->nodes_with_inode_title_format,
	       "1K-blocks", "Used", "Avail", "Use%",
	       "Inodes", "IUsed", "IAvail", "IUse%", "Host");
	for (i = 0; i < nhosts; ++i) {
		h = &hosts[i];
		(*formatter->blocks_to_string)(
		    block_capbuf, sizeof block_capbuf,
		    (long long)h->block_root);
		(*formatter->blocks_to_string)(
		    block_usedbuf, sizeof block_usedbuf,
		    (long long)h->block_used);
		(*formatter->blocks_to_string)(
		    block_availbuf, sizeof block_availbuf,
		    (long long)h->block_avail);
		(*formatter->number_to_string)(
		    inode_capbuf, sizeof inode_capbuf,
		    (long long)h->inode_root);
		(*formatter->number_to_string)(
		    inode_usedbuf, sizeof inode_usedbuf,
		    (long long)h->inode_used);
		(*formatter->number_to_string)(
		    inode_availbuf, sizeof inode_availbuf,
		    (long long)h->inode_avail);

		printf(formatter->nodes_with_inode_data_format,
		       block_capbuf, block_usedbuf, block_availbuf,
		       (double)h->block_used / h->block_non_root * 100,
		       inode_capbuf, inode_usedbuf, inode_availbuf,
		       (double)h->inode_used / h->inode_non_root * 100,
		       h->host);
		total_block_root += h->block_root;
		total_block_non_root += h->block_non_root;
		total_block_used += h->block_used;
		total_block_avail += h->block_avail;
		total_inode_root += h->inode_root;
		total_inode_non_root += h->inode_non_root;
		total_inode_used += h->inode_used;
		total_inode_avail += h->inode_avail;
	}
	if (nhosts > 0) {
		puts(formatter->nodes_with_inode_separator);
		(*formatter->blocks_to_string)(
		    block_capbuf, sizeof block_capbuf,
		    (long long)total_block_root);
		(*formatter->blocks_to_string)(
		    block_usedbuf, sizeof block_usedbuf,
		    (long long)total_block_used);
		(*formatter->blocks_to_string)(
		    block_availbuf, sizeof block_availbuf,
		    (long long)total_block_avail);
		(*formatter->number_to_string)(
		    inode_capbuf, sizeof inode_capbuf,
		    (long long)total_inode_root);
		(*formatter->number_to_string)(
		    inode_usedbuf, sizeof inode_usedbuf,
		    (long long)total_inode_used);
		(*formatter->number_to_string)(
		    inode_availbuf, sizeof inode_availbuf,
		    (long long)total_inode_avail);
		printf(formatter->nodes_with_inode_data_format,
		       block_capbuf, block_usedbuf, block_availbuf,
		       (double)total_block_used / total_block_non_root * 100,
		       inode_capbuf, inode_usedbuf, inode_availbuf,
		       (double)total_inode_used / total_inode_non_root * 100,
		       "");
	} else {
		fprintf(stderr, "%s\n",
		    gfarm_error_string(GFARM_ERR_NO_FILESYSTEM_NODE));
	}

	df_info_free(nhosts, hosts);
	*ndisplayed = nhosts;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_statfs_nodes(const char *path, const char *domain,
	int argc, char **argv, int *ndisplayed)
{
	gfarm_error_t e;
	int nhosts, i;
	struct df_info *hosts, *h;
	gfarm_uint64_t total_block_root = 0, total_block_non_root = 0;
	gfarm_uint64_t total_block_used = 0, total_block_avail = 0;
	char capbuf[GFARM_INT64STRLEN];
	char usedbuf[GFARM_INT64STRLEN];
	char availbuf[GFARM_INT64STRLEN];

	*ndisplayed = 0;
	e = df_info_get(path, domain, argc, argv, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	printf(formatter->nodes_title_format,
	       "1K-blocks", "Used", "Avail", "Use%", "Host");
	for (i = 0; i < nhosts; ++i) {
		h = &hosts[i];
		(*formatter->blocks_to_string)(capbuf, sizeof capbuf,
		    (long long)h->block_root);
		(*formatter->blocks_to_string)(usedbuf, sizeof usedbuf,
		    (long long)h->block_used);
		(*formatter->blocks_to_string)(availbuf, sizeof availbuf,
		    (long long)h->block_avail);
		printf(formatter->nodes_data_format,
		       capbuf, usedbuf, availbuf,
		       (double)h->block_used / h->block_non_root * 100,
		       h->host);
		total_block_root += h->block_root;
		total_block_non_root += h->block_non_root;
		total_block_used += h->block_used;
		total_block_avail += h->block_avail;
	}
	if (nhosts > 0) {
		puts(formatter->nodes_separator);
		(*formatter->blocks_to_string)(capbuf, sizeof capbuf,
		    (long long)total_block_root);
		(*formatter->blocks_to_string)(usedbuf, sizeof usedbuf,
		    (long long)total_block_used);
		(*formatter->blocks_to_string)(availbuf, sizeof availbuf,
		    (long long)total_block_avail);
		printf(formatter->nodes_data_format,
		       capbuf, usedbuf, availbuf,
		       (double)total_block_used / total_block_non_root * 100,
		       "");
	} else {
		fprintf(stderr, "%s\n",
		    gfarm_error_string(GFARM_ERR_NO_FILESYSTEM_NODE));
	}

	df_info_free(nhosts, hosts);
	*ndisplayed = nhosts;

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
display_nodes(const char *path, const char *domain,
	int argc, char **argv, int *ndisplayed)
{
	gfarm_error_t e;
	int nhosts, i;
	struct df_info *hosts;

	*ndisplayed = 0;
	e = df_info_get(path, domain, argc, argv, &nhosts, &hosts);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	for (i = 0; i < nhosts; ++i)
		printf("%s\n", hosts[i].host);

	df_info_free(nhosts, hosts);
	*ndisplayed = nhosts;
	return (e);
}

long
parse_opt_long(char *option, int option_char, char *argument_name)
{
	long value;
	char *s;

	errno = 0;
	value = strtol(option, &s, 0);
	if (s == option) {
		fprintf(stderr, "%s: missing %s after -%c\n",
		    program_name, argument_name, option_char);
		usage();
	} else if (*s != '\0') {
		fprintf(stderr, "%s: garbage in -%c %s\n",
		    program_name, option_char, option);
		usage();
	} else if (errno != 0 && (value == LONG_MIN || value == LONG_MAX)) {
		fprintf(stderr, "%s: %s with -%c %s\n",
		    program_name, strerror(errno), option_char, option);
		usage();
	}
	return (value);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	const char *domain = "", *path = ".";
	char *p = NULL;
	gfarm_error_t (*statfs_sw)(const char *, const char *, int, char **,
	    int *) = display_statfs_nodes;
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

	while ((c = getopt(argc, argv, "ahiHnruD:P:SUV?")) != -1) {
		switch (c) {
		case 'a':
			statfs_sw = display_statfs_total;
			break;
		case 'h':
			formatter = &readable_formatter;
			option_formatting_flags = GFARM_HUMANIZE_BINARY;
			break;
		case 'H':
			formatter = &readable_formatter;
			option_formatting_flags = 0;
			break;
		case 'i':
			statfs_sw = display_statfs_nodes_with_inode;
			/* schedule_host_domain() cannot provide inode info */
			df_info_get = statfsnode;
			break;
#if 0
		case 'j':
			option_concurrency = parse_opt_long(optarg,
			    c, "<concurrency>");
			break;
#endif
		case 'n':
			statfs_sw = display_nodes;
			break;
		case 'r':
			option_reverse_sort = 1;
			break;
		case 'u':
			df_info_get = statfsnode;
			option_uncached = 1;
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
		case 'U':
			option_sort_order = SO_NONE;
			break;
		case 'V':
			fprintf(stderr, "Gfarm version %s\n", gfarm_version());
			exit(0);
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (statfs_sw == display_statfs_total && option_uncached) {
		fprintf(stderr,
		    "%s: cannot specify -a and -u at the same time\n",
		program_name);
		exit(1);
	}

	if (gfarm_realpath_by_gfarm2fs(path, &p) == GFARM_ERR_NO_ERROR)
		path = p;
	e = statfs_sw(path, domain, argc, argv, &ndisplayed);
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
	exit(0);
}

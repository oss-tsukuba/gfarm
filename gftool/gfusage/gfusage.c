/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfm_client.h"
#include "quota_info.h"

char *program_name = "gfusage";

/* XXX FIXME: this doesn't support multiple metadata server. */
struct gfm_connection *gfarm_metadb_server;

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-g] [name]\n", program_name);
	exit(1);
}

static char head_user[]  = " username";
static char head_group[] = "groupname";
static char head_space[] = "FileSpace";
static char head_num[] = "FileNum";
static char head_phy_space[] = "PhysicalSpace";
static char head_phy_num[] = "PhysicalNum";

static char header_format[] = "#  %s : %15s %11s %15s %11s\n";

static int
print_usage_common(const char *name, int mode_group)
{
	struct gfarm_quota_get_info qi;
	gfarm_error_t e;

	if (mode_group)
		e = gfm_client_quota_group_get(
			gfarm_metadb_server, name, &qi);
	else
		e = gfm_client_quota_user_get(
			gfarm_metadb_server, name, &qi);
	if (e == GFARM_ERR_OPERATION_NOT_PERMITTED)
		return (0);
	else if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		fprintf(stderr,
			"%s : quota is not enabled."
			" (Please run gfquotacheck)\n", name);
		return (1);
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s : %s\n", name, gfarm_error_string(e));
		return (1);
	} else {
		printf("%12s :"
		       " %15"GFARM_PRId64" %11"GFARM_PRId64
		       " %15"GFARM_PRId64" %11"GFARM_PRId64"\n"
		       , name, qi.space, qi.num, qi.phy_space, qi.phy_num);
		gfarm_quota_get_info_free(&qi);
		return (1);
	}
}

static void
print_header_user()
{
	printf(header_format, head_user, head_space, head_num,
	       head_phy_space, head_phy_num);
}

static void
print_header_group()
{
	printf(header_format, head_group, head_space, head_num,
	       head_phy_space, head_phy_num);
}

static int
print_usage_user(const char *name)
{
	return (print_usage_common(name, 0));
}

static int
print_usage_group(const char *name)
{
	return (print_usage_common(name, 1));
}

static gfarm_error_t
list_user_one(const char *name)
{
	print_header_user();
	if (print_usage_common(name, 0) == 0)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
list_group_one(const char *name)
{
	print_header_group();
	if (print_usage_common(name, 1) == 0)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
list_user()
{
	struct gfarm_user_info *users;
	gfarm_error_t e;
	int nusers, i, count = 0;

	e = gfm_client_user_info_get_all(gfarm_metadb_server, &nusers, &users);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	print_header_user();
	for (i = 0; i < nusers; i++) {
		count += print_usage_user(users[i].username);
		gfarm_user_info_free(&users[i]);
	}
	if (count == 0)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	free(users);
	return (e);
}

static gfarm_error_t
list_group()
{
	struct gfarm_group_info *groups;
	gfarm_error_t e;
	int ngroups, i, count = 0;

	e = gfm_client_group_info_get_all(gfarm_metadb_server,
					  &ngroups, &groups);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	print_header_group();
	for (i = 0; i < ngroups; i++) {
		count += print_usage_group(groups[i].groupname);
		gfarm_group_info_free(&groups[i]);
	}
	if (count == 0)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	free(groups);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	int mode_group = 0; /* default: users list */
	char *name = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	/* XXX FIXME: this doesn't support multiple metadata server. */
	if ((e = gfm_client_connection_and_process_acquire(
	    gfarm_metadb_server_name, gfarm_metadb_server_port,
	    &gfarm_metadb_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "metadata server `%s', port %d: %s\n",
		    gfarm_metadb_server_name, gfarm_metadb_server_port,
		    gfarm_error_string(e));
		exit(1);
	}
	while ((c = getopt(argc, argv, "gh?")) != -1) {
		switch (c) {
		case 'g':
			mode_group = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0)
		name = argv[0];

	if (mode_group) {
		if (name)
			e = list_group_one(name);
		else
			e = list_group();
	} else {
		if (name)
			e = list_user_one(name);
		else
			e = list_user();
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = 1;
	}
	/* XXX FIXME: this doesn't support multiple metadata server. */
	gfm_client_connection_free(gfarm_metadb_server);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

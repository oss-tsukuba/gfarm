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
#include "lookup.h"
#include "quota_info.h"
#include "gfarm_path.h"

char *program_name = "gfusage";

static struct gfm_connection *gfm_server;
static int opt_format_flags = 0;
static int opt_humanize_number = 0;

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-P <path>] [-gHh] [name]\n", program_name);
	exit(1);
}

static const char head_user[]  = " UserName";
static const char head_group[] = "GroupName";
static const char head_space[] = "FileSpace";
static const char head_num[] = "FileNum";
static const char head_phy_space[] = "PhysicalSpace";
static const char head_phy_num[] = "PhysicalNum";

static const char header_format[] = "#  %s : %15s %11s %15s %11s\n";

static char *
humanize(long long num)
{
	static char buf[GFARM_INT64STRLEN];

	gfarm_humanize_number(buf, sizeof buf, num, opt_format_flags);
	return (buf);
}

static gfarm_error_t
print_usage_common(const char *name, int opt_group)
{
	struct gfarm_quota_get_info qi;
	gfarm_error_t e;

	if (opt_group)
		e = gfm_client_quota_group_get(gfm_server, name, &qi);
	else
		e = gfm_client_quota_user_get(gfm_server, name, &qi);
	if (e == GFARM_ERR_OPERATION_NOT_PERMITTED) /* not report here */
		return (e);
	else if (e == GFARM_ERR_NO_SUCH_OBJECT) { /* not enabled */
		fprintf(stderr, "%s : quota is not enabled.\n", name);
		return (e);
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s : %s\n",
			program_name, name, gfarm_error_string(e));
		return (e);
	}
	printf("%12s : ", name);
	if (opt_humanize_number) {
		printf("%15s ", humanize(qi.space));
		printf("%11s ", humanize(qi.num));
		printf("%15s ", humanize(qi.phy_space));
		printf("%11s\n", humanize(qi.phy_num));
	} else
		printf("%15"GFARM_PRId64" %11"GFARM_PRId64
		       " %15"GFARM_PRId64" %11"GFARM_PRId64"\n",
		       qi.space, qi.num, qi.phy_space, qi.phy_num);
	gfarm_quota_get_info_free(&qi);
	return (GFARM_ERR_NO_ERROR);
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

static gfarm_error_t
print_usage_user(const char *name)
{
	return (print_usage_common(name, 0));
}

static gfarm_error_t
print_usage_group(const char *name)
{
	return (print_usage_common(name, 1));
}

static gfarm_error_t
usage_user_one(const char *name)
{
	print_header_user();
	return (print_usage_user(name));
}

static gfarm_error_t
usage_group_one(const char *name)
{
	print_header_group();
	return (print_usage_group(name));
}

static gfarm_error_t
usage_user_all()
{
	struct gfarm_user_info *users;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int nusers, i, success = 0;

	e = gfm_client_user_info_get_all(gfm_server, &nusers, &users);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfm_client_user_info_get_all: %s\n",
			program_name, gfarm_error_string(e));
		return (e);
	}

	print_header_user();
	for (i = 0; i < nusers; i++) {
		e = print_usage_user(users[i].username);
		if (e == GFARM_ERR_NO_ERROR)
			success++;
		else {
			/* GFARM_ERR_NO_SUCH_OBJECT is preferred */
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e_save = e;
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
		gfarm_user_info_free(&users[i]);
	}
	free(users);

	if (success > 0)
		return (GFARM_ERR_NO_ERROR);
	else
		return (e_save);
}

static gfarm_error_t
usage_group_all()
{
	struct gfarm_group_info *groups;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int ngroups, i, success = 0;

	e = gfm_client_group_info_get_all(gfm_server, &ngroups, &groups);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfm_client_group_info_get_all: %s\n",
			program_name, gfarm_error_string(e));
		return (e);
	}

	print_header_group();
	for (i = 0; i < ngroups; i++) {
		e = print_usage_group(groups[i].groupname);
		if (e == GFARM_ERR_NO_ERROR)
			success++;
		else {
			/* GFARM_ERR_NO_SUCH_OBJECT is preferred */
			if (e == GFARM_ERR_NO_SUCH_OBJECT)
				e_save = e;
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = e;
		}
		gfarm_group_info_free(&groups[i]);
	}
	free(groups);

	if (success > 0)
		return (GFARM_ERR_NO_ERROR);
	else
		return (e_save);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	int opt_group = 0; /* default: users list */
	char *name = NULL, *realpath = NULL;
	const char *path = ".";

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "P:gHh?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case 'g':
			opt_group = 1;
			break;
		case 'H':
			opt_humanize_number = 1;
			opt_format_flags = 0;
			break;
		case 'h':
			opt_humanize_number = 1;
			opt_format_flags = GFARM_HUMANIZE_BINARY;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	if ((e = gfm_client_connection_and_process_acquire_by_path(
		     path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			program_name, path, gfarm_error_string(e));
		status = 1;
		goto terminate;
	}

	if (argc > 0)
		name = argv[0];

	if (name) {
		if (opt_group)
			e = usage_group_one(name);
		else
			e = usage_user_one(name);
	} else {
		if (opt_group)
			e = usage_group_all();
		else
			e = usage_user_all();
	}
	if (e != GFARM_ERR_NO_ERROR)
		status = 1;
	if (e == GFARM_ERR_OPERATION_NOT_PERMITTED)
		fprintf(stderr, "%s: %s\n", program_name,
			gfarm_error_string(e));
	gfm_client_connection_free(gfm_server);
terminate:
	free(realpath);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

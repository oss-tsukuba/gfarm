/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>

#include <gfarm/gfarm.h>

#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

#include "config.h"
#include "quota_info.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

const char *program_name = "gfedquota";

#ifdef HAVE_GETOPT_LONG
#define OPT(s, l, m) fprintf(stderr, "  -%s,--%s\t%s\n", s, l, m)
#else
#define OPT(s, l, m) fprintf(stderr, "  -%s\t%s\n", s, m)
#endif

static void
usage(void)
{
	fprintf(stderr,
		"Usage:"
		"\t%s -u <user_name> [options]\n"
		"\t%s -g <group_name>) [options]\n"
		"\t%s -D <dirset_name> [-u <user_name>] [options]\n"
		"Options:\n"
		"(If the value is 'disable' or -1, the limit is disabled):\n",
		program_name, program_name, program_name);
	OPT("P", "path=NAME", "pathname "
		"(to select one of multiple metadata servers)");
	OPT("u", "user=NAME", "username");
	OPT("g", "group=NAME", "groupname");
	OPT("D", "dirset=NAME", "dirset name (for directory quota)");
	OPT("G", "grace=SEC", "grace period for all SoftLimits");
	OPT("s", "softspc=BYTE", "SoftLimit of total used file space");
	OPT("h", "hardspc=BYTE", "HardLimit of total used file space");
	OPT("m", "softnum=NUM", "SoftLimit of total used file number");
	OPT("n", "hardnum=NUM", "HardLimit of total used file number");
	OPT("S", "physoftspc=BYTE", "SoftLimit of total used physical space");
	OPT("H", "phyhardspc=BYTE", "HardLimit of total used physical space");
	OPT("M", "physoftnum=NUM", "SoftLimit of total used physical number");
	OPT("N", "phyhardnum=NUM", "HardLimit of total used physical number");
	OPT("?", "help", "this help message");

	exit(2);
}

static void
gfarm_quota_set_info_to_limit_info(
	const struct gfarm_quota_set_info *si,
	struct gfarm_quota_limit_info *li)
{
	li->grace_period = si->grace_period;
	li->soft.space = si->space_soft;
	li->hard.space = si->space_hard;
	li->soft.num = si->num_soft;
	li->hard.num = si->num_hard;
	li->soft.phy_space = si->phy_space_soft;
	li->hard.phy_space = si->phy_space_hard;
	li->soft.phy_num = si->phy_num_soft;
	li->hard.phy_num = si->phy_num_hard;
}

static gfarm_int64_t
convert_value(const char *str)
{
	gfarm_int64_t val;
	char *endptr;

	if (strcmp(str, "disable") == 0)
		return (GFARM_QUOTA_INVALID);

	val = strtoll(str, &endptr, 10);
	if (*endptr != '\0') {
		fprintf(stderr, "ignore invalid parameter: %s\n", str);
		return (GFARM_QUOTA_NOT_UPDATE);
	}
	if (val == -1)
		return (GFARM_QUOTA_INVALID);
	else if (val < -1)
		return (GFARM_QUOTA_NOT_UPDATE);
	else
		return (val);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	char *username = NULL, *groupname = NULL, *dirsetname = NULL;
	char *optstring = "P:u:D:g:G:s:h:m:n:S:H:M:N:?";
#ifdef HAVE_GETOPT_LONG
	struct option long_options[] = {
		{"path", 1, NULL, 'P'},
		{"user", 1, NULL, 'u'},
		{"dirset", 1, NULL, 'D'},
		{"group", 1, NULL, 'g'},
		{"grace", 1, NULL, 'G'},
		{"softspc", 1, NULL, 's'},
		{"hardspc", 1, NULL, 'h'},
		{"softnum", 1, NULL, 'm'},
		{"hardnum", 1, NULL, 'n'},
		{"physoftspc", 1, NULL, 'S'},
		{"phyhardspc", 1, NULL, 'H'},
		{"physoftnum", 1, NULL, 'M'},
		{"phyhardnum", 1, NULL, 'N'},
		{"help", 0, NULL, '?'},
		{0, 0, 0, 0}
	};
#endif
	struct gfarm_quota_set_info qi = {
		NULL,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
		GFARM_QUOTA_NOT_UPDATE,
	};
	struct gfm_connection *gfm_server;
	const char *path = ".";
	char *realpath = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
			gfarm_error_string(e));
		exit(1);
	}

	while (1) {
#ifdef HAVE_GETOPT_LONG
		int option_index = 0;
		c = getopt_long(argc, argv, optstring,
				long_options, &option_index);
#else
		c = getopt(argc, argv, optstring);
#endif
		if (c == -1)
			break;
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case 'u':
			username = optarg;
			break;
		case 'g':
			groupname = optarg;
			break;
		case 'D':
			dirsetname = optarg;
			break;
		case 'G':
			qi.grace_period = convert_value(optarg);
			break;
		case 's':
			qi.space_soft = convert_value(optarg);
			break;
		case 'h':
			qi.space_hard = convert_value(optarg);
			break;
		case 'm':
			qi.num_soft = convert_value(optarg);
			break;
		case 'n':
			qi.num_hard = convert_value(optarg);
			break;
		case 'S':
			qi.phy_space_soft = convert_value(optarg);
			break;
		case 'H':
			qi.phy_space_hard = convert_value(optarg);
			break;
		case 'M':
			qi.phy_num_soft = convert_value(optarg);
			break;
		case 'N':
			qi.phy_num_hard = convert_value(optarg);
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
	free(realpath);

	if (username != NULL && groupname == NULL && dirsetname == NULL) {
		qi.name = username;
		e = gfm_client_quota_user_set(gfm_server, &qi);
	} else if (groupname != NULL &&
	    username == NULL && dirsetname == NULL) {
		qi.name = groupname;
		e = gfm_client_quota_group_set(gfm_server, &qi);
	} else if (dirsetname != NULL && groupname == NULL) {
		struct gfarm_quota_limit_info li;
		const char *uname;

		if (username != NULL)
			uname = username;
		else {
			e = gfm_client_get_username_in_tenant(gfm_server,
			    &uname);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "%s: failed to get self user name: %s\n",
				    program_name, gfarm_error_string(e));
				exit(EXIT_FAILURE);
			}
		}

		gfarm_quota_set_info_to_limit_info(&qi, &li);
		e = gfm_client_quota_dirset_set(gfm_server, uname,
		    dirsetname, &li);
	} else
		usage();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = 1;
	}
	gfm_client_connection_free(gfm_server);
terminate:
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

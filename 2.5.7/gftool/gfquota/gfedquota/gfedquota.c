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
#include "gfm_client.h"
#include "lookup.h"
#include "quota_info.h"

char *program_name = "gfedquota";

#ifdef HAVE_GETOPT_LONG
#define OPT(s, l, m) fprintf(stderr, "  -%s,--%s\t%s\n", s, l, m)
#else
#define OPT(s, l, m) fprintf(stderr, "  -%s\t%s\n", s, m)
#endif

static void
usage(void)
{
	fprintf(stderr,
		"Usage:\t%s "
		"[-P <path>] -u username (or -g groupname) [options]\n"
		"Options:\n"
		"(If the value is 'disable' or -1, the limit is disabled):\n",
		program_name);
	OPT("P", "path=NAME", "pathname (for multiple metadata servers)");
	OPT("u", "user=NAME", "username");
	OPT("g", "group=NAME", "groupname");
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

	exit(1);
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
	char *username = NULL, *groupname = NULL;
	char *optstring = "P:u:g:G:s:h:m:n:S:H:M:N:?";
#ifdef HAVE_GETOPT_LONG
	struct option long_options[] = {
		{"path", 1, NULL, 'P'},
		{"user", 1, NULL, 'u'},
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
        const char *path = GFARM_PATH_ROOT;

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
			username = strdup(optarg);
			break;
		case 'g':
			groupname = strdup(optarg);
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

	if ((e = gfm_client_connection_and_process_acquire_by_path(
		     path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			program_name, path, gfarm_error_string(e));
		status = -1;
		goto terminate;
	}

	if (username != NULL && groupname == NULL) {
		qi.name = username;
		e = gfm_client_quota_user_set(gfm_server, &qi);
	} else if (username == NULL && groupname != NULL) {
		qi.name = groupname;
		e = gfm_client_quota_group_set(gfm_server, &qi);
	} else
		usage();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = -2;
	}
	gfarm_quota_set_info_free(&qi);
	gfm_client_connection_free(gfm_server);
terminate:
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = -3;
	}
	return (status);
}

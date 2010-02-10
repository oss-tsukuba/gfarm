/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>

#include <gfarm/gfarm.h>
#include "config.h"
#include "gfm_client.h"
#include "lookup.h"
#include "quota_info.h"

char *program_name = "gfquota";

static void
usage(void)
{
	fprintf(stderr,
		"Usage:\t%s [-P <path>] [-u username | -g groupname]\n",
		program_name);
	exit(1);
}

#define EXCEEDED(f, type) fprintf(f, "warning: %s exceeded\n", type)
#define EXPIRED(f, type) fprintf(f, "warning: %s expired\n", type)

static int
quota_check_and_warning(FILE *f, struct gfarm_quota_get_info *q)
{
	int count = 0;

	/* hardlimit exceeded */
	if ((quota_limit_is_valid(q->space_hard) &&
	     q->space >= q->space_hard)) {
		EXCEEDED(f, "FileSpaceHardLimit");
		count++;
	}
	if (quota_limit_is_valid(q->num_hard) &&
	    q->num >= q->num_hard) {
		EXCEEDED(f, "FileNumHardLimit");
		count++;
	}
	if (quota_limit_is_valid(q->phy_space_hard) &&
	    q->phy_space >= q->phy_space_hard) {
		EXCEEDED(f, "PhysicalSpaceHardLimit");
		count++;
	}
	if (quota_limit_is_valid(q->phy_num_hard) &&
	    q->phy_num >= q->phy_num_hard) {
		EXCEEDED(f, "PhysicalNumHardLimit");
		count++;
	}

	if (!quota_limit_is_valid(q->grace_period))
		return (count);

	/* softlimit expired or eceeded */
	if (quota_limit_is_valid(q->space_soft)) {
		if (q->space_grace == 0) {
			EXPIRED(f, "FileSpaceSoftLimit");
			count++;
		} else if (q->space > q->space_soft) {
			EXCEEDED(f, "FileSpaceSoftLimit");
			count++;
		}
	}
	if (quota_limit_is_valid(q->num_soft)) {
		if (q->num_grace == 0) {
			EXPIRED(f, "FileNumSoftLimit");
			count++;
		} else if (q->num > q->num_soft) {
			EXCEEDED(f, "FileNumSoftLimit");
			count++;
		}
	}
	if (quota_limit_is_valid(q->phy_space_soft)) {
		if (q->phy_space_grace == 0) {
			EXPIRED(f, "PhysicalSpaceSoftLimit");
			count++;
		} else if (q->phy_space > q->phy_space_soft) {
			EXCEEDED(f, "PhysicalSpaceSoftLimit");
			count++;
		}
	}
	if (quota_limit_is_valid(q->phy_num_soft)) {
		if (q->phy_num_grace == 0) {
			EXPIRED(f, "PhysicalNumSoftLimit");
			count++;
		} else if (q->phy_num > q->phy_num_soft) {
			EXCEEDED(f, "PhysicalNumSoftLimit");
			count++;
		}
	}
	return (count);
}

#define PRINT(f, s, v)							\
	{								\
		if (quota_limit_is_valid(v))				\
			fprintf(f, "%s : %22"GFARM_PRId64"\n", s, v);	\
		else							\
			fprintf(f, "%s : %22s\n", s, "disabled");	\
	}

static void
quota_get_info_print(FILE *f, struct gfarm_quota_get_info *q, int is_group)
{
	if (!is_group)
		fprintf(f, "UserName                 : ");
	else
		fprintf(f, "GroupName                : ");
	fprintf(f, "%22s\n", q->name);
	PRINT(f, "GracePeriod             ", q->grace_period);
	PRINT(f, "FileSpace               ", q->space);
	PRINT(f, "FileSpaceGracePeriod    ", q->space_grace);
	PRINT(f, "FileSpaceSoftLimit      ", q->space_soft);
	PRINT(f, "FileSpaceHardLimit      ", q->space_hard);
	PRINT(f, "FileNum                 ", q->num);
	PRINT(f, "FileNumGracePeriod      ", q->num_grace);
	PRINT(f, "FileNumSoftLimit        ", q->num_soft);
	PRINT(f, "FileNumHardLimit        ", q->num_hard);
	PRINT(f, "PhysicalSpace           ", q->phy_space);
	PRINT(f, "PhysicalSpaceGracePeriod", q->phy_space_grace);
	PRINT(f, "PhysicalSpaceSoftLimit  ", q->phy_space_soft);
	PRINT(f, "PhysicalSpaceHardLimit  ", q->phy_space_hard);
	PRINT(f, "PhysicalNum             ", q->phy_num);
	PRINT(f, "PhysicalNumGracePeriod  ", q->phy_num_grace);
	PRINT(f, "PhysicalNumSoftLimit    ", q->phy_num_soft);
	PRINT(f, "PhysicalNumHardLimit    ", q->phy_num_hard);
}

#define MODE_USER  0
#define MODE_GROUP 1

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	int mode = MODE_USER;
	char *name = "";  /* default: my username */
	struct gfarm_quota_get_info qi;
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

	while ((c = getopt(argc, argv, "P:u:g:h?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case 'u':
			name = optarg;
			mode = MODE_USER;
			break;
		case 'g':
			name = optarg;
			mode = MODE_GROUP;
			break;
		case 'h':
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

	switch (mode) {
	case MODE_USER:
		e = gfm_client_quota_user_get(
			gfm_server, name, &qi);
		break;
	case MODE_GROUP:
		e = gfm_client_quota_group_get(
			gfm_server, name, &qi);
		break;
	default:
		usage();
	}

	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		if (strcmp(name, "") == 0)
			fprintf(stderr, "Your");
		else
			fprintf(stderr, "%s's", name);
		fprintf(stderr, " quota is not enabled.\n");
		fprintf(stderr, "gfarmadm need to execute "
			"gfedquota and gfquotacheck for ");
		if (strcmp(name, "") == 0)
			fprintf(stderr, "you.\n");
		else
			fprintf(stderr, "%s.\n", name);
		status = -2;
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = -3;
	} else {
		status = quota_check_and_warning(stderr, &qi);
		if (mode == MODE_GROUP)
			quota_get_info_print(stdout, &qi, 1);
		else
			quota_get_info_print(stdout, &qi, 0);
		gfarm_quota_get_info_free(&qi);
	}
	gfm_client_connection_free(gfm_server);
terminate:
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = -4;
	}
	return (status);
}

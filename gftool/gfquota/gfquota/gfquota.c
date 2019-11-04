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
#include "gfarm_path.h"

char *program_name = "gfquota";
static int opt_format_flags = 0;
static int opt_humanize_number = 0;

static void
usage(void)
{
	fprintf(stderr,
		"Usage:\t%s [-qHh] [-P <path>] [-u username | -g groupname]\n",
		program_name);
	exit(EXIT_FAILURE);
}

#define EXCEEDED(f, type) fprintf(f, "warning: %s exceeded\n", type)
#define EXPIRED(f, type) fprintf(f, "warning: %s expired\n", type)

static char *
humanize(long long num)
{
	static char buf[GFARM_INT64STRLEN];

	gfarm_humanize_number(buf, sizeof buf, num, opt_format_flags);
	return (buf);
}

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
		fprintf(f, "%s : ", s);					\
		if (quota_limit_is_valid(v))				\
			if (opt_humanize_number)			\
				fprintf(f, "%22s\n", humanize(v));	\
			else						\
				fprintf(f, "%22"GFARM_PRId64"\n", v);	\
		else							\
			fprintf(f, "%22s\n", "disabled");		\
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

#define OPT_USER	'u'
#define OPT_GROUP	'g'

static void
conflict_check(int *mode_ch_p, int ch)
{
	if (*mode_ch_p) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
			program_name, ch, *mode_ch_p);
		usage();
	}
	*mode_ch_p = ch;
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0, opt_quiet = 0;
	int mode = 0;
	char *name = "";  /* default: my username */
	struct gfarm_quota_get_info qi;
	struct gfm_connection *gfm_server;
	const char *path = ".";
	char *realpath = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "P:g:Hhqu:?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case OPT_USER: /* 'u' */
			name = optarg;
			conflict_check(&mode, c);
			break;
		case OPT_GROUP: /* 'g' */
			name = optarg;
			conflict_check(&mode, c);
			break;
		case 'q':
			opt_quiet = 1;
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

	/* default mode is user */
	if (mode == 0)
		mode = OPT_USER;

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	if ((e = gfm_client_connection_and_process_acquire_by_path(
		     path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			program_name, path, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	free(realpath);

	switch (mode) {
	case OPT_USER:
		e = gfm_client_quota_user_get(gfm_server, name, &qi);
		break;
	case OPT_GROUP:
		e = gfm_client_quota_group_get(gfm_server, name, &qi);
		break;
	default:
		usage();
	}

	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* quota is not enabled */
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	} else {
		status = quota_check_and_warning(stderr, &qi);
		if (!opt_quiet)
			quota_get_info_print(stdout, &qi, mode == OPT_GROUP);
		gfarm_quota_get_info_free(&qi);
	}
	gfm_client_connection_free(gfm_server);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (status);
}

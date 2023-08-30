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
#include "quota_info.h"
#include "gfm_proto.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_dirquota.h"
#include "gfarm_path.h"

const char *program_name = "gfquota";
static int opt_format_flags = 0;
static int opt_humanize_number = 0;

static void
usage(void)
{
	fprintf(stderr, "Usage:"
	    "\t%s [-qHh] [-P <path>] -u <username>\n"
	    "\t%s [-qHh] [-P <path>] -g <groupname>\n"
	    "\t%s [-qHh] [-P <path>] -D <dirset_name> [-u <username>]\n"
	    "\t%s [-qHh] -d <pathname>\n",
	    program_name, program_name, program_name, program_name);
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
quota_get_info_print(FILE *f, const char *nametype,
	struct gfarm_quota_get_info *q)
{
	fprintf(f, "%-25s: %22s\n", nametype, q->name);
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

static void
gfarm_quota_get_info_from_dirquota(
	struct gfarm_quota_get_info *qi,
	char *name,
	struct gfarm_quota_limit_info *limit_info,
	struct gfarm_quota_subject_info *usage_info,
	struct gfarm_quota_subject_time *grace_info)
{
	qi->name = name;
	qi->grace_period = limit_info->grace_period;
	qi->space = usage_info->space;
	qi->space_grace = grace_info->space_time;
	qi->space_soft = limit_info->soft.space;
	qi->space_hard = limit_info->hard.space;
	qi->num = usage_info->num;
	qi->num_grace = grace_info->num_time;
	qi->num_soft = limit_info->soft.num;
	qi->num_hard = limit_info->hard.num;
	qi->phy_space = usage_info->phy_space;
	qi->phy_space_grace = grace_info->phy_space_time;
	qi->phy_space_soft = limit_info->soft.phy_space;
	qi->phy_space_hard = limit_info->hard.phy_space;
	qi->phy_num = usage_info->phy_num;
	qi->phy_num_grace = grace_info->phy_num_time;
	qi->phy_num_soft = limit_info->soft.phy_num;
	qi->phy_num_hard = limit_info->hard.phy_num;
}

#define OPT_USER	'u'
#define OPT_GROUP	'g'
#define OPT_DIRSET	'D'
#define OPT_DIR		'd'

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
	const char *name = "";  /* default: my username */
	char *nametype, *dirsetname = NULL;
	struct gfarm_quota_get_info qi;
	gfarm_uint64_t flags = 0;

	/* for dirquota */
	struct gfarm_dirset_info di;
	struct gfarm_quota_limit_info limit_info;
	struct gfarm_quota_subject_info usage_info;
	struct gfarm_quota_subject_time grace_info;

	struct gfm_connection *gfm_server = NULL;
	const char *path = NULL;
	char *realpath = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "P:D:d:g:Hhqu:?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case OPT_DIRSET: /* 'D' */
			/* `name' may be used for -u <username> */
			dirsetname = optarg;
			break;
		case OPT_DIR: /* 'd' */
			name = optarg;
			conflict_check(&mode, c);
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

	if (path != NULL && mode == OPT_DIR) {
		fprintf(stderr, "%s: -P option conflicts with -%d\n",
		    program_name, mode);
		usage();
	}
	if (dirsetname != NULL) {
		if (mode == OPT_GROUP || mode == OPT_DIR) {
			fprintf(stderr, "%s: -D option conflicts with -%c\n",
				program_name, mode);
			usage();
		}
		mode = OPT_DIRSET;
	}
	/* default mode is user */
	if (mode == 0)
		mode = OPT_USER;

	if (mode == OPT_DIR) {
		if (gfarm_realpath_by_gfarm2fs(name, &realpath)
		    == GFARM_ERR_NO_ERROR)
			name = realpath;
	} else {
		if (path == NULL)
			path = ".";
		if (gfarm_realpath_by_gfarm2fs(path, &realpath)
		    == GFARM_ERR_NO_ERROR)
			path = realpath;
		if ((e = gfm_client_connection_and_process_acquire_by_path(
			     path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
				program_name, path, gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
		free(realpath);
		realpath = NULL;
	}

	switch (mode) {
	case OPT_DIRSET:
		if (*name == '\0') {
			e = gfm_client_get_username_in_tenant(gfm_server,
			    &name);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "%s: failed to get self user name: %s\n",
				    program_name, gfarm_error_string(e));
				exit(EXIT_FAILURE);
			}
		}
		e = gfm_client_quota_dirset_get(gfm_server,
		    name, dirsetname, &limit_info, &usage_info, &grace_info,
		    &flags);
		if (e == GFARM_ERR_NO_ERROR)
			gfarm_quota_get_info_from_dirquota(
			    &qi, dirsetname,
			    &limit_info, &usage_info, &grace_info);
		nametype = "DirsetName";
		break;
	case OPT_DIR:
		e = gfs_dirquota_get(name, &di,
		    &limit_info, &usage_info, &grace_info, &flags);
		if (e == GFARM_ERR_NO_ERROR)
			gfarm_quota_get_info_from_dirquota(
			    &qi, di.dirsetname,
			    &limit_info, &usage_info, &grace_info);
		nametype = "DirsetName";
		break;
	case OPT_USER:
		e = gfm_client_quota_user_get(gfm_server, name, &qi);
		nametype = "UserName";
		break;
	case OPT_GROUP:
		e = gfm_client_quota_group_get(gfm_server, name, &qi);
		nametype = "GroupName";
		break;
	default:
#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
		nametype = NULL;
#endif
		usage();
	}

	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* quota is not enabled */
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	} else {
		if ((flags & GFM_PROTO_QUOTA_INACCURATE) != 0)
			fprintf(stderr,
			    "warning: quota usage is inaccurate\n");
		status = quota_check_and_warning(stderr, &qi);
		if (!opt_quiet) {
			switch (mode) {
			case OPT_DIRSET:
				fprintf(stdout, "%-25s: %22s\n",
				    "UserName", name);
				break;
			case OPT_DIR:
				fprintf(stdout, "%-25s: %22s\n",
				    "Pathanme", name);
				fprintf(stdout, "%-25s: %22s\n",
				    "UserName", di.username);
				break;
			}
			quota_get_info_print(stdout, nametype, &qi);
		}
		switch (mode) {
		case OPT_DIRSET:
			/* no need to free */
			break;
		case OPT_DIR:
			gfarm_dirset_info_free(&di);
			break;
		case OPT_USER:
			/*FALLTHROUGH*/
		case OPT_GROUP:
			gfarm_quota_get_info_free(&qi);
			break;
		}
	}
	free(realpath);
	if (gfm_server != NULL)
		gfm_client_connection_free(gfm_server);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (status);
}

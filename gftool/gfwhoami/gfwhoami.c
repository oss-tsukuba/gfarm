/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <gfarm/gfarm.h>
#ifdef HAVE_GSI
#include "auth.h"
#endif
#include "gfarm_path.h"

char *program_name = "gfwhoami";

#ifdef HAVE_KERBEROS
#define GFWHOAMI_KERBEROS_OPTION	"k"
#define GFWHOAMI_KERBEROS_GETOPT_ARG	"k"
#else
#define GFWHOAMI_KERBEROS_OPTION	""
#define GFWHOAMI_KERBEROS_GETOPT_ARG	""
#endif
#ifdef HAVE_GSI
#define GFWHOAMI_GSI_OPTION	"v"
#define GFWHOAMI_GSI_GETOPT_ARG	"v"
#else
#define GFWHOAMI_GSI_OPTION	""
#define GFWHOAMI_GSI_GETOPT_ARG	""
#endif


#define GFWHOAMI_NO_ARG_OPTIONS	"fh" \
				GFWHOAMI_KERBEROS_OPTION \
				GFWHOAMI_GSI_OPTION
#define GFWHOAMI_GETOPT_ARG	"P:fh" \
				GFWHOAMI_KERBEROS_GETOPT_ARG \
				GFWHOAMI_GSI_GETOPT_ARG \
				"?"

void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s [-" GFWHOAMI_NO_ARG_OPTIONS "] [-P <path>]\n",
	    program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c;
	const char *path = ".";
	char *user, *realpath = NULL;
#ifdef HAVE_KERBEROS
	int kerberos_flag = 0;
#endif
#ifdef HAVE_GSI
	int verbose_flag = 0;
#endif
	int fullname_flag = 0;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n", program_name,
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, GFWHOAMI_GETOPT_ARG)) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case 'f':
			fullname_flag = 1;
			break;
#ifdef HAVE_KERBEROS
		case 'k':
			kerberos_flag = 1;
			break;
#endif
#ifdef HAVE_GSI
		case 'v':
			verbose_flag = 1;
			break;
#endif
		case 'h':
		case '?':
		default:
			usage();
		}
	}

	if (argc - optind > 0)
		usage();

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	if ((e = (fullname_flag ?
	    gfarm_get_global_username_by_url(path, &user) :
	    gfarm_get_global_username_in_tenant_by_url(path, &user)))
	    != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_get_global_username_by_url: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	free(realpath);
	printf("%s", user);
	free(user);
#ifdef HAVE_GSI
	if (verbose_flag)
		printf(" %s", gfarm_gsi_client_cred_name());
#endif
	printf("\n");
#ifdef HAVE_KERBEROS
	if (kerberos_flag)
		printf("%s\n", gfarm_kerberos_client_cred_name());
#endif

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n", program_name,
			gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (0);
}

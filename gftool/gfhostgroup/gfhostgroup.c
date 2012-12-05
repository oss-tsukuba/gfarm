/*
 * $Id$
 */

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <assert.h>

#include <gfarm/gfarm.h>
#include "fsngroup_info.h"
#include "gfm_client.h"
#include "lookup.h"
#include "config.h"

#define OP_LIST		'\0'
#define OP_SET		's'
#define OP_REMOVE	'r'

#define is_string_valid(s)	((s) != NULL && *s != '\0')

static char program_name[PATH_MAX];

static struct gfm_connection *gfm_server;

static void
set_myname(const char *argv0)
{
	const char *p = (const char *)strrchr(argv0, '/');
	if (p != NULL)
		p++;
	else
		p = argv0;
	(void)snprintf(program_name, PATH_MAX, "%s", p);
}

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-P <path>] [hostname]\n",
		program_name);
	fprintf(stderr, "\t%s [-P <path>] -s hostname fsngroupname\n",
		program_name);
	fprintf(stderr, "\t%s [-P <path>] -r hostname [hostname ...]\n",
		program_name);

	exit(1);
}

static gfarm_error_t
modify_fsngroup(int op, const char *hostname, const char *fsngroupname)
{
	struct gfarm_fsngroup_info fsng;
	gfarm_error_t e = GFARM_ERR_UNKNOWN;

	fsng.hostname = (char *)hostname;
	if (op == OP_REMOVE) {
		if (is_string_valid(fsngroupname)) {
			e = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		} else {
			fsng.fsngroupname = "";
		}
	} else {
		if (is_string_valid(fsngroupname)) {
			fsng.fsngroupname = (char *)fsngroupname;
		} else {
			e = GFARM_ERR_INVALID_ARGUMENT;
			goto bailout;
		}
	}

	e = gfm_client_fsngroup_modify(gfm_server, &fsng);
	if (e != GFARM_ERR_NO_ERROR) {
		switch (e) {
		case GFARM_ERR_NO_SUCH_OBJECT:
			fprintf(stderr,
				"%s: host '%s' seems not existing: "
				"%s\n",
				program_name, hostname,
				gfarm_error_string(e));
			break;
		default:
			fprintf(stderr,	"%s: %s\n", program_name,
				gfarm_error_string(e));
			break;
		}
	}

bailout:
	return (e);
}

/*
 * !is_string_valid(hostname) : display all.
 */
static gfarm_error_t
display_fsngroup(const char *hostname)
{
	gfarm_error_t e = GFARM_ERR_UNKNOWN;

	if (is_string_valid(hostname)) {
		char *fsngroupname = NULL;

		e = gfm_client_fsngroup_get_by_hostname(
			gfm_server, hostname, &fsngroupname);
		if (e == GFARM_ERR_NO_ERROR) {
			fprintf(stdout, "%s: %s\n", hostname, fsngroupname);
			(void)fflush(stdout);
		} else {
			switch (e) {
			case GFARM_ERR_NO_SUCH_OBJECT:
				fprintf(stderr,
					"%s: host '%s' seems not existing: "
					"%s\n",
					program_name, hostname,
					gfarm_error_string(e));
				break;
			default:
				fprintf(stderr,	"%s: %s\n", program_name,
					gfarm_error_string(e));
				break;
			}
		}

		if (fsngroupname != NULL)
			free((void *)fsngroupname);
	} else {
		struct gfarm_fsngroup_info *fsngs = NULL;
		size_t n = 0;
		size_t i;

		e = gfm_client_fsngroup_get_all(gfm_server, &n, &fsngs);
		if (e == GFARM_ERR_NO_ERROR) {
			for (i = 0; i < n; i++) {
				fprintf(stdout, "%s: %s\n",
					fsngs[i].hostname,
					fsngs[i].fsngroupname);
			}
			(void)fflush(stdout);
		} else {
			fprintf(stderr, "%s: %s\n", program_name,
				gfarm_error_string(e));
		}

		if (n > 0 && fsngs != NULL) {
			for (i = 0; i < n; i++) {
				free((void *)fsngs[i].hostname);
				free((void *)fsngs[i].fsngroupname);
			}
			free((void *)fsngs);
		}
	}

	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	int op = OP_LIST;
	int c;
	int i;
	const char *path = GFARM_PATH_ROOT;

	set_myname(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "P:sr?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case OP_SET:
		case OP_REMOVE:
			op = c;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			program_name, path, gfarm_error_string(e));
		return (1);
	}

	switch (op) {
	case OP_LIST:
		if (argc > 0) {
			for (i = 0; i < argc; i++) {
				(void)display_fsngroup(argv[i]);
			}
		} else {
			(void)display_fsngroup(NULL);
		}
		break;

	case OP_SET:
		if (argc < 2) {
			fprintf(stderr, "%s: too few arguments.\n",
				program_name);
		} else {
			(void)modify_fsngroup(OP_SET, argv[0], argv[1]);
		}
		break;
	case OP_REMOVE:
		for (i = 0; i < argc; i++) {
			(void)modify_fsngroup(OP_REMOVE, argv[i], NULL);
		}
		break;
	default:
		break;
	}

	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n",
			program_name, gfarm_error_string(e));
		return (1);
	}

	return (0);
}

/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

/*
 * $Id$
 */

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <strings.h>
#include <string.h>
#include <errno.h>

#include <gfarm/gfarm.h>
#include "gfutil.h"

#define DEFAULT_ALLOC_SIZE (64 * 1024)

static const char *xattrname = GFARM_REPLICAINFO_XATTR_NAME;
static char program_name[PATH_MAX];

/*****************************************************************************/
static gfarm_error_t
set_replicainfo(char *path, void *value, size_t len, int flags, int nofollow)
{
	return (nofollow == 1 ? gfs_lsetxattr : gfs_setxattr)
		(path, xattrname, value, len, flags);
}

static gfarm_error_t
get_replicainfo_alloc(char *path, void **valuep, size_t *size, int nofollow)
{
	gfarm_error_t e;
	void *value;

	value = malloc(*size);
	if (value == NULL)
		return GFARM_ERR_NO_ERROR;

	e = (nofollow ? gfs_lgetxattr : gfs_getxattr)
		(path, xattrname, value, size);

	if (e == GFARM_ERR_NO_ERROR)
		*valuep = value;
	else
		free(value);
	return (e);
}

static gfarm_error_t
get_replicainfo(char *path, void **valuep, size_t *lenp, int nofollow)
{
	gfarm_error_t e;
	void *value = NULL;
	size_t size;

	size = DEFAULT_ALLOC_SIZE;
	e = get_replicainfo_alloc(path, &value, &size, nofollow);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = get_replicainfo_alloc(path, &value,
				    &size, nofollow);
	}
	if (valuep != NULL)
		*valuep = value;
	else
		free(value);
	if (lenp != NULL)
		*lenp = size;

	return (e);
}

static gfarm_error_t
remove_replicainfo(char *path, int nofollow)
{
	gfarm_error_t e;

	e = (nofollow ? gfs_lremovexattr : gfs_removexattr)
		(path, xattrname);
	return (e);
}

/*****************************************************************************/
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
	fprintf(stderr, "Usage:\t%s [-s|-r] [-c|-m] [-h] ARG [ARG ...]\n",
		program_name);

	fprintf(stderr, "\t%s -s [-c|-m] [-h] path spec\n", program_name);
	fprintf(stderr, "\t%s -r [-c|-m] [-h] path [path ...]\n",
		program_name);
	fprintf(stderr, "\t%s [-h] path [path ...]\n\n", program_name);

	fprintf(stderr, "\t\t-s\tset a replicaton spec.\n");
	fprintf(stderr, "\t\t-r\tremove any replicaton specs.\n");
	fprintf(stderr, "\t\t-c\tfail if a spec already specified "
		"(use with -s).\n");
	fprintf(stderr, "\t\t-m\tfail if a spec dosen't specified yet "
		"(use with -s).\n");
	fprintf(stderr, "\t\t-h\tprocess symbolic link instead of "
		"any referenced files.\n\n");

	fprintf(stderr, "spec BNF:\n");
	fprintf(stderr, "\tspec := aspec | aspec ',' spec\n");
	fprintf(stderr, "\taspec := string ':' integer\n");

	fprintf(stderr, "e.g.)\n");
	fprintf(stderr, "\t'group0:2'\n");
	fprintf(stderr, "\t'group0:1,group1:3'\n");
}

int
main(int argc, char *argv[])
{
	enum {
		NONE, SET_MODE, GET_MODE, REMOVE_MODE
	} mode = NONE;
	const char *opts = "srcmh?";
	char *c_path = NULL;
	char *replicainfo = NULL;
	size_t infolen = 0;
	int c;
	int i;
	int nofollow = 0;
	int flags = 0;
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	int got_errors = 0;
	int inited = 0;
	gfarm_replicainfo_t *reps = NULL;
	size_t nreps = 0;

	set_myname(argv[0]);

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 's':
			mode = SET_MODE;
			break;
		case 'r':
			mode = REMOVE_MODE;
			break;
		case 'c':
			if (flags == 0) {
				flags = GFS_XATTR_CREATE;
			} else {
				usage();
				got_errors++;
				goto done;
			}
			break;
		case 'm':
			if (flags == 0) {
				flags = GFS_XATTR_REPLACE;
			} else {
				usage();
				got_errors++;
				goto done;
			}
			break;
		case 'h':
			nofollow = 1;
			break;
		case '?':
		default:
			usage();
			goto done;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage();
		got_errors++;
		goto done;
	} else {
		if (mode == NONE) {
			mode = GET_MODE;
		}
	}

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
			program_name, gfarm_error_string(e));
		got_errors++;
		goto done;
	}
	inited = 1;

	switch (mode) {
	case SET_MODE:
		if (argc != 2) {
			usage();
			got_errors++;
			goto done;
		}
		c_path = argv[0];
		nreps = gfarm_replicainfo_parse(argv[1], &reps);
		if (nreps == 0) {
			fprintf(stderr, "%s: invalid spec '%s'\n",
				program_name, argv[1]);
			got_errors++;
			goto done;
		}
		replicainfo = gfarm_replicainfo_stringify(reps, nreps);
		if (replicainfo == NULL) {
			fprintf(stderr, "%s: canonicalization failure '%s'\n",
				program_name, argv[1]);
			got_errors++;
			goto done;
		}
		infolen = strlen(replicainfo);
		e = set_replicainfo(c_path, replicainfo, infolen, flags,
			nofollow);
		switch (e) {
		case GFARM_ERR_NO_ERROR:
			break;
		case GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY:
			fprintf(stderr, "%s: %s: %s\n",
				program_name, c_path, gfarm_error_string(e));
			got_errors++;
			break;
		default:
			fprintf(stderr, "%s: %s\n",
				program_name,
				gfarm_error_string(e));
			got_errors++;
			break;
		}
		if (replicainfo != NULL)
			free((void *)replicainfo);
		break;
	case GET_MODE:
		for (i = 0; i < argc; i++) {
			replicainfo = NULL;
			infolen = 0;
			c_path = argv[i];
			e = get_replicainfo(c_path,
				(void **)&replicainfo, &infolen, nofollow);
			switch (e) {
			case GFARM_ERR_NO_ERROR:
			case GFARM_ERR_NO_SUCH_OBJECT:
				fprintf(stdout, "%s: '%s'\n",
					c_path,
					replicainfo != NULL ?
					replicainfo : "");
				break;
			case GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY:
				fprintf(stderr, "%s: %s: %s\n",
					program_name, c_path,
					gfarm_error_string(e));
				got_errors++;
				break;
			default:
				fprintf(stderr, "%s: %s\n",
					program_name,
					gfarm_error_string(e));
				got_errors++;
				break;
			}
			if (replicainfo != NULL)
				free((void *)replicainfo);
		}
		(void)fflush(stdout);
		break;
	case REMOVE_MODE:
		for (i = 0; i < argc; i++) {
			c_path = argv[i];
			e = remove_replicainfo(c_path, nofollow);
			switch (e) {
			case GFARM_ERR_NO_ERROR:
			case GFARM_ERR_NO_SUCH_OBJECT:
				break;
			default:
				fprintf(stderr, "%s: %s\n",
					program_name,
					gfarm_error_string(e));
				break;
			}
		}
		break;
	default:
		usage();
		got_errors++;
		break;
	}

done:
	if (nreps > 0 && reps != NULL)
		for (i = 0; i < nreps; i++)
			gfarm_replicainfo_free(reps[i]);
	if (inited == 1) {
		e = gfarm_terminate();
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n",
				program_name, gfarm_error_string(e));
			got_errors++;
		}
	}

	return (got_errors == 0 ? 0 : 1);
}

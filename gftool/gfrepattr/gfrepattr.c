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
#include "repattr.h"
#include "gfutil.h"

#define DEFAULT_ALLOC_SIZE (64 * 1024)

static const char *repattrname = GFARM_REPATTR_NAME;
static char program_name[PATH_MAX];

/*****************************************************************************/
static gfarm_error_t
set_repattr(char *path, const void *value, size_t len,
	int flags, int nofollow)
{
	return (nofollow == 1 ? gfs_lsetxattr : gfs_setxattr)
		(path, repattrname, value, len, flags);
}

static gfarm_error_t
get_repattr_alloc(char *path, char **valuep, size_t *size, int nofollow)
{
	gfarm_error_t e;
	char *value;

	value = malloc(*size);
	if (value == NULL)
		return GFARM_ERR_NO_ERROR;

	e = (nofollow ? gfs_lgetxattr : gfs_getxattr)
		(path, repattrname, value, size);

	if (e == GFARM_ERR_NO_ERROR)
		*valuep = value;
	else
		free(value);
	return (e);
}

static gfarm_error_t
get_repattr(char *path, char **valuep, size_t *lenp, int nofollow)
{
	gfarm_error_t e;
	char *value = NULL;
	size_t size;

	size = DEFAULT_ALLOC_SIZE;
	e = get_repattr_alloc(path, &value, &size, nofollow);
	if (e == GFARM_ERR_RESULT_OUT_OF_RANGE) {
		e = get_repattr_alloc(path, &value,
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
remove_repattr(char *path, int nofollow)
{
	gfarm_error_t e;

	e = (nofollow ? gfs_lremovexattr : gfs_removexattr)
		(path, repattrname);
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

	fprintf(stderr, "\t%s -s [-c|-m] [-h] path attr\n", program_name);
	fprintf(stderr, "\t%s -r [-c|-m] [-h] path [path ...]\n",
		program_name);
	fprintf(stderr, "\t%s [-h] path [path ...]\n\n", program_name);

	fprintf(stderr, "\t\t-s\tset a replicaton attr.\n");
	fprintf(stderr, "\t\t-r\tremove any replicaton attr(s).\n");
	fprintf(stderr, "\t\t-c\tfail if an attr already specified "
		"(use with -s).\n");
	fprintf(stderr, "\t\t-m\tfail if an attr isn't specified yet "
		"(use with -s).\n");
	fprintf(stderr, "\t\t-h\tprocess symbolic link instead of "
		"any referenced files.\n\n");

	fprintf(stderr, "attr BNF:\n");
	fprintf(stderr, "\tattr := anattr | anattr ',' attr\n");
	fprintf(stderr, "\tanattr := string ':' integer\n");

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
	char *repattr = NULL;
	size_t infolen = 0;
	int c;
	int i;
	int nofollow = 0;
	int flags = 0;
	gfarm_error_t e = GFARM_ERR_UNKNOWN;
	int got_errors = 0;
	int inited = 0;
	gfarm_repattr_t *reps = NULL;
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
		if ((e = gfarm_repattr_reduce(argv[1], &reps, &nreps))
		    != GFARM_ERR_NO_ERROR) {
			got_errors++;
			goto done;
		}
		if (nreps == 0) {
			fprintf(stderr, "%s: invalid attr '%s'\n",
				program_name, argv[1]);
			got_errors++;
			goto done;
		}
		e = gfarm_repattr_stringify(reps, nreps, &repattr);
		if (e != GFARM_ERR_NO_ERROR) {
			if (repattr == NULL) {
				fprintf(stderr, "%s: canonicalization failure '%s'\n",
					program_name, argv[1]);
			}
			got_errors++;
			goto done;
		}

		/* Add one for the last NUL. */
		infolen = strlen(repattr) + 1;
		e = set_repattr(c_path, repattr, infolen, flags,
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
		free(repattr);
		break;
	case GET_MODE:
		for (i = 0; i < argc; i++) {
			repattr = NULL;
			infolen = 0;
			c_path = argv[i];
			e = get_repattr(c_path,
				&repattr, &infolen, nofollow);
			switch (e) {
			case GFARM_ERR_NO_ERROR:
			case GFARM_ERR_NO_SUCH_OBJECT:
				fprintf(stdout, "%s: '%s'\n",
					c_path,
					repattr != NULL ?
					repattr : "");
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
			free(repattr);
		}
		(void)fflush(stdout);
		break;
	case REMOVE_MODE:
		for (i = 0; i < argc; i++) {
			c_path = argv[i];
			e = remove_repattr(c_path, nofollow);
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
	if (nreps > 0 && reps != NULL) {
		for (i = 0; i < nreps; i++)
			gfarm_repattr_free(reps[i]);
		free(reps);
	}
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

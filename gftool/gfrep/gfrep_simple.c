/*
 * $Id$
 */

#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>

#include <gfarm/gfarm.h>
#include "gfarm_path.h"
#include "gfarm_foreach.h"

char *program_name = "gfrep_simple";

static int
usage()
{
	fprintf(stderr, "Usage: %s [-r] [-s srchost] -d dsthost file\n",
		program_name);
	exit(EXIT_FAILURE);
}

static gfarm_error_t
select_src_from_replica_info(char *file, char **srcp)
{
	struct gfs_replica_info *ri;
	int flags = 0;
	const char *s;
	gfarm_error_t e;

	e = gfs_replica_info_by_name(file, flags, &ri);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	s = gfs_replica_info_nth_host(ri, 0);
	if (srcp != NULL && s != NULL)
		*srcp = strdup(s);
	gfs_replica_info_free(ri);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
replicate_file(char *f, char *src, char *dst, int flags)
{
	gfarm_error_t e, e2;

	if (src != NULL)
		return (gfs_replicate_file_from_to(f, src, dst, flags));

	e = gfs_replicate_file_to(f, dst, flags);
	if (e == GFARM_ERR_NO_ERROR)
		return (e);
	/* if src host scheduling fails, select it from replica info */
	e2 = select_src_from_replica_info(f, &src);
	if (e2 == GFARM_ERR_NO_ERROR && src != NULL) {
		e = gfs_replicate_file_from_to(f, src, dst, flags);
		free(src);
	}
	return (e);
}

struct options {
	char *src, *dst;
	int flags;
};

static gfarm_error_t
replicate(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	struct options *opt = arg;
	gfarm_mode_t mode;

	mode = st->st_mode;
	if (GFARM_S_ISLNK(mode))
		e = GFARM_ERR_IS_A_SYMBOLIC_LINK;
	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_NOT_A_REGULAR_FILE;
	else
		e = replicate_file(path, opt->src, opt->dst, opt->flags);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", path, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
ok(char *path, struct gfs_stat *st, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
ng(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e = GFARM_ERR_IS_A_DIRECTORY;

	fprintf(stderr, "%s: %s\n", path, gfarm_error_string(e));
	return (e);
}

int
main(int argc, char *argv[])
{
	char *src = NULL, *dst = NULL, *f, *rp = NULL, c;
	int flags = 0;
	struct options opt;
	gfarm_error_t (*isdir)(char *, struct gfs_stat *, void *) = ng;
	gfarm_error_t e;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	while ((c = getopt(argc, argv, "d:s:hr?")) != -1) {
		switch (c) {
		case 'd':
			dst = optarg;
			break;
		case 's':
			src = optarg;
			break;
		case 'r':
			isdir = ok;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	f = *argv;
	if (dst == NULL || f == NULL)
		usage();

	e = gfarm_realpath_by_gfarm2fs(f, &rp);
	if (e == GFARM_ERR_NO_ERROR)
		f = rp;
	opt.src = src;
	opt.dst = dst;
	opt.flags = flags;
	e = gfarm_foreach_directory_hierarchy(replicate, isdir, NULL, f, &opt);
	(void)e; /* error message already displayed */
	free(rp);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (0);
}

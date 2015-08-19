/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#include <gfarm/gfarm.h>
#include "gfarm_path.h"

static char *program_name = "gfmv";

static int opt_force = 0;
static int opt_interactive = 0;
static int is_terminal;

/* XXX should be gfs_access(path, GFS_F_OK) */
static int
does_exist(const char *path, gfarm_error_t *ep)
{
	gfarm_error_t e;
	struct gfs_stat gst;

	e = gfs_stat(path, &gst);
	if (e == GFARM_ERR_NO_ERROR)
		gfs_stat_free(&gst);
	if (ep != NULL)
		*ep = e;
	return (e == GFARM_ERR_NO_ERROR);
}

/* XXX should be gfs_access(path, GFS_W_OK) */
static int
is_writable(const char *path)
{
	gfarm_error_t e;
	GFS_File gf;

	e = gfs_pio_open(path, GFARM_FILE_WRONLY, &gf);
	if (e == GFARM_ERR_NO_ERROR) {
		gfs_pio_close(gf);
		return (1);
	}
	return (0);
}

static int
is_answer_yes(void)
{
	int c1, c2;

	c1 = getchar();
	if (c1 == EOF)
		return (0);
	if (c1 != '\n') {
		while ((c2 = getchar()) != EOF && c2 != '\n')
			;
	}
	return (c1 == 'y' || c1 == 'Y');
	
}

static int
move_file(const char *srcarg, const char *dst)
{
	gfarm_error_t e;
	const char *src;
	char *stmp = NULL;
	int status = EXIT_SUCCESS, do_ask = 0, do_rename = 1;

	e = gfarm_realpath_by_gfarm2fs(srcarg, &stmp);
	if (e == GFARM_ERR_NO_ERROR)
		src = stmp;
	else
		src = srcarg;

	if (!opt_force && does_exist(dst, NULL)) {
		if (opt_interactive) {
			if (does_exist(src, &e)) {
				do_ask = 1;
			} else {
				fprintf(stderr, "%s: %s: %s\n", program_name,
				    src, gfarm_error_string(e));
				do_rename = 0;
				status = EXIT_FAILURE;
			}
		} else if (is_terminal &&
		    !is_writable(dst) &&
		    /* the following is to avoid race condition */
		    does_exist(dst, NULL)) {
			do_ask = 1;
		}
		if (do_ask) {
			fprintf(stderr, "overwrite %s? ", dst);
			if (!is_answer_yes())
				do_rename = 0; /* status is SUCCESS */
		}
	}

	if (do_rename) {
		e = gfs_rename(src, dst);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s %s %s: %s\n",
			    program_name, src, dst, gfarm_error_string(e));
			status = EXIT_FAILURE;
		}
	}

	free(stmp);
	return (status);
}

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-fi] source target-file\n", program_name);
	fprintf(stderr, "\t%s [-fi] source... target-dir\n", program_name);
	exit(2);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, j, n, c, status = EXIT_SUCCESS, dst_is_not_dir = 0;
	char *path, *src, *dst, *dtmp = NULL;
	struct gfs_stat gst;
	size_t srclen, dstlen, srcbase, srctail, pathlen;
	gfarm_stringlist paths;
	gfs_glob_t types;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "fi?")) != -1) {
		switch (c) {
		case 'f':
			opt_force = 1;
			break;
		case 'i':
			opt_interactive = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 2)
		usage();

	is_terminal = isatty(STDIN_FILENO);

	e = gfarm_realpath_by_gfarm2fs(argv[argc - 1], &dtmp);
	if (e == GFARM_ERR_NO_ERROR)
		dst = dtmp;
	else
		dst = argv[argc - 1];
	--argc; /* remove dst from argv[] */

	if ((e = gfarm_stringlist_init(&paths)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if ((e = gfs_glob_init(&types)) != GFARM_ERR_NO_ERROR) {
		gfarm_stringlist_free_deeply(&paths);
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	n = gfarm_stringlist_length(&paths);

	e = gfs_stat(dst, &gst);
	if (e == GFARM_ERR_NO_ERROR) {
		if (!GFARM_S_ISDIR(gst.st_mode))
			dst_is_not_dir = 1;
		gfs_stat_free(&gst);
	}
	if (e == GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY ||
	    (e == GFARM_ERR_NO_ERROR && dst_is_not_dir)) {
		if (n != 1)
			usage();
		/* file-to-file case */
		status = move_file(gfarm_stringlist_elem(&paths, 0), dst);
	} else if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, dst, gfarm_error_string(e));
		status = EXIT_FAILURE;
	} else { /* file-to-dir case */
		dstlen = strlen(dst);
		for (i = 0; i < n; i++) {
			src = gfarm_stringlist_elem(&paths, i);
			srclen = strlen(src);
			srctail = srclen;
			while (srctail > 0 && src[srctail - 1] == '/')
				--srctail;
			srcbase = 0;
			for (j = 0; j < srctail; j++) {
				if (src[j] == '/')
					srcbase = j + 1;
			}
			pathlen = srctail - srcbase;
			if (dstlen > 0 && dst[dstlen - 1] == '/') {
				pathlen += dstlen;
				path = malloc(pathlen + 1);
				if (path == NULL) {
					fprintf(stderr,
					    "%s: no memory for '%s%.*s'\n",
					    program_name, dst,
					    (int)(srctail - srcbase),
					    &src[srcbase]);
					status = EXIT_FAILURE;
					continue;
				}
				sprintf(path, "%s%.*s",
				     dst,
				     (int)(srctail - srcbase), &src[srcbase]);
			} else {
				pathlen += dstlen + 1;
				path = malloc(pathlen + 1);
				if (path == NULL) {
					fprintf(stderr,
					    "%s: no memory for '%s/%.*s'\n",
					    program_name, dst,
					    (int)(srctail - srcbase),
					    &src[srcbase]);
					status = EXIT_FAILURE;
					continue;
				}
				sprintf(path, "%s/%.*s",
				     dst,
				     (int)(srctail - srcbase), &src[srcbase]);
			}
			if (move_file(src, path) != 0)
				status = EXIT_FAILURE;
			free(path);
		}
	}

	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	free(dtmp);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = EXIT_FAILURE;
	}
	return (status);

}

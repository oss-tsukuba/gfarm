/*
 * $Id$
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>
#include "gfs_misc.h"

static char *program_name = "gftest";

static int
usage()
{
	fprintf(stderr,	"Usage: %s [-defrswxLR] file\n", program_name);
	exit(EXIT_FAILURE);
}

static void
error_check(char *msg, char *e)
{
	if (e == NULL)
		return;

	fprintf(stderr, "%s: %s\n", msg, e);
	exit(1);
}

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

static char *
is_local_section(struct gfarm_file_section_info *info, void *arg)
{
	struct gfarm_file_section_copy_info scinfo;
	char *host = arg, *e;

	e = gfarm_file_section_copy_info_get(
		info->pathname, info->section, host, &scinfo);
	if (e == NULL)
		gfarm_file_section_copy_info_free(&scinfo);
	return (e);
}

static char *
is_local_file(char *file)
{
	char *e, *path, *host;

	e = gfarm_url_make_path(file, &path);
	if (e != NULL)
		return (e);

	e = gfarm_host_get_canonical_self_name(&host);
	if (e != NULL)
		goto free_path;

	e = gfarm_foreach_section(is_local_section, path, host, NULL);
free_path:
	free(path);
	return (e);
}

static char *
is_not_local_section(struct gfarm_file_section_info *info, void *arg)
{
	char *e = is_local_section(info, arg);
	return (e == NULL ? "local file" : NULL);
}

static char *
is_not_local_file(char *file)
{
	char *e, *path, *host;

	e = gfarm_url_make_path(file, &path);
	if (e != NULL)
		return (e);

	e = gfarm_host_get_canonical_self_name(&host);
	if (e != NULL) {
		/* always true in case of non-filesystem node */
		e = NULL; 
		goto free_path;
	}
	
	e = gfarm_foreach_section(is_not_local_section, path, host, NULL);
free_path:
	free(path);
	return (e);
}

int
main(int argc, char *argv[])
{
	char *e, *file = NULL;
	struct gfs_stat st;
	int ch, mode_ch = 0, ret = 1;

	while ((ch = getopt(argc, argv, "d:e:f:r:s:w:x:L:R:?")) != -1) {
		switch (ch) {
		case 'd': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'e': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'f': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'r': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 's': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'w': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'x': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'L': conflict_check(&mode_ch, ch);	file = optarg; break;
		case 'R': conflict_check(&mode_ch, ch);	file = optarg; break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (file == NULL || mode_ch == 0 || argc > 0)
		usage();

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	e = gfs_stat(file, &st);
	if (e != NULL)
		return (1); /* not exist */

	switch (mode_ch) {
	case 'd': ret = GFARM_S_ISDIR(st.st_mode); break;
	case 'e': ret = 1;			   break;
	case 'f': ret = GFARM_S_ISREG(st.st_mode); break;
	case 'r': ret = gfs_access(file, R_OK) == NULL; break;
	case 's': ret = st.st_size > 0;		   break;
	case 'w': ret = gfs_access(file, W_OK) == NULL; break;
	case 'x': ret = gfs_access(file, X_OK) == NULL; break;
	case 'L': ret = is_local_file(file) == NULL; break;
	case 'R': ret = is_not_local_file(file) == NULL; break;
	default:
		break;
	}
	gfs_stat_free(&st);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	return (!ret);
}

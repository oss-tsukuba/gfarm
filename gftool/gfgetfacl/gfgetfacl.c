/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfgetfacl";

static void
usage(void)
{
	fprintf(stderr, "Usage: %s path...\n", program_name);
	exit(1);
}

static const char *
flags_str(gfarm_mode_t mode)
{
	static char str[4];

	str[0] = (mode & 04000) ? 's' : '-';
	str[1] = (mode & 02000) ? 's' : '-';
	str[2] = (mode & 01000) ? 't' : '-';
	str[3] = '\0';

	return (str);
}

static gfarm_error_t
acl_print(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	gfarm_acl_t acl_acc = NULL, acl_def = NULL;
	char *text;
	int options = GFARM_ACL_TEXT_SOME_EFFECTIVE;

	if (GFARM_S_ISLNK(st->st_mode))  /* ignore */
		return (GFARM_ERR_NO_ERROR);

	if (isatty(fileno(stdout)))
		options |= GFARM_ACL_TEXT_SMART_INDENT;

	/* gfs_getxattr_cached follows symlinks */
	e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_ACCESS, &acl_acc);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		gfarm_mode_t st_mode = st->st_mode;
		struct gfs_stat st2;

		if (GFARM_S_ISLNK(st->st_mode)) {
			/* follow symlinks */
			e = gfs_lstat(path, &st2);
			if (e != GFARM_ERR_NO_ERROR)
				goto end;
			st_mode = st2.st_mode;
			gfs_stat_free(&st2);
		}
		e = gfs_acl_from_mode(st_mode, &acl_acc);
	}
	if (e != GFARM_ERR_NO_ERROR)
		goto end;

	if (GFARM_S_ISDIR(st->st_mode)) {
		e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_DEFAULT,
					    &acl_def);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			acl_def = NULL;
		else if (e != GFARM_ERR_NO_ERROR)
			goto end;
	} else
		acl_def = NULL;

	printf("# file: %s\n", path);
	printf("# owner: %s\n", st->st_user);
	printf("# group: %s\n", st->st_group);
	if (st->st_mode & 07000)
		printf("# flags: %s\n", flags_str(st->st_mode));

	e = gfs_acl_to_any_text(acl_acc, NULL, '\n', options, &text);
	if (e != GFARM_ERR_NO_ERROR)
		goto end;
	printf("%s\n", text);
	free(text);

	if (acl_def != NULL) {
		e = gfs_acl_to_any_text(acl_def, "default:", '\n', options,
					&text);
		if (e != GFARM_ERR_NO_ERROR)
			goto end;
		printf("%s\n", text);
		free(text);
	}
	puts("");
end:
	gfs_acl_free(acl_acc);
	gfs_acl_free(acl_def);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, path, gfarm_error_string(e));
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int c, i, n, recursive = 0;
	char *si = NULL, *s;
	gfarm_stringlist paths;
	gfs_glob_t types;
	struct gfs_stat st;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "hR?")) != -1) {
		switch (c) {
		case 'R':
			recursive = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0)
		usage();

	gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_ACCESS);
	gfarm_xattr_caching_pattern_add(GFARM_ACL_EA_DEFAULT);

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
	for (i = 0; i < n; i++) {
		s = gfarm_stringlist_elem(&paths, i);
		e = gfarm_realpath_by_gfarm2fs(s, &si);
		if (e == GFARM_ERR_NO_ERROR)
			s = si;
		if ((e = gfs_lstat(s, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", s, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISDIR(st.st_mode) && recursive) {
				e = gfarm_foreach_directory_hierarchy(
				    acl_print, acl_print, NULL, s, NULL);
				gfs_stat_free(&st);
			} else if (GFARM_S_ISLNK(st.st_mode)) {
				gfs_stat_free(&st);
				/* follow symlinks */
				e = gfs_stat(s, &st);
				if (e == GFARM_ERR_NO_ERROR) {
					e = acl_print(s, &st, NULL);
					gfs_stat_free(&st);
				} else {
					fprintf(stderr, "%s: %s\n",
					    s, gfarm_error_string(e));
				}
			} else {
				e = acl_print(s, &st, NULL);
				gfs_stat_free(&st);
			}
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(si);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}

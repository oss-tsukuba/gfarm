/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

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
acl_print(char *path)
{
	gfarm_error_t e;
	struct gfs_stat sb;
	gfarm_acl_t acl_acc, acl_def;
	char *text;
	int options = GFARM_ACL_TEXT_SOME_EFFECTIVE;

	if (isatty(fileno(stdout)))
		options |= GFARM_ACL_TEXT_SMART_INDENT;

	e = gfs_stat_cached(path, &sb);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_ACCESS, &acl_acc);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = gfs_acl_from_mode(sb.st_mode, &acl_acc);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_stat_free(&sb);
		return (e);
	}

	if (GFARM_S_ISDIR(sb.st_mode)) {
		e = gfs_acl_get_file_cached(path, GFARM_ACL_TYPE_DEFAULT,
					    &acl_def);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			acl_def = NULL;
		else if (e != GFARM_ERR_NO_ERROR) {
			gfs_stat_free(&sb);
			gfs_acl_free(acl_acc);
			return (e);
		}
	} else
		acl_def = NULL;

	printf("# file: %s\n", path);
	printf("# owner: %s\n", sb.st_user);
	printf("# group: %s\n", sb.st_group);
	if (sb.st_mode & 07000)
		printf("# flags: %s\n", flags_str(sb.st_mode));

	e = gfs_acl_to_any_text(acl_acc, NULL, '\n', options, &text);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_stat_free(&sb);
		gfs_acl_free(acl_acc);
		gfs_acl_free(acl_def);
		return (e);
	}
	printf("%s\n", text);
	free(text);

	if (acl_def != NULL) {
		e = gfs_acl_to_any_text(acl_def, "default:", '\n', options,
					&text);
		if (e == GFARM_ERR_NO_ERROR) {
			printf("%s\n", text);
			free(text);
		}
	}

	gfs_stat_free(&sb);
	gfs_acl_free(acl_acc);
	gfs_acl_free(acl_def);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
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
	for (i = 0; i < argc; i++) {
		e = acl_print(argv[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s: %s\n",
			    program_name, argv[i], gfarm_error_string(e));
			status = 1;
		}
	}
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

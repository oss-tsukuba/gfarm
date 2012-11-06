/*
 * $Id$
 */

#include <stdio.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

static char *program_name = "empty_acl";

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, rv = 1, is_dir;
	char *path;
	struct gfs_stat st;
	gfarm_acl_t acl, acl2;
	gfarm_acl_type_t type = 0;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	while ((c = getopt(argc, argv, "ad")) != -1) {
		switch (c) {
		case 'a':
			type = GFARM_ACL_TYPE_ACCESS;
			break;
		case 'd':
			type = GFARM_ACL_TYPE_DEFAULT;
			break;
		default:
			fprintf(stderr, "unknown option\n");
			goto term;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr, "%s: %s <filename>\n", program_name,
		    argc == 0 ? "missing" : "extra arguments after");
		goto term;
	}
	path = argv[0];
	if (type == 0) {
		fprintf(stderr, "-a or -d option is necessary\n");
		goto term;
	}
	e = gfs_lstat(path, &st);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_lstat: %s\n",
		    gfarm_error_string(e));
		goto term;
	}
	is_dir = GFARM_S_ISDIR(st.st_mode);
	gfs_stat_free(&st);
	e = gfs_acl_init(1, &acl);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_acl_init: %s\n",
		    gfarm_error_string(e));
		goto term;
	}
	/*
	 * empty access ACL: always fail
	 * empty default ACL (not directory): fail
	 * empty default ACL (directory): remove
	 */
	e = gfs_acl_set_file(path, type, acl);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_acl_set_file: %s\n",
		    gfarm_error_string(e));
		if (e == GFARM_ERR_INVALID_ARGUMENT &&
		    (type == GFARM_ACL_TYPE_ACCESS || !is_dir)) {
			printf("expected error\n");
			rv = 0;
		}
	} else if (type == GFARM_ACL_TYPE_ACCESS)
		printf("should fail (1)\n");
	else { /* GFARM_ACL_TYPE_DEFAULT */
		if (is_dir) {
			e = gfs_acl_get_file(path, type, &acl2);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) {
				printf("success\n");
				rv = 0;
			} else if (e == GFARM_ERR_NO_ERROR) {
				gfs_acl_free(acl2);
				printf("should fail (2)\n");
			} else
				printf("unexpected error: %s\n",
				    gfarm_error_string(e));
		} else
			printf("should fail (3)\n");
	}
	gfs_acl_free(acl);
term:
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	return (rv);
}

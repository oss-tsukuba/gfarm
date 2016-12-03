#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "quota_info.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_dirquota.h"
#include "gfarm_path.h"

enum operation_mode {
	OP_ADD_TO = 'a',
	OP_CREATE = 'c',
	OP_DELETE = 'd',
	OP_LIST_LONG = 'l',
	OP_LIST = '\0',
	OP_DEFAULT = OP_LIST
};

static const char ALL_USERS[] = "";
static char *all_dirsets = "";

static const char *program_name = "gfdirquota";

static gfarm_error_t
dirset_add_to(const char *username, const char *dirsetname, const char *dir)
{
	gfarm_error_t e;
	char *realpath = NULL;

	if (gfarm_realpath_by_gfarm2fs(dir, &realpath) == GFARM_ERR_NO_ERROR)
		dir = realpath;
	if (username == NULL) {
		struct gfm_connection *gfm_server;

		if ((e = gfm_client_connection_and_process_acquire_by_path(
		    dir, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			    program_name, dir, gfarm_error_string(e));
			free(realpath);
			return (e);
		}
		username = gfm_client_username(gfm_server);
	}

	e = gfs_dirquota_add(dir, username, dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", dir, gfarm_error_string(e));

	free(realpath);
	return (e);
}

static gfarm_error_t
dirset_create(struct gfm_connection *gfm_server,
	const char *username, const char *dirsetname)
{
	gfarm_error_t e;

	e = gfm_client_dirset_info_set(gfm_server, username, dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s:%s: %s\n",
		    username, dirsetname, gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
dirset_delete(struct gfm_connection *gfm_server,
	const char *username, const char *dirsetname)
{
	gfarm_error_t e;

	e = gfm_client_dirset_info_remove(gfm_server, username, dirsetname);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s:%s: %s\n",
		    username, dirsetname, gfarm_error_string(e));
	return (e);
}

static int
dirset_dir_info_cmp(const void *a0, const void *b0)
{
	const struct gfarm_dirset_dir_info *a = a0, *b = b0;
	int cmp;

	cmp = strcmp(a->dirset.username, b->dirset.username);
	if (cmp != 0)
		return (cmp);
	return (strcmp(a->dirset.dirsetname, b->dirset.dirsetname));
}

static int
dir_cmp(const void *a0, const void *b0)
{
	const struct gfarm_dirset_dir_info_dir *a = a0, *b = b0;
	int cmp;

	if (a->dir != NULL && b->dir != NULL) {
		cmp = strcmp(a->dir, b->dir);
		if (cmp != 0)
			return (cmp);
	} else if (a->dir != NULL || b->dir != NULL) {
		if (a->dir != NULL)
			return (-1);
		else
			return (1);
	}
	if (a->error < b->error)
		return (-1);
	else if (a->error > b->error)
		return (1);
	else
		return (0);
}

static gfarm_error_t
dirset_list_long(struct gfm_connection *gfm_server,
	const char *username, const char *dirsetname,
	int print_dirset, int *need_newline)
{
	gfarm_error_t e;
	int i, ndirsets = 0;
	gfarm_uint32_t j;
	struct gfarm_dirset_dir_info *dirsets = NULL, *ds;
	struct gfarm_dirset_dir_info_dir *dir;
	int is_all_users = strcmp(username, ALL_USERS) == 0;
	int is_all_dirsets = strcmp(dirsetname, all_dirsets) == 0;

	print_dirset |= is_all_users || is_all_dirsets;

	e = gfm_client_dirset_dir_list(gfm_server, username, dirsetname,
	    &ndirsets, &dirsets);
	if (e == GFARM_ERR_NO_ERROR && ndirsets == 0 &&
	    !is_all_users && !is_all_dirsets) {
		/*
		 * when both user and dirset is specified,
		 * report if the dirset does not exist.
		 */
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gfarm_dirset_dir_list_free(ndirsets, dirsets);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fflush(stdout);
		if (is_all_dirsets) {
			fprintf(stderr, "%s: %s\n",
			    program_name, gfarm_error_string(e));
		} else if (is_all_users) {
			fprintf(stderr, "%s: %s\n",
			    dirsetname, gfarm_error_string(e));
		} else {
			fprintf(stderr, "%s:%s: %s\n",
			    username, dirsetname, gfarm_error_string(e));
		}
		return (e);
	}

	qsort(dirsets, ndirsets, sizeof(dirsets[0]), dirset_dir_info_cmp);
	for (i = 0; i < ndirsets; i++) {
		if (*need_newline)
			printf("\n");
		else
			*need_newline = 1;

		ds = &dirsets[i];
		if (print_dirset)
			printf("%s:%s:\n",
			    ds->dirset.username, ds->dirset.dirsetname);

		qsort(ds->dirs, ds->n_dirs, sizeof(ds->dirs[0]), dir_cmp);
		for (j = 0; j < ds->n_dirs; j++) {
			dir = &ds->dirs[j];
			if (dir->error == GFARM_ERR_NO_ERROR) {
				printf("%s\n", dir->dir);
			} else {
				fflush(stdout);
				fprintf(stderr, "%s:%s: error: %s\n",
				    ds->dirset.username,
				    ds->dirset.dirsetname,
				    gfarm_error_string(dir->error));
			}
		}
	}
	gfarm_dirset_dir_list_free(ndirsets, dirsets);

	return (e);
}

static int
dirset_cmp(const void *a0, const void *b0)
{
	const struct gfarm_dirset_info *a = a0, *b = b0;
	int cmp;

	cmp = strcmp(a->username, b->username);
	if (cmp != 0)
		return (cmp);
	return (strcmp(a->dirsetname, b->dirsetname));
}

static gfarm_error_t
dirset_list_all(struct gfm_connection *gfm_server, const char *username)
{
	gfarm_error_t e;
	int i, ndirsets, print_user = 0;
	struct gfarm_dirset_info *dirsets, *current, *previous = NULL;

	if (*username == '\0') /* this means all users */
		print_user = 1;

	e = gfm_client_dirset_info_list(gfm_server, username,
	    &ndirsets, &dirsets);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    *username == '\0' ? "(ALL_USER)" : username,
		    gfarm_error_string(e));
		return (e);
	}
	qsort(dirsets, ndirsets, sizeof(dirsets[0]), dirset_cmp);
	for (i = 0; i < ndirsets; i++) {
		current = &dirsets[i];
		if (print_user &&
		    (previous == NULL ||
		     strcmp(current->username, previous->username) != 0)) {
			if (previous != NULL)
				printf("\n");
			printf("%s:\n", current->username);
			previous = current;
		}
		printf("%s\n", current->dirsetname);
	}
	for (i = 0; i < ndirsets; i++)
		gfarm_dirset_info_free(&dirsets[i]);
	free(dirsets);
	return (e);
}

static void
usage(void)
{
	fprintf(stderr, "Usage:"
	    "\t%s [-u <user>] [-P <path>] -c <dirset_name>...\n"
	    "\t%s [-u <user>] [-P <path>] -d <dirset_name>...\n"
	    "\t%s [-u <user>] -a <dirset_name> <dir>...\n"
	    "\t%s [-u <user> | -A] [-P <path>]\n"
	    "\t%s [-u <user> | -A] [-P <path>] -l [<dirset>...]\n",
	    program_name, program_name, program_name,
	    program_name, program_name);
	exit(EXIT_FAILURE);
}

static void
check_and_set(enum operation_mode *op_modep, enum operation_mode opt)
{
	if (*op_modep != OP_DEFAULT) {
		fprintf(stderr, "%s: -%c option conflicts with -%c\n",
		    program_name, opt, *op_modep);
		usage();
		/*NOTREACHED*/
	}
	*op_modep = opt;
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server = NULL;
	enum operation_mode op_mode = OP_DEFAULT;
	int opt_all_user = 0;
	const char *opt_username = NULL;
	static const char OPT_PATH_DEFAULT[] = ".";
	const char *opt_path = OPT_PATH_DEFAULT, *opt_dirsetname_to_add = NULL;
	int c, i, need_newline = 0, exit_code = EXIT_SUCCESS;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	while ((c = getopt(argc, argv, "AP:a:cdlu:?")) != -1) {
		switch (c) {
		case 'A':
			if (opt_username != NULL) {
				fprintf(stderr,
				    "%s: -A option conflicts with -u %s\n",
				    program_name, opt_username);
				usage();
				/*NOTREACHED*/
			}
			opt_all_user = 1;
			break;
		case 'P':
			opt_path = optarg;
			break;
		case OP_ADD_TO:
			check_and_set(&op_mode, c);
			opt_dirsetname_to_add = optarg;
			break;
		case OP_CREATE:
			check_and_set(&op_mode, c);
			break;
		case OP_DELETE:
			check_and_set(&op_mode, c);
			break;
		case OP_LIST_LONG:
			check_and_set(&op_mode, c);
			break;
		case 'u':
			if (opt_all_user) {
				fprintf(stderr, "%s: -u %s option conflicts "
				    "with -A\n", program_name, optarg);
				usage();
				/*NOTREACHED*/
			}
			opt_username = optarg;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (op_mode == OP_ADD_TO) {
		if (opt_path != OPT_PATH_DEFAULT) {
			fprintf(stderr, "%s: -P %s option conflicts "
			    "with -a\n", program_name, opt_path);
			exit(EXIT_FAILURE);
		}
	} else {
		char *realpath = NULL;

		if (gfarm_realpath_by_gfarm2fs(opt_path, &realpath)
		    == GFARM_ERR_NO_ERROR)
			opt_path = realpath;
		if ((e = gfm_client_connection_and_process_acquire_by_path(
		    opt_path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
			    program_name, opt_path, gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
		free(realpath);
	}

	if (opt_all_user && op_mode != OP_LIST && op_mode != OP_LIST_LONG) {
		fprintf(stderr, "%s: -A option conflicts with -%c\n",
		    program_name, op_mode);
		exit(EXIT_FAILURE);
	}
	if (opt_username == NULL && op_mode != OP_ADD_TO) {
		if (opt_all_user)
			opt_username = ALL_USERS;
		else
			opt_username = gfm_client_username(gfm_server);
	}

	switch (op_mode) {
	case OP_ADD_TO:
		for (i = 0; i < argc; i++) {
			e = dirset_add_to(opt_username,
			    opt_dirsetname_to_add, argv[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				exit_code = EXIT_FAILURE;
				/* do not do "break;" here */
			}
		}
		break;
	case OP_CREATE:
		for (i = 0; i < argc; i++) {
			e = dirset_create(gfm_server, opt_username, argv[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				exit_code = EXIT_FAILURE;
				/* do not do "break;" here */
			}
		}
		break;
	case OP_DELETE:
		for (i = 0; i < argc; i++) {
			e = dirset_delete(gfm_server, opt_username, argv[i]);
			if (e != GFARM_ERR_NO_ERROR) {
				exit_code = EXIT_FAILURE;
				/* do not do "break;" here */
			}
		}
		break;
	case OP_LIST_LONG:
		if (argc == 0) {
			argc = 1;
			argv = &all_dirsets;
		}
		for (i = 0; i < argc; i++) {
			e = dirset_list_long(gfm_server,
			    opt_username, argv[i], argc != 1, &need_newline);
			if (e != GFARM_ERR_NO_ERROR) {
				exit_code = EXIT_FAILURE;
				/* do not do "break;" here */
			}
		}
		break;
	case OP_LIST:
		if (argc > 0) {
			usage();
			/*NOTREACHED*/
		}
		e = dirset_list_all(gfm_server, opt_username);
		if (e != GFARM_ERR_NO_ERROR)
			exit_code = EXIT_FAILURE;
		break;
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (exit_code);
}

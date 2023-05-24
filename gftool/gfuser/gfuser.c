/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include <gfarm/gfarm.h>
#include <gfm_proto.h>

#include "config.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

char *program_name = "gfuser";

#define OP_USERNAME	'\0'
#define OP_LIST_LONG	'l'
#define OP_CREATE_ENTRY	'c'
#define OP_MODIFY_ENTRY	'm'
#define OP_DELETE_ENTRY	'd'
#define OP_LIST_AUTH	'L'
#define OP_MODIFY_AUTH	'A'

struct gfm_connection *gfm_server;

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-P <path>] [-l] [username ...]\n",
	    program_name);
	fprintf(stderr,
	    "\t%s [-P <path>] -c username realname homedir gsi_dn\n",
	    program_name);
	fprintf(stderr,
	    "\t%s [-P <path>] -m username realname homedir gsi_dn\n",
	    program_name);
	fprintf(stderr,
	    "\t%s [-P <path>] -d username\n",
	    program_name);
	fprintf(stderr,
	    "\t%s [-P <path>] -L [username ...]\n",
	    program_name);
	fprintf(stderr,
	    "\t%s [-P <path>] -A username auth_id_type auth_id\n",
	    program_name);
	exit(1);
}

/*
 * NOTE: names and errs may be NULL in case of list_all()
 */
static gfarm_error_t
display_user(int op, int nusers, char *names[],
	gfarm_error_t *errs, struct gfarm_user_info *users)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, j;
	char *auth_id;

	for (i = 0; i < nusers; i++) {
		if (errs != NULL && errs[i] != GFARM_ERR_NO_ERROR) {
			assert(names != NULL);
			fprintf(stderr, "%s: %s\n", names[i],
				gfarm_error_string(errs[i]));
			if (e == GFARM_ERR_NO_ERROR)
				e = errs[i];
			continue;
		}
		switch (op) {
		case OP_USERNAME:
			puts(users[i].username);
			break;
		case OP_LIST_LONG:
			printf("%s:%s:%s:%s\n",
			       users[i].username, users[i].realname,
			       users[i].homedir, users[i].gsi_dn);
			break;
		case OP_LIST_AUTH:
			printf("%s:%s:%s:%s\n",
			       users[i].username, users[i].realname,
			       users[i].homedir, users[i].gsi_dn);

			for (j = 0; j < gfarm_auth_user_id_type_number; j++) {
				e = gfm_client_user_auth_get(gfm_server,
					     users[i].username,
					     gfarm_auth_user_id_type_list[j],
					     &auth_id);
				if (e == GFARM_ERR_NO_ERROR) {
					if (strcmp(auth_id, "") != 0)
						printf("\t%s:%s\n",
						gfarm_auth_user_id_type_list[j],
						auth_id);
				} else {
					fprintf(stderr,
					    "%s: auth_type %s: %s\n",
					    users[i].username,
					    gfarm_auth_user_id_type_list[j],
					    gfarm_error_string(e));
					continue;
				}
				free(auth_id);
			}
			break;
		}
		gfarm_user_info_free(&users[i]);
	}
	return (e);
}

gfarm_error_t
list_all(int op)
{
	struct gfarm_user_info *users;
	gfarm_error_t e;
	int nusers;

	e = gfm_client_user_info_get_all(gfm_server, &nusers, &users);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = display_user(op, nusers, NULL, NULL, users);

	free(users);
	return (e);
}

gfarm_error_t
list(int op, int n, char *names[])
{
	struct gfarm_user_info *users;
	gfarm_error_t e, *errs;

	GFARM_MALLOC_ARRAY(users, n);
	GFARM_MALLOC_ARRAY(errs, n);
	if (users == NULL || errs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else if ((e = gfm_client_user_info_get_by_names(
	    gfm_server, n, (const char **)names, errs, users)) !=
	    GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else {
		e = display_user(op, n, names, errs, users);
	}
	free(users);
	free(errs);
	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	char opt_operation = '\0'; /* default operation */
	struct gfarm_user_info ui;
	const char *path = ".";
	char *realpath = NULL;
	char *username = NULL;
	char *auth_id_type = NULL;
	char *auth_id = NULL;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "P:cdhlmLA?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case 'c':
		case 'd':
		case 'l':
		case 'm':
		case 'L':
		case 'A':
			opt_operation = c;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	if ((e = gfm_client_connection_and_process_acquire_by_path(path,
	    &gfm_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata server for \"%s\": %s\n",
		    program_name, path, gfarm_error_string(e));
		exit(1);
	}
	free(realpath);

	switch (opt_operation) {
	case OP_USERNAME:
	case OP_LIST_LONG:
		if (argc == 0)
			e = list_all(opt_operation);
		else
			e = list(opt_operation, argc, argv);
		break;
	case OP_CREATE_ENTRY:
	case OP_MODIFY_ENTRY:
		if (argc != 4)
			usage();
		ui.username = argv[0];
		ui.realname = argv[1];
		ui.homedir = argv[2];
		ui.gsi_dn = argv[3];
		switch (opt_operation) {
		case OP_CREATE_ENTRY:
			e = gfm_client_user_info_set(gfm_server, &ui);
			break;
		case OP_MODIFY_ENTRY:
			e = gfm_client_user_info_modify(
				gfm_server, &ui);
			break;
		}
		break;
	case OP_DELETE_ENTRY:
		if (argc != 1)
			usage();
		e = gfm_client_user_info_remove(gfm_server, argv[0]);
		break;
	case OP_LIST_AUTH:
		if (argc == 0)
			e = list_all(opt_operation);
		else
			e = list(opt_operation, argc, argv);
		break;
	case OP_MODIFY_AUTH:
		if (argc != 3)
			usage();
		username = argv[0];
		auth_id_type = argv[1];
		auth_id = argv[2];
		e = gfm_client_user_auth_modify(gfm_server,
			username,
			auth_id_type,
			auth_id);
		break;

	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = 1;
	}

	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

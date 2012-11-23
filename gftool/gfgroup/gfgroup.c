/*
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <assert.h>

#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "lookup.h"
#include "config.h"
#include "gfarm_path.h"

#define OP_GROUPNAME	'\0'
#define OP_LIST_LONG	'l'
#define OP_CREATE_GROUP	'c'
#define OP_DELETE_GROUP	'd'
#define OP_MODIFY_GROUP	'm'
#define OP_ADD_ENTRY	'a'
#define OP_REMOVE_ENTRY	'r'

char *program_name = "gfgroup";

struct gfm_connection *gfm_server;

void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-P <path>] [-l] [groupname ...]\n",
	    program_name);
	fprintf(stderr, "\t%s [-P <path>] -c groupname user1 user2 ...\n",
	    program_name);
	fprintf(stderr, "\t%s [-P <path>] -m groupname user1 user2 ...\n",
	    program_name);
	fprintf(stderr, "\t%s [-P <path>] -d groupname\n", program_name);
	exit(1);
}

gfarm_error_t
create_group(int op, char *groupname, int nusers, char **users)
{
	struct gfarm_group_info group;
	gfarm_error_t e;

	group.groupname = groupname;
	group.nusers = nusers;
	group.usernames = users;

	switch (op) {
	case OP_CREATE_GROUP:
		e = gfm_client_group_info_set(gfm_server, &group);
		break;
	case OP_MODIFY_GROUP:
		e = gfm_client_group_info_modify(gfm_server, &group);
		break;
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
	}
	return (e);
}

gfarm_error_t
delete_group(char *groupname)
{
	return (gfm_client_group_info_remove(gfm_server, groupname));
}

static gfarm_error_t
display_group(int op, int n, char *names[],
	gfarm_error_t *errs, struct gfarm_group_info *groups)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int i, j;

	for (i = 0; i < n; ++i) {
		if (errs != NULL && errs[i] != GFARM_ERR_NO_ERROR) {
			assert(names != NULL);
			fprintf(stderr, "%s: %s\n", names[i],
				gfarm_error_string(errs[i]));
			if (e == GFARM_ERR_NO_ERROR)
				e = errs[i];
			continue;
		}
		printf("%s", groups[i].groupname);
		if (op == OP_LIST_LONG) {
			printf(":");
			for (j = 0; j < groups[i].nusers; ++j)
				printf(" %s", groups[i].usernames[j]);
		}
		puts("");
		gfarm_group_info_free(&groups[i]);
	}
	return (e);
}

gfarm_error_t
list_all(int op)
{
	struct gfarm_group_info *groups;
	gfarm_error_t e;
	int n;

	e = gfm_client_group_info_get_all(gfm_server, &n, &groups);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = display_group(op, n, NULL, NULL, groups);

	free(groups);
	return (e);
}

gfarm_error_t
list(int op, int n, char *names[])
{
	struct gfarm_group_info *groups;
	gfarm_error_t e, *errs;

	GFARM_MALLOC_ARRAY(groups, n);
	GFARM_MALLOC_ARRAY(errs, n);
	if (groups == NULL || errs == NULL) {
		e = GFARM_ERR_NO_MEMORY;
	} else if ((e = gfm_client_group_info_get_by_names(
	    gfm_server, n, (const char **)names, errs, groups)) !=
	    GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else {
		e = display_group(op, n, names, errs, groups);
	}
	free(groups);
	free(errs);
	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	char op = OP_GROUPNAME, *groupname;
	int c;
	const char *path = ".";
	char *realpath = NULL;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "P:cdlm?")) != -1) {
		switch (c) {
		case 'P':
			path = optarg;
			break;
		case OP_CREATE_GROUP:
		case OP_DELETE_GROUP:
		case OP_MODIFY_GROUP:
		case OP_LIST_LONG:
			op = c;
			break;
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

	switch (op) {
	case OP_GROUPNAME:
	case OP_LIST_LONG:
		if (argc == 0)
			e = list_all(op);
		else
			e = list(op, argc, argv);
		break;
	case OP_CREATE_GROUP:
	case OP_MODIFY_GROUP:
		if (argc < 1)
			usage();
		groupname = *argv++;
		--argc;
		e = create_group(op, groupname, argc, argv);
		break;
	case OP_DELETE_GROUP:
		if (argc != 1)
			usage();
		e = delete_group(*argv);
		break;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}
	exit(0);
}

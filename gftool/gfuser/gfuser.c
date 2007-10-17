/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "config.h"
#include "gfm_client.h"

char *program_name = "gfuser";

#define OP_USERNAME	'\0'
#define OP_LIST_LONG	'l'
#define OP_CREATE_ENTRY	'c'
#define OP_MODIFY_ENTRY	'm'
#define OP_DELETE_ENTRY	'd'

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s [-l]\n", program_name);
	fprintf(stderr, "\t%s -c username realname homedir gsi_dn\n",
	    program_name);
	fprintf(stderr, "\t%s -m username realname homedir gsi_dn\n",
	    program_name);
	fprintf(stderr, "\t%s -d username\n",
	    program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int i, c, status = 0;
	char opt_operation = '\0'; /* default operation */
	extern int optind;
	int nusers;
	struct gfarm_user_info *users, ui;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "cdhlm?")) != -1) {
		switch (c) {
		case 'c':
		case 'd':
		case 'l':
		case 'm':
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

	switch (opt_operation) {
	case OP_USERNAME:
	case OP_LIST_LONG:
		if (argc != 0)
			usage();
		e = gfm_client_user_info_get_all(gfarm_metadb_server,
		    &nusers, &users);
		if (e != GFARM_ERR_NO_ERROR)
			break;
		for (i = 0; i < nusers; i++) {
			switch (opt_operation) {
			case OP_USERNAME:
				puts(users[i].username);
				break;
			case OP_LIST_LONG:
				printf("%s:%s:%s:%s\n",
				    users[i].username, users[i].realname,
				    users[i].homedir, users[i].gsi_dn);
				break;
			}
		}
		gfarm_user_info_free_all(nusers, users);
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
			e = gfm_client_user_info_set(gfarm_metadb_server, &ui);
			break;
		case OP_MODIFY_ENTRY:
			e = gfm_client_user_info_modify(
				gfarm_metadb_server, &ui);
			break;
		}
		break;
	case OP_DELETE_ENTRY:
		if (argc != 1)
			usage();
		e = gfm_client_user_info_remove(gfarm_metadb_server, argv[0]);
		break;
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = 1;
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

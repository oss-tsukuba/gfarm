/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>
#include "gfs_client.h"

char *program_name = "gfsetdir";

void
usage()
{
	fprintf(stderr, "Usage: %s [-s|-c] [directory]\n", program_name);
	fprintf(stderr, "\t-c\t output string for *csh\n");
        fprintf(stderr, "\t-s\t                   otherwise\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	extern int optind;
	char *e, *canonic_path, *gfarm_path, *peer_hostname;
	int ch;
	struct sockaddr peer_addr;
	struct gfs_connection *peer_conn;
	enum { UNDECIDED,
	       B_SHELL_LIKE,
	       C_SHELL_LIKE
	} shell_type = UNDECIDED;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "sc")) != -1) {
		switch (ch) {
		case 's':
			shell_type = B_SHELL_LIKE;
			break;
		case 'c':
			shell_type = C_SHELL_LIKE;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * Get absolute path from the argument directory name.
	 * If no arugument is passed, generate gfarm:/"global username".
	 */
	canonic_path = NULL;
	switch (argc) {
	case 0:
		canonic_path = gfarm_get_global_username();
		break;
	case 1:
		e = gfarm_canonical_path(argv[0], &canonic_path);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", program_name, e);
			exit(1);
		}
		break;
	default:
		usage();
	}

	/*
	 * Check existence of the absolute path in a host.
	 */
	e = gfarm_schedule_search_idle_by_all(1, &peer_hostname);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfarm_host_address_get(peer_hostname, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfs_client_connect(peer_hostname, &peer_addr, &peer_conn);
	free(peer_hostname);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfs_client_chdir(peer_conn, canonic_path);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	e = gfs_client_disconnect(peer_conn);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	gfarm_path = malloc(strlen(GFARM_URL_PREFIX) + strlen(canonic_path) +
		     2);
	if (gfarm_path == NULL) {
		fprintf(stderr, "%s: %s\n", program_name, GFARM_ERR_NO_MEMORY);
		exit(1);
	}
	sprintf(gfarm_path, "%s/%s", GFARM_URL_PREFIX, canonic_path);
	free(canonic_path);
	if (shell_type == UNDECIDED) {
		char *shell;
		shell = getenv("SHELL");
		if (strlen(shell) < 3 || 
		    strncmp(shell + strlen(shell) - 3, "csh", 3))
			shell_type = B_SHELL_LIKE;
		else
			shell_type = C_SHELL_LIKE;
	}
	if (shell_type == B_SHELL_LIKE)
		printf("GFS_PWD=%s; export GFS_PWD", gfarm_path);
	else
		printf("setenv GFS_PWD %s", gfarm_path);
	fflush(stdout);
	free(gfarm_path);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (0);
}

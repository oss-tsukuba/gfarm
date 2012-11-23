/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

#include "context.h"
#include "config.h"
#include "auth.h"
#include "host.h"
#include "gfpath.h"
#include "metadb_server.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

char *program_name = "gfstatus";

void
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;

	fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
	exit(EXIT_FAILURE);
}

void
print_msg(char *msg, const char *status)
{
	if (msg != NULL && status != NULL)
		printf("%s: %s\n", msg, status);
}

void
print_user_config_file(char *msg)
{
	static char gfarm_client_rc[] = GFARM_CLIENT_RC;
	char *rc;

	/* copied from gfarm_config_read() in config_client.c */
	printf("%s: ", msg);
	rc = getenv("GFARM_CONFIG_FILE");
	if (rc == NULL)
		printf("%s/%s\n", gfarm_get_local_homedir(), gfarm_client_rc);
	else
		printf("%s\n", rc);
}

void
usage(void)
{
	fprintf(stderr,
	    "Usage:\t%s [-P <path>]\n",
	    program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e, e2;
	int port, c;
	char *canonical_hostname, *hostname, *realpath = NULL;
	const char *user, *gfmd_hostname;
	const char *path = ".";
	struct gfm_connection *gfm_server;
	struct gfarm_metadb_server *ms;
#ifdef HAVE_GSI
	char *cred;
#endif

	if (argc > 0)
		program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "dP:?"))
	    != -1) {
		switch (c) {
		case 'd':
			gflog_set_priority_level(LOG_DEBUG);
			break;
		case 'P':
			path = optarg;
			break;
		case '?':
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	if ((e = gfm_client_connection_and_process_acquire_by_path(
	    path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
		if ((e2 = gfarm_get_hostname_by_url(path, &hostname, &port))
		    != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "cannot get metadata server name"
			    " represented by `%s': %s\n",
			    path, gfarm_error_string(e2));
		else {
			fprintf(stderr, "metadata server `%s', port %d: %s\n",
			    hostname, port,
			    gfarm_error_string(e));
			free(hostname);
		}
		exit(EXIT_FAILURE);
	}
	user = gfm_client_username(gfm_server);

	print_user_config_file("user config file  ");
	print_msg("system config file", gfarm_config_get_filename());

	puts("");
	print_msg("hostname          ", gfarm_host_get_self_name());
	e = gfm_host_get_canonical_self_name(gfm_server,
	    &canonical_hostname, &port);
	if (e == GFARM_ERR_NO_ERROR)
		printf("canonical hostname: %s:%d\n",
		    canonical_hostname, port);
	else
		printf("canonical hostname: not available\n");
#if 0
	print_msg("active fs node    ",
		  gfarm_is_active_file_system_node ? "yes" : "no");
#endif

	puts("");
	print_msg("global username", user);
	print_msg(" local username", gfarm_get_local_username());
	print_msg(" local home dir", gfarm_get_local_homedir());
#ifdef HAVE_GSI
	cred = gfarm_gsi_client_cred_name();
	print_msg("credential name", cred ? cred : "no credential");
#endif
	/* gfmd */
	puts("");
	ms = gfm_client_connection_get_real_server(gfm_server);
	if (ms == NULL) {
		if ((e = gfarm_get_hostname_by_url(
		    path, &hostname, &port)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "cannot get metadata server name"
			    " represented by `%s': %s\n",
			    path, gfarm_error_string(e));
			exit(EXIT_FAILURE);
		}
		gfmd_hostname = hostname;
	} else {
		gfmd_hostname = gfarm_metadb_server_get_name(ms);
		port = gfarm_metadb_server_get_port(ms);
	}
	free(realpath);
	print_msg("gfmd server name", gfmd_hostname);
	printf("gfmd server port: %d\n", port);
	print_msg("gfmd admin user", gfarm_ctxp->metadb_admin_user);
	print_msg("gfmd admin dn  ", gfarm_ctxp->metadb_admin_user_gsi_dn);

	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "auth.h"
#include "host.h"
#include "gfpath.h"
#include "gfm_client.h"

void
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;

	fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
	exit(EXIT_FAILURE);
}

void
print_msg(char *msg, char *status)
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

int
main(int argc, char *argv[])
{
	int port;
	char *name;
	gfarm_error_t e;

	/* XXX FIXME: this doesn't support multiple metadata server. */
	struct gfm_connection *gfarm_metadb_server;

#ifdef HAVE_GSI
	char *cred;
#endif
#if 0
	char *arch;
	extern int gfarm_is_active_file_system_node;
#endif
	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	if ((e = gfm_client_connection_and_process_acquire(
	    gfarm_metadb_server_name, gfarm_metadb_server_port,
	    &gfarm_metadb_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "metadata server `%s', port %d: %s\n",
		    gfarm_metadb_server_name, gfarm_metadb_server_port,
		    gfarm_error_string(e));
		exit (1);
	}

	print_user_config_file("user config file  ");
	print_msg("system config file", gfarm_config_file);

	puts("");
	print_msg("hostname          ", gfarm_host_get_self_name());
	e = gfm_host_get_canonical_self_name(gfarm_metadb_server,
	    &name, &port);
	if (e == GFARM_ERR_NO_ERROR)
		printf("canonical hostname: %s:%d\n", name, port);
	else
		printf("canonical hostname: not available\n");
#if 0
	e = gfarm_host_get_self_architecture(&arch);
	print_msg("architecture name ",
		  e == GFARM_ERR_NO_ERROR ? arch : gfarm_error_string(e));
	print_msg("active fs node    ",
		  gfarm_is_active_file_system_node ? "yes" : "no");
#endif
	puts("");
	print_msg("global username", gfarm_get_global_username());
	print_msg(" local username", gfarm_get_local_username());
	print_msg(" local home dir", gfarm_get_local_homedir());
#ifdef HAVE_GSI
	cred = gfarm_gsi_client_cred_name();
	print_msg("credential name", cred ? cred : "no credential");
#endif
	/* gfmd */
	puts("");
	print_msg("gfmd server name", gfarm_metadb_server_name);
	printf("gfmd server port: %d\n", gfarm_metadb_server_port);
	print_msg("gfmd admin user", gfarm_metadb_admin_user);
	print_msg("gfmd admin dn  ", gfarm_metadb_admin_user_gsi_dn);

	/* XXX FIXME: this doesn't support multiple metadata server. */
	gfm_client_connection_free(gfarm_metadb_server);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

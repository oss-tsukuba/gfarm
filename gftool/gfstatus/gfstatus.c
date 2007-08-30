/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "auth.h"
#include "agent_wrap.h"

void
error_check(char *msg, char *e)
{
	if (e == NULL)
		return;

	fprintf(stderr, "%s: %s\n", msg, e);
	exit(1);
}

void
print_msg(char *msg, char *status)
{
	if (msg != NULL && status != NULL)
		printf("%s: %s\n", msg, status);
}

int
main(int argc, char *argv[])
{
	char *e, *name, *arch;
#ifdef HAVE_GSI
	char *cred;
#endif
	extern int gfarm_is_active_file_system_node;

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	print_msg("hostname          ", gfarm_host_get_self_name());
	e = gfarm_host_get_canonical_self_name(&name);
	print_msg("canonical hostname", e == NULL ? name : e);
	e = gfarm_host_get_self_architecture(&arch);
	print_msg("architecture name ", e == NULL ? arch : e);
	print_msg("active fs node    ",
		  gfarm_is_active_file_system_node ? "yes" : "no");

	puts("");
	print_msg("global username", gfarm_get_global_username());
	print_msg(" local username", gfarm_get_local_username());
	print_msg(" local home dir", gfarm_get_local_homedir());
#ifdef HAVE_GSI
	cred = gfarm_gsi_client_cred_name();
	print_msg("credential name", cred ? cred : "no credential");
#endif

	puts("");
	printf("gfsd server port: %d\n", gfarm_spool_server_port);

	puts("");
	e = gfarm_agent_check();
	if (e != NULL)
		print_msg("metadata cache server", e);
	else {
		print_msg("metadata cache server name",
			  gfarm_agent_name_get());
		printf("metadata cache server port: %d\n",
		       gfarm_agent_port_get());
	}

	/* metadata backend database server */
	puts("");
	/* LDAP */
	print_msg("LDAP server name", gfarm_ldap_server_name);
	print_msg("LDAP server port", gfarm_ldap_server_port);
	print_msg("LDAP base dn    ", gfarm_ldap_base_dn);
	print_msg("LDAP bind dn    ", gfarm_ldap_bind_dn);
	/* PGSQL */
	print_msg("PGSQL server name", gfarm_postgresql_server_name);
	print_msg("PGSQL server port", gfarm_postgresql_server_port);
	print_msg("PGSQL dbname     ", gfarm_postgresql_dbname);
	print_msg("PGSQL user       ", gfarm_postgresql_user);
	print_msg("PGSQL connection info", gfarm_postgresql_conninfo);
	/* LocalFS */
	print_msg("LocalFS datadir", gfarm_localfs_datadir);
	if (!gfarm_ldap_server_name && !gfarm_postgresql_server_name &&
	    !gfarm_localfs_datadir)
		print_msg("metadata backend server", "not available");

	/* gfmd */
	puts("");
	if (gfarm_metadb_server_name) {
		print_msg("gfmd server name", gfarm_metadb_server_name);
		printf("gfmd server port: %d\n", gfarm_metadb_server_port);
	}
	else
		print_msg("gfmd server", "not available");

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

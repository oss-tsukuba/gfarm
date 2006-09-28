/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <gfarm/gfarm.h>

void
error_check(char *msg, char *e)
{
	if (e == NULL)
		return;

	fprintf(stderr, "%s: %s\n", msg, e);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *e, *name, *arch;
	extern int gfarm_is_active_file_system_node;

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);

	printf("hostname          : %s\n", gfarm_host_get_self_name());
	e = gfarm_host_get_canonical_self_name(&name);
	printf("canonical hostname: %s\n", e == NULL ? name : e);
	e = gfarm_host_get_self_architecture(&arch);
	printf("architecture name : %s\n", e == NULL ? arch : e);
	printf("active fs node    : %s\n",
	       gfarm_is_active_file_system_node ? "yes" : "no");

	puts("");
	printf("global username: %s\n", gfarm_get_global_username());
	printf(" local username: %s\n", gfarm_get_local_username());
	printf(" local home dir: %s\n", gfarm_get_local_homedir());

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

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
#include "quota_info.h"

char *program_name = "gfquota";

static void
usage(void)
{
	fprintf(stderr, "Usage:\t%s\n", program_name);
	exit(1);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int c, status = 0;
	/* XXX FIXME: this doesn't support multiple metadata server. */
	struct gfm_connection *gfarm_metadb_server;

	if (argc > 0)
		program_name = basename(argv[0]);
	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "h?")) != -1) {
		switch (c) {
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* XXX FIXME: this doesn't support multiple metadata server. */
	if ((e = gfm_client_connection_and_process_acquire(
		     gfarm_metadb_server_name, gfarm_metadb_server_port,
		     &gfarm_metadb_server)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "metadata server `%s', port %d: %s\n",
			gfarm_metadb_server_name, gfarm_metadb_server_port,
			gfarm_error_string(e));
		status = 1;
		goto terminate;
	}
	e = gfm_client_quota_check(gfarm_metadb_server);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		status = 1;
	}
	/* XXX FIXME: this doesn't support multiple metadata server. */
	gfm_client_connection_free(gfarm_metadb_server);
terminate:
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		status = 1;
	}
	return (status);
}

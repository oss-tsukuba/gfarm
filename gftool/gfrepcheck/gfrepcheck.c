#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "gfm_client.h"
#include "lookup.h"
#include "gfarm_path.h"

char *program_name = "gfrepcheck";

static void
error_check(const char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;

	fprintf(stderr, "%s: %s\n", msg, gfarm_error_string(e));
	exit(EXIT_FAILURE);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: \t%s %s\n" "\t%s %s\n" "\t%s %s\n"
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n",
	    program_name, "[-P <path>] enable(or start)",
	    program_name, "[-P <path>] disable(or stop)",
	    program_name, "[-P <path>] remove enable",
	    program_name, "[-P <path>] remove disable",
	    program_name, "[-P <path>] reduced_log enable",
	    program_name, "[-P <path>] reduced_log disable");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int c;
	const char *path = ".";
	char *realpath = NULL;
	struct gfm_connection *gfm_server;
	enum { START, STOP, REMOVE, REDUCED_LOG } ctrl = STOP;
#define ENABLE  1
#define DISABLE 0
	int value = ENABLE;

	if (argc > 0)
		program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "P:?")) != -1) {
		switch (c) {
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

	if (argc < 1)
		usage();
	if (strcmp(*argv, "start") == 0 || strcmp(*argv, "enable") == 0)
		ctrl = START;
	else if (strcmp(*argv, "stop") == 0 || strcmp(*argv, "disable") == 0)
		ctrl = STOP;
	else if (strcmp(*argv, "remove") == 0)
		ctrl = REMOVE;
	else if (strcmp(*argv, "reduced_log") == 0)
		ctrl = REDUCED_LOG;
	else
		usage();

	if (ctrl == START || ctrl == STOP) {
		if (argc != 1)
			usage();
	} else {
		if (argc != 2)
			usage();
		argv++;
		if (strcmp(*argv, "enable") == 0)
			value = ENABLE;
		else if (strcmp(*argv, "disable") == 0)
			value = DISABLE;
		else
			usage();
	}

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	e = gfm_client_connection_and_process_acquire_by_path(
		path, &gfm_server);
	error_check(path, e);

	switch (ctrl) {
	case START:
		e = gfm_client_replica_check_ctrl_start(gfm_server);
		break;
	case STOP:
		e = gfm_client_replica_check_ctrl_stop(gfm_server);
		break;
	case REMOVE:
		if (value)
			e = gfm_client_replica_check_ctrl_remove_enable(
			    gfm_server);
		else
			e = gfm_client_replica_check_ctrl_remove_disable(
			    gfm_server);
		break;
	case REDUCED_LOG:
		if (value)
			e = gfm_client_replica_check_ctrl_reduced_log_enable(
			    gfm_server);
		else
			e = gfm_client_replica_check_ctrl_reduced_log_disable(
			    gfm_server);
		break;
	}
	error_check(path, e);

	free(realpath);
	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

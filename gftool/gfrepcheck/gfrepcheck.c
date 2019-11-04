#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <gfarm/gfarm.h>

#include "config.h"
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
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n"
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n"
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n"
	    "\t%s %s\n" "\t%s %s\n" "\t%s %s\n" "\t%s %s\n"
	    "\t%s %s\n" "\t%s %s\n",
	    program_name, "[-P <path>] status",
	    program_name, "[-P <path>] start(or enable)",
	    program_name, "[-P <path>] stop(or disable)",
	    program_name, "[-P <path>] remove status",
	    program_name, "[-P <path>] remove enable",
	    program_name, "[-P <path>] remove disable",
	    program_name, "[-P <path>] reduced_log status",
	    program_name, "[-P <path>] reduced_log enable",
	    program_name, "[-P <path>] reduced_log disable",
	    program_name, "[-P <path>] remove_grace_used_space_ratio status",
	    program_name, "[-P <path>] remove_grace_used_space_ratio PERCENT",
	    program_name, "[-P <path>] remove_grace_time status",
	    program_name, "[-P <path>] remove_grace_time SECOND",
	    program_name, "[-P <path>] host_down_thresh status",
	    program_name, "[-P <path>] host_down_thresh SECOND",
	    program_name, "[-P <path>] sleep_time status",
	    program_name, "[-P <path>] sleep_time NANOSECOND",
	    program_name, "[-P <path>] minimum_interval status",
	    program_name, "[-P <path>] minimum_interval SECOND");
	exit(EXIT_FAILURE);
}

enum target { START, STOP, STATUS1,
	      REMOVE, REDUCED_LOG,
	      REMOVE_GRACE_USED_SPACE_RATIO, REMOVE_GRACE_TIME,
	      HOST_DOWN_THRESH, SLEEP_TIME, MINIMUM_INTERVAL };
enum value { ENABLE, DISABLE, STATUS2, SET_VALUE };

#define BUFSIZE 2048

static gfarm_error_t
config_get(struct gfm_connection *gfm_server, enum target target)
{
	gfarm_error_t e;
	char buffer[BUFSIZE];
	char config[BUFSIZE];

	switch (target) {
	case REMOVE_GRACE_USED_SPACE_RATIO:
		snprintf(config, sizeof config,
		    "replica_check_remove_grace_used_space_ratio");
		break;
	case REMOVE_GRACE_TIME:
		snprintf(config, sizeof config,
		    "replica_check_remove_grace_time");
		break;
	case HOST_DOWN_THRESH:
		snprintf(config, sizeof config,
		    "replica_check_host_down_thresh");
		break;
	case SLEEP_TIME:
		snprintf(config, sizeof config,
		    "replica_check_sleep_time");
		break;
	case MINIMUM_INTERVAL:
		snprintf(config, sizeof config,
		    "replica_check_minimum_interval");
		break;
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		return (e);
	}
	e = gfm_client_config_name_to_string(gfm_server,
			    config, buffer, sizeof buffer);
	if (e == GFARM_ERR_NO_ERROR)
		printf("%s\n", buffer);
	return (e);
}

static gfarm_error_t
config_set(struct gfm_connection *gfm_server, enum target target, char *value)
{
	gfarm_error_t e;
	char config[BUFSIZE];

	switch (target) {
	case REMOVE_GRACE_USED_SPACE_RATIO:
		snprintf(config, sizeof config,
		    "replica_check_remove_grace_used_space_ratio %s", value);
		break;
	case REMOVE_GRACE_TIME:
		snprintf(config, sizeof config,
		    "replica_check_remove_grace_time %s", value);
		break;
	case HOST_DOWN_THRESH:
		snprintf(config, sizeof config,
		    "replica_check_host_down_thresh %s", value);
		break;
	case SLEEP_TIME:
		snprintf(config, sizeof config,
		    "replica_check_sleep_time %s", value);
		break;
	case MINIMUM_INTERVAL:
		snprintf(config, sizeof config,
		    "replica_check_minimum_interval %s", value);
		break;
	default:
		e = GFARM_ERR_INVALID_ARGUMENT;
		return (e);
	}
	e = gfm_client_config_set_by_string(gfm_server, config);
	return (e);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int c;
	const char *path = ".";
	char *realpath = NULL;
	struct gfm_connection *gfm_server;
	enum target arg1 = STATUS1;
	enum value arg2 = STATUS2;
	char *arg2_str = NULL;
	int status;

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
		arg1 = START;
	else if (strcmp(*argv, "stop") == 0 || strcmp(*argv, "disable") == 0)
		arg1 = STOP;
	else if (strcmp(*argv, "status") == 0)
		arg1 = STATUS1;
	else if (strcmp(*argv, "remove") == 0)
		arg1 = REMOVE;
	else if (strcmp(*argv, "reduced_log") == 0)
		arg1 = REDUCED_LOG;
	else if (strcmp(*argv, "remove_grace_used_space_ratio") == 0)
		arg1 = REMOVE_GRACE_USED_SPACE_RATIO;
	else if (strcmp(*argv, "remove_grace_time") == 0)
		arg1 = REMOVE_GRACE_TIME;
	else if (strcmp(*argv, "host_down_thresh") == 0)
		arg1 = HOST_DOWN_THRESH;
	else if (strcmp(*argv, "sleep_time") == 0)
		arg1 = SLEEP_TIME;
	else if (strcmp(*argv, "minimum_interval") == 0)
		arg1 = MINIMUM_INTERVAL;
	else
		usage();

	if (arg1 == START || arg1 == STOP || arg1 == STATUS1) {
		if (argc != 1)
			usage();
	} else {
		if (argc != 2)
			usage();
		argv++;
		arg2_str = *argv;
		if (strcmp(*argv, "enable") == 0)
			arg2 = ENABLE;
		else if (strcmp(*argv, "disable") == 0)
			arg2 = DISABLE;
		else if (strcmp(*argv, "status") == 0)
			arg2 = STATUS2;
		else if (strcmp(*argv, "") == 0)
			usage();
		else
			arg2 = SET_VALUE;
	}

	if (gfarm_realpath_by_gfarm2fs(path, &realpath) == GFARM_ERR_NO_ERROR)
		path = realpath;
	e = gfm_client_connection_and_process_acquire_by_path(
		path, &gfm_server);
	error_check(path, e);

	switch (arg1) {
	case START:
		e = gfm_client_replica_check_ctrl_start(gfm_server);
		break;
	case STOP:
		e = gfm_client_replica_check_ctrl_stop(gfm_server);
		break;
	case STATUS1:
		e = gfm_client_replica_check_status_mainctrl(
		    gfm_server, &status);
		if (e == GFARM_ERR_NO_ERROR)
			printf("%s\n",
			    gfm_client_replica_check_status_string(status));
		break;
	case REMOVE:
		switch (arg2) {
		case ENABLE:
			e = gfm_client_replica_check_ctrl_remove_enable(
			    gfm_server);
			break;
		case DISABLE:
			e = gfm_client_replica_check_ctrl_remove_disable(
			    gfm_server);
			break;
		case STATUS2:
			e = gfm_client_replica_check_status_remove(
			    gfm_server, &status);
			if (e == GFARM_ERR_NO_ERROR)
				printf("%s\n",
				    gfm_client_replica_check_status_string(
					status));
			break;
		default:
			usage();
		}
		break;
	case REDUCED_LOG:
		switch (arg2) {
		case ENABLE:
			e = gfm_client_replica_check_ctrl_reduced_log_enable(
			    gfm_server);
			break;
		case DISABLE:
			e = gfm_client_replica_check_ctrl_reduced_log_disable(
			    gfm_server);
			break;
		case STATUS2:
			e = gfm_client_replica_check_status_reduced_log(
			    gfm_server, &status);
			if (e == GFARM_ERR_NO_ERROR)
				printf("%s\n",
				    gfm_client_replica_check_status_string(
					status));
			break;
		default:
			usage();
		}
		break;
	default:
		switch (arg2) {
		case STATUS2:
			e = config_get(gfm_server, arg1);
			break;
		case SET_VALUE:
			e = config_set(gfm_server, arg1, arg2_str);
			error_check(arg2_str, e);
			e = GFARM_ERR_NO_ERROR; /* error_check()ed */
			break;
		default:
			usage();
		}

	}
	error_check(path, e);

	free(realpath);
	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

enum gfstatus_operation {
	OP_PRINT = '\0',
	OP_MODIFY = 'm',
	OP_LIST = 'l',
	OP_LIST_WITH_VALUE = 'L',
};

void
error_check(char *msg, gfarm_error_t e)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;

	fprintf(stderr, "%s: %s: %s\n",
	    program_name, msg, gfarm_error_string(e));
	exit(EXIT_FAILURE);
}

void
check_version(struct gfm_connection *gfm_server)
{
	static void *config_vars[] = {
		&gfarm_metadb_version_major,
		&gfarm_metadb_version_minor,
		&gfarm_metadb_version_teeny,
	};
	gfarm_error_t e;
	int n_config_vars = GFARM_ARRAY_LENGTH(config_vars);

	if ((e = gfm_client_config_get_vars_request(
	    gfm_server, n_config_vars, config_vars)) != GFARM_ERR_NO_ERROR)
		error_check("asking gfmd about protocol version", e);
	else if ((e = gfm_client_config_get_vars_result(
	    gfm_server, n_config_vars, config_vars)) != GFARM_ERR_NO_ERROR)
		error_check("asked gfmd about protocol version", e);
	else {
		int major_version = gfarm_version_major();
		int minor_version = gfarm_version_minor();
		int teeny_version = gfarm_version_teeny();

		if (gfarm_metadb_version_major < major_version ||
		    (gfarm_metadb_version_major == major_version &&
		     (gfarm_metadb_version_minor < minor_version ||
		      (gfarm_metadb_version_minor == minor_version &&
		       gfarm_metadb_version_teeny < teeny_version)))) {
			fprintf(stderr, "%s: warning: "
			    "gfmd version %s or later is expected, "
			    "but it's %d.%d.%d\n",
			    program_name, gfarm_version(),
			    gfarm_metadb_version_major,
			    gfarm_metadb_version_minor,
			    gfarm_metadb_version_teeny);
		}
#if 0 /* this is harmless, and not worth reporting */
		else if (gfarm_metadb_version_major != major_version ||
		     gfarm_metadb_version_minor != minor_version ||
		     gfarm_metadb_version_teeny != teeny_version)
			fprintf(stderr, "%s: info: "
			    "client version %s is older than "
			    "gfmd version %d.%d.%d\n",
			    program_name, gfarm_version(),
			    gfarm_metadb_version_major,
			    gfarm_metadb_version_minor,
			    gfarm_metadb_version_teeny);
#endif
	}
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

static gfarm_error_t
do_config(struct gfm_connection *gfm_server, char *config, int ask_gfmd)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (!ask_gfmd) {
		fprintf(stderr, "%s: %s: does not make sense "
		    "without -M option\n", program_name, config);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	e = gfm_client_config_set_by_string(gfm_server, config);
	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s: %s\n", program_name, config,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
show_config(struct gfm_connection *gfm_server, const char *config_name,
	int ask_gfmd, int print_config_name)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char buffer[2048];

	if (ask_gfmd) {
		e = gfm_client_config_name_to_string(gfm_server,
		    config_name, buffer, sizeof buffer);
	} else {
		e = gfarm_config_local_name_to_string(
		    config_name, buffer, sizeof buffer);
	}
	if (e == GFARM_ERR_NO_ERROR) {
		if (print_config_name)
			printf("%s: %s\n", config_name, buffer);
		else
			printf("%s\n", buffer);
	} else if (e == GFARM_ERR_OPERATION_NOT_PERMITTED) {
		fprintf(stderr, "%s: %s: not available%s\n",
		    program_name, config_name, ask_gfmd ? " in gfmd" : "");
	} else {
		fprintf(stderr, "%s: %s: %s\n",
		    program_name, config_name, gfarm_error_string(e));
	}
	return (e);
}

struct each_config_name_callback_closure {
	struct gfm_connection *gfm_server;
	enum gfstatus_operation op;
	int ask_gfmd;
};

static gfarm_error_t
each_config_name_callback(void *closure, const char *config_name)
{
	struct each_config_name_callback_closure *c = closure;
	gfarm_error_t e;

	switch (c->op) {
	case OP_LIST:
		printf("%s\n", config_name);
		e = GFARM_ERR_NO_ERROR;
		break;
	case OP_LIST_WITH_VALUE:
		e = show_config(c->gfm_server, config_name, c->ask_gfmd, 1);
		break;
	default:
		e = GFARM_ERR_INTERNAL_ERROR;
		break;
	}
	return (e);
}

static gfarm_error_t
do_configurations(struct gfm_connection *gfm_server, int argc, char **argv,
	int ask_gfmd, enum gfstatus_operation op)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, print_config_name = (argc > 1);

	if (op == OP_LIST || op == OP_LIST_WITH_VALUE) {
		struct each_config_name_callback_closure c;

		if (argc > 0) {
			fprintf(stderr,
			    "%s: option -%c does not take any argument\n",
			    program_name, op);
			exit(1);
		}
		c.gfm_server = gfm_server;
		c.op = op;
		c.ask_gfmd = ask_gfmd;
		return (gfarm_config_name_foreach(
		    each_config_name_callback, &c,
		    ask_gfmd ?
		    GFARM_CONFIG_NAME_FLAG_FOR_METADB :
		    GFARM_CONFIG_NAME_FLAG_FOR_CLIENT));
	}

	for (i = 0; i < argc; i++) {
		switch (op) {
		case OP_MODIFY:
			e = do_config(gfm_server, argv[i], ask_gfmd);
			break;
		case OP_PRINT:
			e = show_config(gfm_server, argv[i], ask_gfmd,
			    print_config_name);
			break;
		default:
			e = GFARM_ERR_INTERNAL_ERROR;
			break;
		}
		if (e != GFARM_ERR_NO_ERROR && e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	return (e_save);
}

void
usage(void)
{
	fprintf(stderr,
	    "Usage:\t%s [-P <path>] [-d]\n"
	    "\t%s [-P <path>] [-d] [-M] <configuration_variable>...\n"
	    "\t%s [-P <path>] [-d] -Mm <configuration_directive>...\n"
	    "\t%s -V\n",
	    program_name, program_name, program_name, program_name);
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e, e2;
	int port, c, debug_mode = 0, ask_gfmd = 0;
	enum gfstatus_operation op = OP_PRINT;
	char *canonical_hostname, *hostname, *realpath = NULL;
	const char *user = NULL, *gfmd_hostname;
	const char *path = ".";
	struct gfm_connection *gfm_server = NULL;
	struct gfarm_metadb_server *ms;
#ifdef HAVE_GSI
	char *cred;
#endif

	if (argc > 0)
		program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "dlLmMP:V?"))
	    != -1) {
		switch (c) {
		case 'd':
			debug_mode = 1;
			gflog_set_priority_level(LOG_DEBUG);
			gflog_auth_set_verbose(1);
			break;
		case 'l':
		case 'L':
		case 'm':
			op = c;
			break;
		case 'M':
			ask_gfmd = 1;
			break;
		case 'P':
			path = optarg;
			break;
		case 'V':
			fprintf(stderr, "Gfarm version %s\n", gfarm_version());
			exit(0);
		case '?':
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc, &argv);
	error_check("gfarm_initialize", e);
	if (debug_mode) {
		/* set again since gfarm_initialize overwrites them */
		gflog_set_priority_level(LOG_DEBUG);
		gflog_auth_set_verbose(1);
	}

	if (argc <= 0 || ask_gfmd) {
		if (gfarm_realpath_by_gfarm2fs(path, &realpath)
		    == GFARM_ERR_NO_ERROR)
			path = realpath;
		if ((e = gfm_client_connection_and_process_acquire_by_path(
		    path, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			if ((e2 = gfarm_get_hostname_by_url(path,
			    &hostname, &port)) != GFARM_ERR_NO_ERROR)
				fprintf(stderr,
				    "%s: cannot get metadata server name"
				    " represented by `%s': %s\n", program_name,
				    path, gfarm_error_string(e2));
			else {
				fprintf(stderr,
				    "%s: metadata server `%s', port %d: %s\n",
				    program_name, hostname, port,
				    gfarm_error_string(e));
				free(hostname);
			}
			exit(EXIT_FAILURE);
		}
		user = gfm_client_username(gfm_server);
		check_version(gfm_server);
	}

	if (argc > 0 || op == OP_LIST || op == OP_LIST_WITH_VALUE) {
		e = do_configurations(gfm_server, argc, argv, ask_gfmd, op);
		e2 = gfarm_terminate();
		error_check("gfarm_terminate", e2);

		exit(e == GFARM_ERR_NO_ERROR ? 0 : 1);
	}

	print_msg("client version    ", gfarm_version());
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
			fprintf(stderr, "%s: cannot get metadata server name"
			    " represented by `%s': %s\n",
			    program_name, path, gfarm_error_string(e));
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
	printf("gfmd version    : %d.%d.%d\n",
	    gfarm_metadb_version_major,
	    gfarm_metadb_version_minor,
	    gfarm_metadb_version_teeny);

	gfm_client_connection_free(gfm_server);

	e = gfarm_terminate();
	error_check("gfarm_terminate", e);

	exit(0);
}

/*
 * $Id$
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "context.h"
#include "liberror.h"
#include "auth.h"
#include "gfpath.h"
#define GFARM_USE_STDIO
#include "config.h"

static void
gfarm_config_set_default_spool_on_server(void)
{
	if (gfarm_spool_root == NULL) {
		/* XXX - this case is not recommended. */
		gfarm_spool_root = GFARM_SPOOL_ROOT;
	}
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_config_read(void)
{
	gfarm_error_t e;
	int lineno;
	FILE *config;
	char *config_file = gfarm_config_get_filename();

	gfarm_init_config();
	if ((config = fopen(config_file, "r")) == NULL) {
		gflog_debug(GFARM_MSG_1000976,
			"open operation on server config file (%s) failed",
			config_file);
		return (GFARM_ERRMSG_CANNOT_OPEN_CONFIG);
	}
	e = gfarm_config_read_file(config, &lineno);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1000014, "%s: line %d: %s",
		    config_file, lineno, gfarm_error_string(e));
		return (e);
	}

	gfarm_config_set_default_ports();
	gfarm_config_set_default_misc();

	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_server_sig_debug(int sig)
{
	static int already_called = 0;
	pid_t pid;
	const char *message;
	int status;
	char **argv;
	switch (sig) {
	case SIGQUIT:
		message = "caught SIGQUIT\n";
		break;
	case SIGILL:
		message = "caught SIGILL\n";
		break;
	case SIGTRAP:
		message = "caught SIGTRAP\n";
		break;
	case SIGABRT:
		message = "caught SIGABRT\n";
		break;
	case SIGFPE:
		message = "caught SIGFPE\n";
		break;
	case SIGBUS:
		message = "caught SIGBUS\n";
		break;
	case SIGSEGV:
		message = "caught SIGSEGV\n";
		break;
	default:
		message = "caught a signal\n";
		break;
	}
	/* ignore return value, since there is no other way here */
	write(2, message, strlen(message));

	if (already_called)
		abort();
	already_called = 1;

	argv = gfarm_config_get_debug_command_argv();
	if (argv == NULL)
		_exit(1);

	pid = fork();
	if (pid == -1) {
		perror("fork"); /* XXX dangerous to call from signal handler */
		abort();
	} else if (pid == 0) {
		execvp(argv[0], argv);
		perror(argv[0]);
		_exit(1);
	} else {
		/* not really correct way to wait until attached, but... */
		sleep(5);
		waitpid(pid, &status, 0);
		_exit(1);
	}
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_initialize(char *config_file, int *argcp, char ***argvp)
{
	gfarm_error_t e;

	if ((e = gfarm_context_init()) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_context_init failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	gflog_initialize();
	if (argvp)
		gfarm_config_set_argv0(**argvp);

	if (config_file != NULL)
		gfarm_config_set_filename(config_file);
	e = gfarm_server_config_read();
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1000977,
			"gfarm_server_config_read() failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	signal(SIGQUIT, gfarm_server_sig_debug);
	signal(SIGILL,  gfarm_server_sig_debug);
	signal(SIGTRAP, gfarm_server_sig_debug);
	signal(SIGABRT, gfarm_server_sig_debug);
	signal(SIGFPE,  gfarm_server_sig_debug);
	signal(SIGBUS,  gfarm_server_sig_debug);
	signal(SIGSEGV, gfarm_server_sig_debug);

	gfarm_config_set_default_spool_on_server();

	return (GFARM_ERR_NO_ERROR);
}

/* the following function is for server. */
gfarm_error_t
gfarm_server_terminate(void)
{
	/* nothing to do (and also may never be called) */
	gflog_terminate();

	return (GFARM_ERR_NO_ERROR);
}

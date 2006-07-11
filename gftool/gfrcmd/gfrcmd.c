#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfs_client.h"
#include "auth.h"
#include "config.h"

char *program_name = "gfrcmd";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] host command...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-n: disable standard input.\n");
	fprintf(stderr, "\t-r: execute a remote command directly.\n");
	fprintf(stderr, "\t-y: inherits the environment variable DISPLAY.\n");
	fprintf(stderr, "\t-X: inherits the authentication info of the "
			"X Window System.\n");
#ifdef HAVE_GSI
	fprintf(stderr, "\t-N <hostname>: "
		"use this name to authenticate peer.\n");
	fprintf(stderr, "\t-v: display GSS minor status error.\n");
#endif
	exit(1);
}

char *opt_username; /* ignored for now */
int opt_no_stdin = 0;
int opt_raw_command = 0;
int opt_xdpy_env = 0;
int opt_xauth_copy = 0;
char *opt_hostname = NULL;
int opt_auth_verbose = 0;

void
parse_option(int *argcp, char ***argvp)
{
	int argc;
	char **argv, *s;

	argc = *argcp;
	argv = *argvp;
	for (; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
		for (s = &argv[0][1]; *s; s++) {
			switch (*s) {
			case 'l':
				/* XXX: FIXME. `opt_username' isn't used yet */
				if (s[1]) {
					opt_username = &s[1];
					s += strlen(s) - 1;
				} else if (argc > 1) {
					opt_username = argv[1];
					argc--, argv++;
					s = argv[0] + strlen(argv[0]) - 1;
				} else {
					usage();
				}
				break;
			case 'n':
				opt_no_stdin = 1;
				break;
			case 'r':
				opt_raw_command = 1;
				break;
			case 'y':
				opt_xdpy_env = 1;
				break;
			case 'X':
				opt_xauth_copy = 1;
				break;
			case 'N':
				if (s[1]) {
					opt_hostname = &s[1];
					s += strlen(s) - 1;
				} else if (argc > 1) {
					opt_hostname = argv[1];
					argc--, argv++;
					s = argv[0] + strlen(argv[0]) - 1;
				} else {
					usage();
				}
				break;
			case 'v':
				opt_auth_verbose = 1;
				break;
			case '?':
			default:
				usage();
			}
		}
	}
	*argcp = argc;
	*argvp = argv;
}

char *
concat(int argc, char **argv)
{
	int i, len = 0;
	char *s;

	for (i = 0; i < argc; i++)
		len += strlen(argv[i]);

	len += argc + 1;
	GFARM_MALLOC_ARRAY(s, len);
	if (s == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(1);
	}
	strcpy(s, argv[0]);
	for (i = 1; i < argc; i++) {
		strcat(s, " ");
		strcat(s, argv[i]);
	}
	return (s);
}

int passing_signals[] = { SIGINT, SIGQUIT, SIGTERM };
volatile int send_signal = 0;

void
record_signal(int sig)
{
	send_signal = sig;
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *e, *user, *if_hostname, *command;
	char *args[2];
	char *envs[2];
	char **argp, **envp = NULL;
	struct gfs_connection *gfs_server;
	int i, remote_pid, sig, status, coredump;
	struct hostent *hp;
	struct sockaddr_in peer_addr;

	if (argc <= 0)
		usage();

	program_name = basename(argv[0]);
	argc--, argv++;

	parse_option(&argc, &argv);
	if (argc <= 0)
		usage();

	if_hostname = argv[0];
	argc--, argv++;

	parse_option(&argc, &argv);
	if (argc <= 0)
		usage();

	e = getenv("GFARM_DEBUG_MODE");
	if (e != NULL && strcmp(e, "gdb") == 0) {
		opt_xauth_copy = 1;
		envs[0] = "GFARM_DEBUG_MODE=gdb";
		envs[1] = NULL;
		envp = envs;
	}

	if (opt_raw_command) {
		argp = argv;
		command = argv[0];
	} else {
		args[0] = concat(argc, argv);
		args[1] = NULL;
		argp = args;
		command = args[0];
	}

	/*
	 * Because gflog_auth_set_verbose() is called here, the call of
	 * gfarm_gsi_client_initialize() below may display verbose
	 * error messages, which will be displayed later again in
	 * gfarm_auth_request_gsi().
	 * But unless it's called here, some error messages (e.g. 
	 * errors in gfarmAuthInitialize()) won't be displayed at all.
	 * Thus, we go the noisy way, because that's better than nothing.
	 */
	if (opt_auth_verbose)
		gflog_auth_set_verbose(1);

	/*
	 * initialization
	 *
	 * XXX
	 * The reason we don't call gfarm_initialize() here is that
	 * we'd like to avoid overhead to access meta database.
	 * but we may have to access meta database eventually
	 * for GSI DN <-> gfarm global username mapping.
	 */
	gfarm_error_initialize();
	e = gfarm_set_local_user_for_this_local_account();
	if (e == NULL)
		e = gfarm_config_read();
#ifdef HAVE_GSI /* XXX this initialization must be removed eventually */
	if (e == NULL)
		(void)gfarm_gsi_client_initialize();
#endif
	if (e == NULL)
		e = gfarm_set_global_user_for_this_local_account();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	hp = gethostbyname(if_hostname);
	if (hp == NULL || hp->h_addrtype != AF_INET) {
		e = GFARM_ERR_UNKNOWN_HOST;
	} else {
		memset(&peer_addr, 0, sizeof(peer_addr));
		memcpy(&peer_addr.sin_addr, hp->h_addr,
		       sizeof(peer_addr.sin_addr));
		peer_addr.sin_family = hp->h_addrtype;
		peer_addr.sin_port = htons(gfarm_spool_server_port);
		e = gfs_client_connect(
		    opt_hostname != NULL ? opt_hostname : if_hostname, 
		    (struct sockaddr *)&peer_addr,
		    &gfs_server);
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", if_hostname, e);
		exit(1);
	}

	user = gfarm_get_global_username();
	/* XXX - kluge */
	e = gfs_client_mkdir(gfs_server, user, 0755);
	e = gfs_client_chdir(gfs_server, user);

	/* for gfs_client_command_send_signal() */
	for (i = 0; i < GFARM_ARRAY_LENGTH(passing_signals); i++) {
		struct sigaction sa;

		/*
		 * DO NOT set SA_RESTART here, because we rely on the fact
		 * that select(2) breaks with EINTR.
		 * XXX - This is not so portable. Use siglongjmp() instead?
		 */
		sa.sa_handler = record_signal;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(passing_signals[i], &sa, NULL);
	}

	e = gfs_client_command_request(gfs_server, command, argp, envp,
	    (opt_raw_command ? 0 : GFS_CLIENT_COMMAND_FLAG_SHELL_COMMAND) |
	    (opt_no_stdin ? GFS_CLIENT_COMMAND_FLAG_STDIN_EOF : 0) |
	    (opt_xdpy_env ? GFS_CLIENT_COMMAND_FLAG_XENVCOPY : 0) |
	    (opt_xauth_copy ? GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY : 0),
	    &remote_pid);
	if (e == NULL) {
		char *e2;

		while (gfs_client_command_is_running(gfs_server)) {
			e = gfs_client_command_io(gfs_server, NULL);
			if (e == NULL && send_signal != 0) {
				e = gfs_client_command_send_signal(
				    gfs_server, send_signal);
				send_signal = 0;
			}
		}
		e2 = gfs_client_command_result(gfs_server,
		    &sig, &status, &coredump);
		if (e == NULL)
			e = e2;
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		exit(1);
	}
	if (sig) {
		fprintf(stderr, "%s: signal %d received%s.\n",
		    if_hostname, sig, coredump ? " (core dumped)" : "");
		exit(255);
	}
	exit(status);
}

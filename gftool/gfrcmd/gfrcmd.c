#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include "gfarm.h"
#include "gfs_client.h"

char *program_name = "gfrcmd";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] host command...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-n: disable standard input.\n");
	exit(1);
}

char *opt_username; /* ignored for now */
int opt_no_stdin = 0;
int opt_xdpy_env = 0;
int opt_xauth_copy = 0;

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
			case 'y':
				opt_xdpy_env = 1;
				break;
			case 'X':
				opt_xauth_copy = 1;
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
	s = malloc(len);
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

int
main(argc, argv)
	int argc;
	char **argv;
{
	int argc_save = argc;
	char **argv_save = argv;
	char *e, *user, *home, *hostname, *command;
	char *args[4];
	char *envs[2];
	char **envp = NULL;
	int i, ch;
	struct gfs_connection *gfs_server;
	int sig, status, coredump;

	if (argc <= 0)
		usage();

	program_name = basename(argv[0]);
	argc--, argv++;

	parse_option(&argc, &argv);

	if (argc > 0) {
		hostname = argv[0];
		argc--, argv++;
	}

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

	command = concat(argc, argv);
	args[0] = "/bin/sh";
	args[1] = "-c";
	args[2] = command;
	args[3] = NULL;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	e = gfs_client_connection(hostname, &gfs_server);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", hostname, e);
		exit(1);
	}

	gfarm_user_home_get(&user, &home);
	/* XXX - kluge */
	e = gfs_client_mkdir(gfs_server, user);
	e = gfs_client_chdir(gfs_server, user);

	e = gfs_client_command(gfs_server, args[0], args, envp,
			       (opt_no_stdin ?
				GFS_CLIENT_COMMAND_FLAG_STDIN_EOF : 0) |
			       (opt_xdpy_env ?
				GFS_CLIENT_COMMAND_FLAG_XENVCOPY : 0) |
			       (opt_xauth_copy ?
				GFS_CLIENT_COMMAND_FLAG_XAUTHCOPY : 0),
			       &sig, &status, &coredump);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
	}
	if (sig) {
		fprintf(stderr, "%s: signal %d received%s.\n", hostname, sig,
			coredump ? " (core dumped)" : "");
		exit(255);
	}
	exit(status);
}

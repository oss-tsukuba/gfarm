/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfj_client.h"

char *program_name = "gfrun";

void
setsig(int signum, void (*handler)(int))
{
	struct sigaction act;

	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	/* do not set SA_RESTART to make interrupt at waitpid(2) */
	act.sa_flags = 0;
	if (sigaction(signum, &act, NULL) == -1) {
		fprintf(stderr, "%s: sigaction(%d): %s\n",
			program_name, signum, strerror(errno));
		exit(1);
	}
}

void
ignore_handler(int signum)
{
	/* do nothing */
}

void
sig_ignore(int signum)
{
	/* we don't use SIG_IGN to make it possible that child catch singals */
	setsig(signum, ignore_handler);
}

void
usage()
{
	fprintf(stderr,
		"Usage: %s [-n] [-l <login>]"
		" [-H <hostfile or Gfarm file>] command...\n",
		program_name);
	exit(1);
}

#define ENV_GFRUN_CMD	"GFRUN_CMD"
#define ENV_GFRUN_FLAGS	"GFRUN_FLAGS"

int
main(argc, argv)
	int argc;
	char **argv;
{
	gfarm_stringlist input_list, output_list, arg_list, option_list;
	int command_index, command_alist_index;
	int pid, status;
	int i, j, nhosts, job_id, nfrags, save_errno;
	char *e, **hosts;
	static char gfarm_prefix[] = "gfarm:";
#	define GFARM_PREFIX_LEN (sizeof(gfarm_prefix) - 1)
	char total_nodes[GFARM_INT32STRLEN], node_index[GFARM_INT32STRLEN];

	char *rsh_command, *rsh_flags;
	int have_gfarm_url_prefix = 1;
	char *hostfile = NULL, *scheduling_file;
	char *command_name, **delivered_paths = NULL;
	int have_redirect_stdio_option = 1;

	/*
	 * rsh_command
	 */
	rsh_command = getenv(ENV_GFRUN_CMD);
	if (rsh_command == NULL)
		rsh_command = "gfrcmd";

	/* For backward compatibility */
	if (argc >= 1) {
		program_name = basename(argv[0]);
		if (strcmp(program_name, "gfrsh") == 0) {
			rsh_command = "rsh";
		} else if (strcmp(program_name, "gfssh") == 0) {
			rsh_command = "ssh";
		} else if (strcmp(program_name, "gfrshl") == 0) {
			have_gfarm_url_prefix = 0;
		} else if (strcmp(program_name, "gfsshl") == 0) {
			rsh_command = "ssh";
			have_gfarm_url_prefix = 0;
		}
	}

	/* Globus-job-run hack */
	if (strcmp(rsh_command, "globus-job-run") == 0) {
		have_redirect_stdio_option = 0;
	}

	/*
	 * rsh_flags
	 *
	 * XXX - Currently, You can specify at most one flag.
	 */
	rsh_flags = getenv(ENV_GFRUN_FLAGS);
	if (rsh_flags == NULL) {
		if (have_redirect_stdio_option == 1)
			rsh_flags = "-n";
	}

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: gfarm initialize: %s\n", program_name, e);
		exit(1);
	}
	e = gfj_initialize();
	if (e != NULL) {
		fprintf(stderr, "%s: job manager: %s\n", program_name, e);
		exit(1);
	}
	gfarm_stringlist_init(&option_list);

	/*
	 * parse and skip/record options
	 */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			break;
		for (j = 1; argv[i][j] != '\0'; j++) {
			switch (argv[i][j]) {
			case 'H':
				if (j > 1) {
					argv[i][j] = '\0';
					gfarm_stringlist_add(&option_list,
						argv[i]);
				}
				if (argv[i][j + 1] != '\0') {
					hostfile = &argv[i][j + 1];
				} else if (++i < argc) {
					hostfile = argv[i];
				} else {
					fprintf(stderr, "%s: "
						"missing argument to -%c\n",
						program_name, argv[i - 1][j]);
					usage();
				}
				goto skip_opt;
			case 'K':
			case 'd':
			case 'n':
			case 'x':
				/* an option which doesn't have an argument */
				break;
			case 'k':
			case 'l':
				/* an option which does have an argument */
				if (argv[i][j + 1] != '\0')
					;
				else if (++i < argc) {
					gfarm_stringlist_add(&option_list,
						argv[i - 1]);
				} else {
					fprintf(stderr, "%s: "
						"missing argument to -%c\n",
						program_name, argv[i - 1][j]);
					usage();
				}
				goto record_opt;
			}
		}
record_opt:
		gfarm_stringlist_add(&option_list, argv[i]);
skip_opt: ;
	}
	command_index = i;
	if (command_index >= argc) /* no command name */
		usage();
	command_name = argv[command_index];

	gfarm_stringlist_init(&input_list);
	gfarm_stringlist_init(&output_list);
	for (i = command_index + 1; i < argc; i++) {
		if (strncmp(argv[i], gfarm_prefix, GFARM_PREFIX_LEN) == 0) {
			e = gfarm_url_fragment_number(argv[i], &nfrags);
			if (e == NULL) {
				gfarm_stringlist_add(&input_list, argv[i]);
			} else {
				gfarm_stringlist_add(&output_list, argv[i]);
			}
			if (!have_gfarm_url_prefix)
				argv[i] += GFARM_PREFIX_LEN;
		}
	}

	if (hostfile == NULL) {
		if (gfarm_stringlist_length(&input_list) == 0) {
			fprintf(stderr, "%s: no input file\n", program_name);
			exit(1);
		}
		/* XXX - this is only using first input file for scheduling */
		scheduling_file = gfarm_stringlist_elem(&input_list, 0);
		e = gfarm_url_hosts_schedule(scheduling_file, NULL,
		    &nhosts, &hosts);
		if (e != NULL) {
			fprintf(stderr, "%s: schedule: %s\n", program_name, e);
			exit(1);
		}
	} else if (gfarm_is_url(hostfile)) {
		/*
		 * If hostfile is a Gfarm file, schedule using the
		 * distribution of the Gfarm file.
		 */
		e = gfarm_url_hosts_schedule(hostfile, NULL,
					     &nhosts, &hosts);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", hostfile, e);
			exit(1);
		}
		scheduling_file = hostfile;
	} else {
		int error_line;

		e = gfarm_hostlist_read(hostfile,
					&nhosts, &hosts, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: %s: line %d: %s\n",
					program_name, hostfile, error_line, e);
			else
				fprintf(stderr, "%s: %s: %s\n",
					program_name, hostfile, e);
			exit(1);
		}
		scheduling_file = hostfile;
	}

	/*
	 * register job manager
	 */
	e = gfarm_user_job_register(nhosts, hosts, program_name,
	    scheduling_file, argc - command_index, &argv[command_index],
	    &job_id);
	if (e != NULL) {
		fprintf(stderr, "%s: job register: %s\n", program_name, e);
		exit(1);
	}

	/*
	 * deliver gfarm:program.
	 */
	if (strncmp(command_name, gfarm_prefix, GFARM_PREFIX_LEN) == 0) {
		e = gfarm_url_program_deliver(command_name, nhosts, hosts,
					      &delivered_paths);
		if (e != NULL) {
			fprintf(stderr, "%s: deliver %s: %s\n",
				program_name, command_name, e);
			exit(1);
		}
	}

	sprintf(total_nodes, "%d", nhosts);

	gfarm_stringlist_init(&arg_list);
	gfarm_stringlist_add(&arg_list, rsh_command);
	gfarm_stringlist_add(&arg_list, "(dummy)");
	if (rsh_flags != NULL)
		gfarm_stringlist_add(&arg_list, rsh_flags);
	gfarm_stringlist_add_list(&arg_list, &option_list);
	command_alist_index = gfarm_stringlist_length(&arg_list);
	gfarm_stringlist_add(&arg_list, "(dummy)");
	gfarm_stringlist_add(&arg_list, "-N");
	gfarm_stringlist_add(&arg_list, total_nodes);
	gfarm_stringlist_add(&arg_list, "-I");
	gfarm_stringlist_add(&arg_list, node_index);
	gfarm_stringlist_cat(&arg_list, &argv[command_index + 1]);
	gfarm_stringlist_add(&arg_list, NULL);

	for (i = 0; i < nhosts; i++) {
		sprintf(node_index, "%d", i);
		GFARM_STRINGLIST_ELEM(arg_list, 1) = hosts[i];
		if (delivered_paths == NULL) {
			GFARM_STRINGLIST_ELEM(arg_list, command_alist_index) =
			    command_name;
		} else {
			GFARM_STRINGLIST_ELEM(arg_list, command_alist_index) =
			    delivered_paths[i];
		}
		switch (pid = fork()) {
		case 0:
			execvp(rsh_command,
			    GFARM_STRINGLIST_STRARRAY(arg_list));
			perror(rsh_command);
			exit(1);
		case -1:
			perror("fork");
			exit(1);
		}
	}

	sig_ignore(SIGHUP);
	sig_ignore(SIGINT);
	sig_ignore(SIGQUIT);
	sig_ignore(SIGTERM);
	sig_ignore(SIGTSTP);
	while (waitpid(-1, &status, 0) != -1)
		;
	save_errno = errno;

	for (i = 0; i < gfarm_stringlist_length(&output_list); i++)
		gfarm_url_fragment_cleanup(
		    gfarm_stringlist_elem(&output_list, i), nhosts, hosts);

	if (delivered_paths != NULL)
		gfarm_strings_free_deeply(nhosts, delivered_paths);
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(&arg_list);
	gfarm_stringlist_free(&output_list);
	gfarm_stringlist_free(&input_list);
	gfarm_stringlist_free(&option_list);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (save_errno == ECHILD ? 0 : 1);
}

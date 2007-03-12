/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "host.h"
#include "config.h"
#include "gfj_client.h"
#include "schedule.h"

#define ENV_GFRUN_CMD	"GFRUN_CMD"
#define ENV_GFRUN_FLAGS	"GFRUN_FLAGS"

enum command_type {UNKNOWN_COMMAND = -1, USUAL_COMMAND = 0, GFARM_COMMAND = 1};

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
#ifdef HAVE_GSI
		"Usage: %s [-bghnuprvS] [-l <login>]\n"
#else		
		"Usage: %s [-bghnuprS] [-l <login>]\n"
#endif
		"\t[-G <Gfarm file>|-H <hostfile>|-N <number of hosts>] "
		"[-I <section>]\n"
		"\t[-o <Gfarm file>] [-e <Gfarm file>]"
		" command ...\n",
		program_name);
	exit(1);
}

struct gfrun_options {
	char *user_name;
	char *stdout_file;
	char *stderr_file;
	char *sched_file;	/* -G <sched_file> */
	char *hosts_file;	/* -H <hosts_file> */
	int nprocs;		/* -N <nprocs> */
	char *section;		/* -I <index> */
	enum command_type cmd_type;
	int authentication_verbose_mode;
	int profile;
	int replicate;
	int use_gfexec;
	int hook_global;
};

static void
default_gfrun_options(struct gfrun_options *options)
{
	options->user_name = NULL;
	options->stdout_file = NULL;
	options->stderr_file = NULL;
	options->sched_file = NULL;
	options->hosts_file = NULL;
	options->nprocs = 0;
	options->section = NULL;
	options->cmd_type = UNKNOWN_COMMAND;
	options->authentication_verbose_mode = 0;
	options->profile = 0;
	options->replicate = 0;
	options->use_gfexec = 1;
	options->hook_global = 0;
}

char *
gfrun(char *rsh_command, gfarm_stringlist *rsh_options,
	char *canonical_name_option, struct gfrun_options *options,
	int nhosts, char **hosts, char *cmd, char **argv)
{
	int i, save_errno, status;
	int base_alist_index, host_alist_index, command_alist_index;
	gfarm_stringlist arg_list;
	char total_nodes[GFARM_INT32STRLEN], node_index[GFARM_INT32STRLEN];
	char **delivered_paths = NULL, *e;
	enum command_type cmd_type_guess = USUAL_COMMAND, cmd_type;
	char *stdout_file = options->stdout_file;
	char *stderr_file = options->stderr_file;
	static char gfexec_command[] = "gfexec";

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	command_alist_index = 0;
#endif

	/*
	 * deliver gfarm:program.
	 */
	if (gfarm_is_url(cmd)) {
		if (!options->use_gfexec) {
			e = gfarm_url_program_deliver(cmd, nhosts, hosts,
				&delivered_paths);
			if (e != NULL) {
				fprintf(stderr, "%s: deliver %s: %s\n",
					program_name, cmd, e);
				return (e);
			}
		}
		cmd_type_guess = GFARM_COMMAND;
	}
	if (options->cmd_type == UNKNOWN_COMMAND)
		cmd_type = cmd_type_guess;
	else
		cmd_type = options->cmd_type;

	sprintf(total_nodes, "%d", nhosts);

	gfarm_stringlist_init(&arg_list);
	/* make room for "gfrcmd -N <canonical_hostname>" */
	gfarm_stringlist_add(&arg_list, "(dummy)");
	gfarm_stringlist_add(&arg_list, "(dummy)");
	gfarm_stringlist_add(&arg_list, "(dummy)");
	host_alist_index = gfarm_stringlist_length(&arg_list);
	gfarm_stringlist_add(&arg_list, "(dummy)");
	if (rsh_options != NULL)
		gfarm_stringlist_add_list(&arg_list, rsh_options);
	if (options->use_gfexec)
		gfarm_stringlist_add(&arg_list, gfexec_command);
	if (!options->use_gfexec) {
		command_alist_index = gfarm_stringlist_length(&arg_list);
		gfarm_stringlist_add(&arg_list, "(dummy)");
	}
	if (options->use_gfexec || cmd_type == GFARM_COMMAND) {
		char *cwd;

		gfarm_stringlist_add(&arg_list, "--gfarm_nfrags");
		gfarm_stringlist_add(&arg_list, total_nodes);
		gfarm_stringlist_add(&arg_list, "--gfarm_index");
		gfarm_stringlist_add(&arg_list, node_index);
		if (stdout_file != NULL) {
			gfarm_stringlist_add(&arg_list, "--gfarm_stdout");
			gfarm_stringlist_add(&arg_list, stdout_file);
		}
		if (stderr_file != NULL) {
			gfarm_stringlist_add(&arg_list, "--gfarm_stderr");
			gfarm_stringlist_add(&arg_list, stderr_file);
		}
		if (options->profile)
			gfarm_stringlist_add(&arg_list, "--gfarm_profile");
		if (options->replicate)
			gfarm_stringlist_add(&arg_list, "--gfarm_replicate");
		if (options->hook_global)
			gfarm_stringlist_add(&arg_list, "--gfarm_hook_global");
		cwd = getenv("GFS_PWD");
		if (cwd != NULL) {
			gfarm_stringlist_add(&arg_list, "--gfarm_cwd");
			gfarm_stringlist_add(&arg_list, cwd);
		}
	}
	if (options->use_gfexec) {
		command_alist_index = gfarm_stringlist_length(&arg_list);
		gfarm_stringlist_add(&arg_list, "(dummy)");
	}
	gfarm_stringlist_cat(&arg_list, argv);
	gfarm_stringlist_add(&arg_list, NULL);

	for (i = 0; i < nhosts; i++) {
		char *if_hostname;
		struct sockaddr peer_addr;

		if (options->section != NULL && nhosts == 1) {
			/* Serial execution case with section name */
			int nfrags;

			sprintf(node_index, "%d", atoi(options->section));
			if (options->sched_file != NULL) {
				e = gfarm_url_fragment_number(
					options->sched_file, &nfrags);
				if (e != NULL)
					return (e);
				sprintf(total_nodes, "%d", nfrags);
			}
			else if (options->nprocs > 0)
				sprintf(total_nodes, "%d", options->nprocs);
		}
		else
			sprintf(node_index, "%d", i);

		/* reflect "address_use" directive in the `if_hostname'  */
		e = gfarm_host_address_get(hosts[i],
		    gfarm_spool_server_port, &peer_addr, &if_hostname);

		if (e != NULL) {
			GFARM_STRINGLIST_ELEM(arg_list, host_alist_index) =
			    hosts[i];
			base_alist_index = 2;
		} else {
			GFARM_STRINGLIST_ELEM(arg_list, host_alist_index) =
			    if_hostname;
			if (canonical_name_option == NULL ||
			    strcmp(hosts[i], if_hostname) == 0) {
				base_alist_index = 2;
			} else {
				base_alist_index = 0;
				GFARM_STRINGLIST_ELEM(arg_list, 1) =
				    canonical_name_option;
				GFARM_STRINGLIST_ELEM(arg_list, 2) =
				    hosts[i];
			}
		}
		GFARM_STRINGLIST_ELEM(arg_list, base_alist_index) =
		    rsh_command;

		if (delivered_paths == NULL) {
			GFARM_STRINGLIST_ELEM(arg_list, command_alist_index) =
			    cmd;
		} else {
			GFARM_STRINGLIST_ELEM(arg_list, command_alist_index) =
			    delivered_paths[i];
		}
		switch (fork()) {
		case 0:
			execvp(rsh_command,
			    GFARM_STRINGLIST_STRARRAY(arg_list) +
			    base_alist_index);
			perror(rsh_command);
			exit(1);
		case -1:
			perror("fork");
			exit(1);
		}
		if (e == NULL)
			free(if_hostname);
	}

	sig_ignore(SIGHUP);
	sig_ignore(SIGINT);
	sig_ignore(SIGQUIT);
	sig_ignore(SIGTERM);
	while (waitpid(-1, &status, 0) != -1 || errno == EINTR)
		;
	save_errno = errno;

	if (delivered_paths != NULL)
		gfarm_strings_free_deeply(nhosts, delivered_paths);
	gfarm_stringlist_free(&arg_list);

	if (save_errno != ECHILD) {
		e = gfarm_errno_to_error(save_errno);
		fprintf(stderr, "%s: waiting child process: %s\n",
		    program_name, e);
		return (e);
	}
	return (NULL);
}

/*
 * register files in gfarm spool
 */

void
register_stdout_stderr(char *stdout_file, char *stderr_file,
	char *rsh_command, gfarm_stringlist *rsh_options,
	char *canonical_name_option, int nhosts, char **hosts)
{
	char gfsplck_cmd[] = "gfarm:/bin/gfsplck";
	struct gfs_stat sb;
	char *e, *gfarm_files[3];
	int i = 0;

	/* purge the directory-tree cache. */
	gfs_uncachedir();

	if (stdout_file != NULL) {
		e = gfs_stat(stdout_file, &sb);
		if (e != NULL)
			gfarm_files[i++] = stdout_file;
		else
			gfs_stat_free(&sb);

	}
	if (stderr_file != NULL) {
		e = gfs_stat(stderr_file, &sb);
		if (e != NULL)
			gfarm_files[i++] = stderr_file;
		else
			gfs_stat_free(&sb);
	}
	gfarm_files[i] = NULL;

	if (i > 0) {
		struct gfrun_options options;

		e = gfs_stat(gfsplck_cmd, &sb);
		if (e != NULL) {
			fprintf(stderr,
			    "%s: cannot register a stdout/stderr file, "
			    "backend program %s: %s\n",
			    program_name, gfsplck_cmd, e);
			return;
		}
		gfs_stat_free(&sb);

		default_gfrun_options(&options);
		options.cmd_type = GFARM_COMMAND;

		e = gfrun(rsh_command, rsh_options, canonical_name_option,
		    &options, nhosts, hosts, gfsplck_cmd, gfarm_files);
		if (e != NULL)
			fprintf(stderr,
				"%s: cannot register a stdout file: "
				"%s\n", program_name, e);
	}
}

/* Process scheduling */
void
schedule(char *command_name, struct gfrun_options *options,
	gfarm_stringlist *input_list,
	int *nhostsp, char ***hostsp, char **scheduling_filep)
{
	char *e, *scheduling_file, **hosts;
	int error_line, nhosts;
	int spooled_command = gfarm_is_url(command_name);
	int nopts = 0;

	if (options->sched_file != NULL)
		nopts++;
	if (options->hosts_file != NULL)
		nopts++;
	if (options->nprocs > 0)
		nopts++;
	if (nopts > 1) {
		fprintf(stderr,
		    "%s: only one of -G/-H/-N option can be used at most.",
		    program_name);
		usage();
	}

	if (options->hosts_file != NULL) { /* Hostfile scheduling */
		if (options->section != NULL)
			fprintf(stderr, "%s: warning: -I option is ignored\n",
				program_name);
		/*
		 * Is it necessary to access a Gfarm hostfile?
		 */
		e = gfarm_hostlist_read(options->hosts_file,
		    &nhosts, &hosts, &error_line);
		if (e != NULL) {
			if (error_line != -1)
				fprintf(stderr, "%s: %s: line %d: %s\n",
				    program_name, options->hosts_file,
				    error_line, e);
			else
				fprintf(stderr, "%s: %s: %s\n",
				    program_name, options->hosts_file, e);
			exit(1);
		}
		scheduling_file = options->hosts_file;
	} else if (options->nprocs > 0) {
		if (options->section != NULL)
			/* schedule a process for specified section */
			nhosts = 1;
		else
			nhosts = options->nprocs;
		GFARM_MALLOC_ARRAY(hosts, nhosts);
		if (hosts == NULL) {
			fprintf(stderr, "%s: not enough memory for %d hosts",
			    program_name, nhosts);
			exit(1);
		}
		if (spooled_command) {
			e = gfarm_schedule_search_idle_by_program(
			    command_name, nhosts, hosts);
		} else {
			e = gfarm_schedule_search_idle_by_all(nhosts, hosts);
		}
		if (e != NULL) {
			fprintf(stderr, "%s: scheduling %d nodes: %s\n",
			    program_name, nhosts, e);
			exit(1);
		}
		scheduling_file = "";
	} else if (options->sched_file != NULL ||
		   gfarm_stringlist_length(input_list) != 0) {
		/*
		 * File-affinity scheduling
		 *
		 * If scheduling file is not explicitly specified, the
		 * first input file used for file-affinity scheduling.
		 */
		if (options->sched_file == NULL)
			options->sched_file =
				gfarm_stringlist_elem(input_list, 0);
		scheduling_file = options->sched_file;

		if (options->section != NULL) {
			/* schedule a process for specified section */
			char *gfarm_file;

			nhosts = 1;
			GFARM_MALLOC(hosts);
			if (hosts == NULL) {
				fprintf(stderr, "%s: not enough memory",
					program_name);
				exit(1);
			}
			e = gfarm_url_make_path(scheduling_file, &gfarm_file);
			if (e != NULL) {
				fprintf(stderr, "%s: %s", program_name, e);
				exit(1);
			}

			if (spooled_command)
				e = gfarm_file_section_host_schedule_by_program(
					gfarm_file, options->section,
					command_name, hosts);
			else
				e = gfarm_file_section_host_schedule(
					gfarm_file, options->section,
					hosts);
			free(gfarm_file);
		}
		else {
			if (spooled_command)
				e = gfarm_url_hosts_schedule_by_program(
					scheduling_file, command_name, NULL,
					&nhosts, &hosts);
			else
				e = gfarm_url_hosts_schedule(scheduling_file,
					NULL, &nhosts, &hosts);
		}
		if (e != NULL) {
			fprintf(stderr, "%s: scheduling by %s: %s\n",
			    program_name, scheduling_file, e);
			exit(1);
		}
	} else { /* Serial execution */
		nhosts = 1;
		GFARM_MALLOC(hosts);
		if (hosts == NULL) {
			fprintf(stderr, "%s: not enough memory", program_name);
			exit(1);
		}
		if (spooled_command)
			e = gfarm_schedule_search_idle_by_program(
				command_name, 1, hosts);
		else
			e= gfarm_schedule_search_idle_by_all(1, hosts);
		if (e != NULL) {
			fprintf(stderr, "%s: scheduling 1 host: %s\n",
				program_name, e);
			exit(1);
		}
		scheduling_file = "";
	}
	*nhostsp = nhosts;
	*hostsp = hosts;
	*scheduling_filep = scheduling_file;
}

/*
 * parse command line
 */

void
decide_rsh_command(char *program_name,
	char **rsh_commandp, gfarm_stringlist *rsh_options,
	char **canonical_name_optionp, int *remove_gfarm_url_prefixp)
{
	char *rsh_command, *rsh_flags, *base;
	char *canonical_name_option = NULL;
	int have_redirect_stdin_option = 1, remove_gfarm_url_prefix = 0;

	rsh_command = getenv(ENV_GFRUN_CMD);
	if (rsh_command == NULL)
		rsh_command = "gfrcmd";

	/* For backward compatibility */
	if (strcmp(program_name, "gfrsh") == 0) {
		rsh_command = "rsh";
	} else if (strcmp(program_name, "gfssh") == 0) {
		rsh_command = "ssh";
	} else if (strcmp(program_name, "gfrshl") == 0) {
		rsh_command = "rsh";
		remove_gfarm_url_prefix = 1;
	} else if (strcmp(program_name, "gfsshl") == 0) {
		rsh_command = "ssh";
		remove_gfarm_url_prefix = 1;
	}

	/* Hack */
	base = basename(rsh_command);
	if (strcmp(base, "gfrcmd") == 0)
		canonical_name_option = "-N";
	else if (strcmp(base, "globus-job-run") == 0)
		have_redirect_stdin_option = 0;

	/* $GFRUN_FLAGS: XXX - Currently, You can specify at most one flag */
	rsh_flags = getenv(ENV_GFRUN_FLAGS);
	if (rsh_flags != NULL)
		gfarm_stringlist_add(rsh_options, rsh_flags);
	else if (have_redirect_stdin_option)
		gfarm_stringlist_add(rsh_options, "-n");

	*rsh_commandp = rsh_command;
	*canonical_name_optionp = canonical_name_option;
	*remove_gfarm_url_prefixp = remove_gfarm_url_prefix;
}

void
missing_argument(char option)
{
	fprintf(stderr, "%s: missing argument to -%c\n", program_name, option);
	usage();
}

/*
 * Parse a gfrun option which does take a parameter.
 * The option itself will not be added to *rsh_options.
 *
 * INPUT:
 *	is_last_arg:	Is `arg' last argument of argv[]?
 *	arg:		currently looking entry of argv[]
 *	next_arg:	next entry of argv[]
 *	char_index:	arg[char_index] is the currently looking gfrun option
 * INPUT/OUTPUT:
 *	*rsh_options:	rsh options
 * OUTPUT:
 *	*parameterp:	parameter of the currently looking gfrun option
 * RETURN:
 *	true, if next argument is spent as the parameter of this option
 */
int
option_param(int is_last_arg, char *arg, char *next_arg, int char_index,
	gfarm_stringlist *rsh_options, char **parameterp)
{
	char optchar = arg[char_index];

	if (char_index > 1) {
		arg[char_index] = '\0';
		gfarm_stringlist_add(rsh_options, arg);
	}
	if (arg[char_index + 1] != '\0') {
		*parameterp = &arg[char_index + 1];
		return (0); /* don't skip next arg */
	} else if (!is_last_arg) {
		*parameterp = next_arg;
		return (1); /* skip next arg */
	} else {
		missing_argument(optchar);
		/* NOTREACHED */
	}
	return (0);
}

/*
 * Parse a rsh option which does take a parameter.
 * The option will be added to *rsh_options.
 *
 * INPUT:
 *	is_last_arg:	Is `arg' last argument of argv[]?
 *	arg:		currently looking entry of argv[]
 *	next_arg:	next entry of argv[]
 *	char_index:	arg[char_index] is the currently looking rsh option
 * INPUT/OUTPUT:
 *	*rsh_options:	rsh options
 * OUTPUT:
 *	*parameterp:	parameter of the currently looking rsh option
 * RETURN:
 *	true, if next argument is spent as the parameter of this option
 */
int
rsh_option_param(int is_last_arg, char *arg, char *next_arg, int char_index,
	gfarm_stringlist *rsh_options, char **parameterp)
{
	if (arg[char_index + 1] != '\0') {
		*parameterp = &arg[char_index + 1];
		gfarm_stringlist_add(rsh_options, arg);
		return (0); /* don't skip next arg */
	} else if (!is_last_arg) {
		*parameterp = next_arg;
		gfarm_stringlist_add(rsh_options, arg);
		gfarm_stringlist_add(rsh_options, next_arg);
		return (1); /* skip next arg */
	} else {
		missing_argument(arg[char_index]);
		/* NOTREACHED */
	}
	return (0);
}

/*
 * RETURN:
 *	true,	if this is the sole option in the arg,
 *		i.e. this option shouldn't be added to rsh_options
 */
int
remove_option(char *arg, int *char_indexp)
{
	int i = *char_indexp;

	if (i == 1 && arg[i + 1] == '\0')
		return (1); /* this is the sole option in the arg */
	memmove(&arg[i], &arg[i + 1], strlen(&arg[i]));
	*char_indexp = i - 1;
	return (0);
}

/*
 * RETURN:
 *	true, if next argument is spent as the parameter of this option.
 */
int
parse_option(int is_last_arg, char *arg, char *next_arg,
	gfarm_stringlist *rsh_options, struct gfrun_options *options)
{
	int i, skip_next;
	char *s;

	for (i = 1; arg[i] != '\0'; i++) {
		switch (arg[i]) {
		case 'b':
			options->hook_global = 1;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'h':
		case '?':
			usage();
		case 'g':
			options->cmd_type = GFARM_COMMAND;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'u':
			options->cmd_type = USUAL_COMMAND;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'v':
			options->authentication_verbose_mode = 1;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'p':
			options->profile = 1;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'r':
			options->replicate = 1;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'S':
			options->use_gfexec = 0;
			if (remove_option(arg, &i))
				return (0);
			break;
		case 'o':
			return (option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->stdout_file));
		case 'e':
			return (option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->stderr_file));
		case 'G':
			return (option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->sched_file));
		case 'H':
			return (option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->hosts_file));
		case 'I':
			return (option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->section));
		case 'N':
			skip_next = option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &s);
			options->nprocs = atoi(s);
			return (skip_next);
		case 'K': /* turn off kerberos */
		case 'd': /* turn on SO_DEBUG */
		case 'n': /* redirect input from /dev/null */
		case 'x': /* DES encrypt */
			/* an option which doesn't have an argument */
			break;
		case 'k': /* realm of kerberos */
			return (rsh_option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &s));
		case 'l': /* user name */
			return (rsh_option_param(is_last_arg, arg, next_arg, i,
			    rsh_options, &options->user_name));
		}
	}
	gfarm_stringlist_add(rsh_options, arg);
	return (0); /* don't skip next arg */
}

int
parse_options(int argc, char **argv,
	gfarm_stringlist *rsh_options, struct gfrun_options *options)
{
	int i;

	default_gfrun_options(options);

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			break;
		/* argv[argc] == NULL, thus this isn't out of bounds. */
		if (parse_option(i + 1 == argc, argv[i], argv[i + 1],
		    rsh_options, options))
			i++;
	}
	return (i);
}

int
main(int argc, char **argv)
{
	char *e, *e_save, *command_url;
	char **hosts, *rsh_command, *command_name, *scheduling_file;
	int i, command_index, nhosts, job_id, nfrags;
	struct gfrun_options options;
	gfarm_stringlist input_list, output_list, rsh_options;
	char *canonical_name_option;
	int remove_gfarm_url_prefix = 0, command_url_need_free;

	if (argc >= 1)
		program_name = basename(argv[0]);
	gfarm_stringlist_init(&rsh_options);
	decide_rsh_command(program_name, &rsh_command, &rsh_options,
	    &canonical_name_option, &remove_gfarm_url_prefix);
	command_index = parse_options(argc, argv, &rsh_options, &options);
	if (command_index >= argc) /* no command name */
		usage();
	command_name = argv[command_index];
	if (options.authentication_verbose_mode)
		gflog_auth_set_verbose(1);
	if ((e = gfarm_initialize(&argc, &argv)) != NULL) {
		fprintf(stderr, "%s: gfarm initialize: %s\n", program_name, e);
		exit(1);
	}
	if ((e = gfj_initialize()) != NULL) {
		fprintf(stderr, "%s: job manager: %s\n", program_name, e);
		exit(1);
	}

	gfarm_stringlist_init(&input_list);
	gfarm_stringlist_init(&output_list);
	for (i = command_index + 1; i < argc; i++) {
		if (gfarm_is_url(argv[i])) {
			e = gfarm_url_fragment_number(argv[i], &nfrags);
			if (e == NULL)
				gfarm_stringlist_add(&input_list, argv[i]);
			else
				gfarm_stringlist_add(&output_list, argv[i]);
			if (remove_gfarm_url_prefix)
				argv[i] += GFARM_URL_PREFIX_LENGTH;
		}
	}
	/* command name */
	e = gfs_realpath(command_name, &command_url);
	if (e == NULL) {
		struct gfs_stat gsb;

		e = gfs_stat(command_url, &gsb);
		if (e == NULL) {
			if (!GFARM_S_IS_PROGRAM(gsb.st_mode)) {
				fprintf(stderr, "%s: not an executable\n",
					command_url);
				gfs_stat_free(&gsb);
				free(command_url);
				exit(1);
			}
			gfs_stat_free(&gsb);
		}
		else {
			fprintf(stderr, "%s: %s\n", command_url, e);
			free(command_url);
			exit(1);
		}
		command_url_need_free = 1;
	}
	else {
		if (gfarm_is_url(command_name)) {
			fprintf(stderr, "%s: %s\n", command_name, e);
			exit(1);
		}
		/* not a command in Gfarm file system */
		command_url = command_name;
		command_url_need_free = 0;
	}
	schedule(command_url, &options, &input_list,
	    &nhosts, &hosts, &scheduling_file);

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

	e_save = gfrun(rsh_command, &rsh_options, canonical_name_option,
	    &options, nhosts, hosts,
	    command_url, &argv[command_index + 1]);
#if 0
	/*
	 * gfarm_terminate() should be called after the change of
	 * hooks_init.c on 2 March, 2005.  It is not necessary to call
	 * costly register_stdout_stderr() any more.
	 */
	if (e_save == NULL) {
		register_stdout_stderr(
		    options.stdout_file, options.stderr_file,
		    rsh_command, &rsh_options, canonical_name_option,
		    nhosts, hosts);
	}
#endif
#if 0 /* XXX - temporary solution; it is not necessary for the output
	 file to be the same number of fragments. */
	for (i = 0; i < gfarm_stringlist_length(&output_list); i++)
		gfarm_url_fragment_cleanup(
		    gfarm_stringlist_elem(&output_list, i), nhosts, hosts);
#endif
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(&output_list);
	gfarm_stringlist_free(&input_list);
	gfarm_stringlist_free(&rsh_options);
	if (command_url_need_free)
		free(command_url);
	if ((e = gfarm_terminate()) != NULL) {
		fprintf(stderr, "%s: gfarm terminate: %s\n", program_name, e);
		exit(1);
	}
	return (e_save == NULL ? 0 : 1);
}

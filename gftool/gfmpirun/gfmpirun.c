#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include "gfarm.h"

char *program_name = "gfmpirun";

void
setsig(int signum, void (*handler)(int))
{
	struct sigaction act;

	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	/* do not set SA_RESTART to make interrupt at waitpid(2) */
	act.sa_flags = SA_RESETHAND;
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
	setsig(signum, ignore_handler);
}

void
usage()
{
	fprintf(stderr,
		"Usage: %s [-H <hostfile>] [<mpirun_options>] command...\n",
		program_name);
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	gfarm_stringlist input_list, output_list, arg_list, option_list;
	int ilist_size, olist_size, alist_size, optlist_size;
	int command_index;
	int pid, status;
	int i, nhosts, nfrags, save_errno;
	char *e, **hosts;
	static char gfarm_prefix[] = "gfarm:";
#	define GFARM_PREFIX_LEN (sizeof(gfarm_prefix) - 1)
	static char template[] = "/tmp/mpXXXXXX";
	char filename[sizeof(template)];
	FILE *fp;
	char total_nodes[GFARM_INT32STRLEN];

	char *hostfile = NULL;
	char *command_name, **delivered_paths = NULL;

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	gfarm_stringlist_init(&optlist_size, &option_list);

	/*
	 * parse and skip/record options
	 */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			break;
		if (argv[i][1] == 'H') {
			if (argv[i][2] != '\0') {
				hostfile = &argv[i][2];
			} else if (++i < argc) {
				hostfile = argv[i];
			} else {
				fprintf(stderr, "%s: "
					"missing argument to %s\n",
					program_name, argv[i - 1]);
				usage();
			}
			goto skip_opt;
		}
		if (strcmp(argv[i], "-arch") == 0 ||
		    strcmp(argv[i], "-np") == 0 ||
		    strcmp(argv[i], "-stdin") == 0 ||
		    strcmp(argv[i], "-stdout") == 0 ||
		    strcmp(argv[i], "-stderr") == 0 ||
		    strcmp(argv[i], "-nexuspg") == 0 ||
		    strcmp(argv[i], "-nexusdb") == 0 ||
		    strcmp(argv[i], "-p4pg") == 0 ||
		    strcmp(argv[i], "-tcppg") == 0 ||
		    strcmp(argv[i], "-p4ssport") == 0 ||
		    strcmp(argv[i], "-mvback") == 0 ||
		    strcmp(argv[i], "-maxtime") == 0 ||
		    strcmp(argv[i], "-mem") == 0 ||
		    strcmp(argv[i], "-cpu") == 0) {
			/* an option which does have an argument */
			if (++i >= argc) {
				fprintf(stderr, "%s: "
					"missing argument to %s\n",
					program_name, argv[i - 1]);
				usage();
			}
			gfarm_stringlist_add(&optlist_size, &option_list,
				argv[i - 1]);
		}
		gfarm_stringlist_add(&optlist_size, &option_list, argv[i]);
skip_opt:
	}
	command_index = i;
	if (command_index >= argc) /* no command name */
		usage();
	command_name = argv[command_index];

	gfarm_stringlist_init(&ilist_size, &input_list);
	gfarm_stringlist_init(&olist_size, &output_list);
	for (i = command_index + 1; i < argc; i++) {
		if (strncmp(argv[i], gfarm_prefix, GFARM_PREFIX_LEN) == 0) {
			e = gfarm_url_fragment_number(argv[i], &nfrags);
			if (e == NULL) {
				gfarm_stringlist_add(&ilist_size, &input_list,
						     argv[i]);
			} else {
				gfarm_stringlist_add(&olist_size, &output_list,
						     argv[i]);
			}
		}
	}

	if (hostfile == NULL) {
		if (gfarm_stringlist_length(input_list) == 0) {
			fprintf(stderr, "%s: no input file\n", program_name);
			exit(1);
		}
		/* XXX - this is only using first input file for scheduling */
		e = gfarm_url_hosts_schedule(input_list[0], NULL,
					     &nhosts, &hosts);

		fp = fdopen(mkstemp(strcpy(filename, template)), "w");
		for (i = 0; i < nhosts; i++)
			fprintf(fp, "%s\n", hosts[i]);
		fclose(fp);
		hostfile = filename;
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
	}

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

	gfarm_stringlist_init(&alist_size, &arg_list);
	gfarm_stringlist_add(&alist_size, &arg_list, "mpirun");
#if 1
	/*
	 * without this option, the machine which is running gfmpirun
	 * always becomes node 0. and total number of nodes becomes
	 * ``1 + total_nodesprocesses'' because this host will be included
	 * regardless whether machine file includes this host or not.
	 * XXX - this option is only available on mpich/p4.
	 */
	gfarm_stringlist_add(&alist_size, &arg_list, "-nolocal");
#endif
	gfarm_stringlist_add(&alist_size, &arg_list, "-machinefile");
	gfarm_stringlist_add(&alist_size, &arg_list, hostfile);
	gfarm_stringlist_add(&alist_size, &arg_list, "-np");
	gfarm_stringlist_add(&alist_size, &arg_list, total_nodes);
	gfarm_stringlist_cat(&alist_size, &arg_list, option_list);
	if (delivered_paths == NULL) {
		gfarm_stringlist_add(&alist_size, &arg_list, command_name);
	} else {
		/*
		 * XXX assumes that all nodes have same gfarm_root!
		 * XXX really broken.
		 */
		gfarm_stringlist_add(&alist_size,&arg_list,delivered_paths[0]);
	}
	gfarm_stringlist_cat(&alist_size, &arg_list, &argv[command_index + 1]);

	switch (pid = fork()) {
	case 0:
		execvp("mpirun", arg_list);
		perror("mpirun");
		exit(1);
	case -1:
		perror("fork");
		exit(1);
	}

	sig_ignore(SIGHUP);
	sig_ignore(SIGINT);
	sig_ignore(SIGQUIT);
	sig_ignore(SIGTERM);
	sig_ignore(SIGTSTP);
	while (waitpid(-1, &status, 0) != -1)
		;
	save_errno = errno;

	for (i = 0; output_list[i] != NULL; i++)
		gfarm_url_fragment_cleanup(output_list[i], nhosts, hosts);
	if (hostfile == filename)
		unlink(filename);

	if (delivered_paths != NULL)
		gfarm_strings_free_deeply(nhosts, delivered_paths);
	gfarm_strings_free_deeply(nhosts, hosts);
	gfarm_stringlist_free(alist_size, arg_list);
	gfarm_stringlist_free(olist_size, output_list);
	gfarm_stringlist_free(ilist_size, input_list);
	gfarm_stringlist_free(optlist_size, option_list);
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	return (save_errno != ECHILD ? 1 :
		WIFEXITED(status) ? WEXITSTATUS(status) : 1);
}

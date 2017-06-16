#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>

#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfj_client.h"
#include "auth.h"
#include "gfutil.h"

char *program_name = "gfps";

void
usage()
{
	fprintf(stderr, "Usage: %s [option] [<job_id>...]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-a\tlist all user\n");
	fprintf(stderr, "\t-l\tdetailed output\n");
#ifdef HAVE_GSI
	fprintf(stderr, "\t-v: display GSS minor status error.\n");
#endif
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern int optind;
	int ch, do_all = 0, do_detailed = 0;
	int opt_auth_verbose = 0;
	int n, i, j, *joblist;
	struct gfarm_job_info *info, *infos;
	char *e;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "alv?")) != -1) {
		switch (ch) {
		case 'a':
			do_all = 1;
			break;
		case 'l':
			do_detailed = 1;
			break;
		case 'v':
			opt_auth_verbose = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (opt_auth_verbose)
		gflog_auth_set_verbose(1);

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	e = gfj_initialize();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	if (argc > 0) {
		n = argc;
		GFARM_MALLOC_ARRAY(joblist, n);
		if (joblist == NULL) {
			fprintf(stderr, "%s: no memory\n", program_name);
			exit(1);
		}
		for (i = 0; i < argc; i++)
			joblist[i] = atoi(argv[i]);
	} else {
		e = gfj_client_list(gfarm_jobmanager_server,
				    do_all ? "" : gfarm_get_global_username(),
				    &n, &joblist);
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	GFARM_MALLOC_ARRAY(infos, n);
	if (infos == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(1);
	}
	e = gfj_client_info(gfarm_jobmanager_server,
			    n, joblist, infos);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	for (i = 0; i < n; i++) {
		info = &infos[i];
		/*
		 * infos[i] may be invalid when there is no process
		 * having the specified process id.
		 */
		if (info->user == NULL)
			continue;
		printf("%6d %8s@%-10.10s %-8s %4d", joblist[i],
		       info->user, info->originate_host,
		       info->job_type, info->total_nodes);
		for (j = 0; j < info->argc; j++)
			printf(" %s", info->argv[j]);
		printf("\n");
		if (do_detailed) {
			printf("\t");
			printf("%s", info->nodes[0].hostname);
			for (j = 1; j < info->total_nodes; j++)
				printf(", %s", info->nodes[j].hostname);
			printf("\n");
		}
	}
	gfarm_job_info_free_contents(infos, n);
	free(infos);
	free(joblist);

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}

	return (0);
}

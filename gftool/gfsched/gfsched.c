/*
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>

#include <gfarm/gfarm.h>

#include "liberror.h"
#include "gfm_client.h"
#include "gfm_schedule.h"
#include "schedule.h"
#include "gfarm_path.h"

/*
 *  Create a hostfile.
 *
 *  gfsched [-P <path>] [-D <domain>] [-n <number>] [-LMlw]
 *  gfsched  -f <file>  [-D <domain>] [-n <number>] [-LMclw]
 */

char *program_name = "gfsched";
char *default_opt_domain = "";

void
usage(void)
{
	fprintf(stderr, 
	    "Usage:\t%s [-P <path>] [-D <domain>] [-n <number>] [-LMlw]\n",
	    program_name);
	fprintf(stderr,
	          "\t%s  -f <file>  [-D <domain>] [-n <number>] [-LMclw]\n",
	    program_name);
	fprintf(stderr,
	    "options:\n");
	fprintf(stderr, "\t-L\t\tdo not check authentication\n");
	fprintf(stderr, "\t-M\t\tno client-side scheduling\n");
	fprintf(stderr, "\t-c\t\tcreate mode (currently leaves a file)\n");
	fprintf(stderr, "\t-w\t\twrite mode\n");
	fprintf(stderr, "\t-l\t\tlong format\n");
	exit(2);
}

long
parse_opt_long(char *option, int option_char, char *argument_name)
{
	long value;
	char *s;

	errno = 0;
	value = strtol(option, &s, 0);
	if (s == option) {
		fprintf(stderr, "%s: missing %s after -%c\n",
		    program_name, argument_name, option_char);
		usage();
	} else if (*s != '\0') {
		fprintf(stderr, "%s: garbage in -%c %s\n",
		    program_name, option_char, option);
		usage();
	} else if (errno != 0 && (value == LONG_MIN || value == LONG_MAX)) {
		fprintf(stderr, "%s: %s with -%c %s\n",
		    program_name, strerror(errno), option_char, option);
		usage();
	}
	return (value);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	char *opt_domain = default_opt_domain;
	char *opt_mount_point = NULL;
	char *opt_file = NULL;
	int opt_metadata_only = 0;
	int opt_long_format = 0;
	int opt_nhosts = 0;
	int opt_write_mode = 0;
	int opt_create_mode = 0;
	int c, i, available_nhosts, nhosts, *ports;
	struct gfarm_host_sched_info *available_hosts;
	char *path, **hosts, *realpath = NULL;

	if (argc >= 1)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}

	while ((c = getopt(argc, argv, "D:LMP:cf:ln:w")) != -1) {
		switch (c) {
		case 'D':
			opt_domain = optarg;
			break;
		case 'L':
			gfarm_schedule_search_mode_use_loadavg();
			break;
		case 'M':
			opt_metadata_only = 1;
			break;
		case 'P':
			opt_mount_point = optarg;
			break;
		case 'c':
			opt_create_mode = 1;
			break;
		case 'f':
			opt_file = optarg;
			break;
		case 'l':
			opt_long_format = 1;
			break;
		case 'n':
			opt_nhosts = parse_opt_long(optarg, c, "<nhosts>");
			if (opt_nhosts <= 0) {
				fprintf(stderr, "%s: invalid value: -%c %d\n",
				    program_name, c, opt_nhosts);
				usage();
			}
			break;
		case 'w':
			opt_write_mode = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();

	if (opt_mount_point != NULL && opt_file != NULL) {
		fprintf(stderr,
		    "%s: -P and -f option cannot be specified at once.\n",
		    program_name);
		usage();
	}

	if (opt_file != NULL) {
		e = gfarm_realpath_by_gfarm2fs(opt_file, &realpath);
		if (e == GFARM_ERR_NO_ERROR)
			path = realpath;
		else
			path = opt_file;
		if (opt_create_mode) {
			GFS_File gf;

			e = gfs_pio_create(path, GFARM_FILE_WRONLY, 0666, &gf);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: creating \"%s\": %s\n",
				    program_name, path, gfarm_error_string(e));
				exit(1);
			}
			e = gfs_pio_close(gf);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "%s: gfs_pio_close(\"%s\"): %s\n",
				    program_name, path, gfarm_error_string(e));
				/* exit(1); */
			}
			/* NOTE: this may leave an empty file with ncopy==0 */
		}
		e = gfarm_schedule_hosts_domain_by_file(path,
		    opt_write_mode ? GFARM_FILE_RDWR : GFARM_FILE_RDONLY,
		    opt_domain,
		    &available_nhosts, &available_hosts);
	} else {
		path = opt_mount_point == NULL ? "." : opt_mount_point;
		e = gfarm_realpath_by_gfarm2fs(path, &realpath);
		if (e == GFARM_ERR_NO_ERROR)
			path = realpath;
		e = gfarm_schedule_hosts_domain_all(path, opt_domain,
		    &available_nhosts, &available_hosts);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: metadata scheduling: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	nhosts = opt_nhosts > 0 ? opt_nhosts : available_nhosts;
	GFARM_MALLOC_ARRAY(hosts, nhosts);
	GFARM_MALLOC_ARRAY(ports, nhosts);
	if (hosts == NULL || ports == NULL) {
		fprintf(stderr, "%s: cannot allocate memory for %d hosts.\n",
		    program_name, nhosts);
		exit(1);
	}

	if (opt_metadata_only) {
		if (nhosts > available_nhosts)
			nhosts = available_nhosts;
		for (i = 0; i < nhosts; i++) {
			hosts[i] = available_hosts[i].host;
			ports[i] = available_hosts[i].port;
		}
	} else if (opt_write_mode) {
		e = gfarm_schedule_hosts_acyclic_to_write(path,
		    available_nhosts, available_hosts,
		    &nhosts, hosts, ports);
	} else {
		e = gfarm_schedule_hosts_acyclic(path,
		    available_nhosts, available_hosts,
		    &nhosts, hosts, ports);
	}
	free(realpath);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: client side scheduling: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	if (nhosts == 0) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(GFARM_ERRMSG_NO_FILESYSTEM_NODE));
		exit(1);
	}
	for (i = 0; i < nhosts; i++) {
		printf("%s", hosts[i]);
		if (opt_long_format)
			printf("\t%d", ports[i]);
		putchar('\n');
	}
	free(hosts);
	free(ports);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}

	exit(0);
}

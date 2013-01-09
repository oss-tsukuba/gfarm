/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "auth.h"

char *program_name = "gfkey";

static void
write_hex(FILE *fp, void *buffer, size_t length)
{
	unsigned char *p = buffer;
	size_t i;

	for (i = 0; i < length; i++)
		fprintf(fp, "%02x", p[i]);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-c|-f] [-p <period>] "
	    "[-L <message-priority-level>]\n", program_name);
	fprintf(stderr, "       %s [-l|-e] [-L <message-priority-level>]\n",
	    program_name);

	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\t\tcreate a new key, if it doesn't exist or "
	    "it is expired\n");
	fprintf(stderr, "\t-f\t\tforce to create a new key\n");
	fprintf(stderr, "\t-p <period>\tspecify term of validity in second\n");
	fprintf(stderr, "\t-l\t\tlist the existing key\n");
	fprintf(stderr, "\t-e\t\treport the expiration time\n");
	fprintf(stderr, "\t-L <message-priority-level>\tmessage priority "
	    "level\n");
	exit(1);
}

void
exclusive(void)
{
	fprintf(stderr,
	    "%s: warning: -c option and -f are mutually exclusive\n",
	    program_name);
#if 0 /* XXX maybe we should exit here? although this is not fatal error. */
	exit(1);
#endif
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int ch, do_list = 0, do_expire_report = 0;
	gfarm_error_t e;
	char *home;
	unsigned int expire;
	char shared_key[GFARM_AUTH_SHARED_KEY_LEN];
	int mode = GFARM_AUTH_SHARED_KEY_GET;
	int period = -1;
	int log_level = -1;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "ceflp:L:?")) != -1) {
		switch (ch) {
		case 'c':
			if (mode == GFARM_AUTH_SHARED_KEY_CREATE_FORCE)
				exclusive();
			mode = GFARM_AUTH_SHARED_KEY_CREATE;
			break;
		case 'e':
			do_expire_report = 1;
			break;
		case 'f':
			if (mode == GFARM_AUTH_SHARED_KEY_CREATE)
				exclusive();
			mode = GFARM_AUTH_SHARED_KEY_CREATE_FORCE;
			break;
		case 'l':
			do_list = 1;
			break;
		case 'p':
			period = strtol(optarg, NULL, 0);
			break;
		case 'L':
			log_level = gflog_syslog_name_to_priority(optarg);
			if (log_level == -1) {
				fprintf(stderr,
					"-L %s: invalid priority level\n",
					optarg);
				exit(1);
			}
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0 ||
	    (mode == GFARM_AUTH_SHARED_KEY_GET &&
	     !do_list && !do_expire_report))
		usage();

	if (log_level != -1)
		gflog_set_priority_level(log_level);

	e = gfarm_set_local_user_for_this_local_account();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(1);
	}
	home = gfarm_get_local_homedir();

	e = gfarm_auth_shared_key_get(&expire, shared_key, home, NULL,
	    mode, period);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		exit(1);
	}
	if (do_list) {
		printf("%08x ", expire);
		write_hex(stdout, shared_key, GFARM_AUTH_SHARED_KEY_LEN);
		putc('\n', stdout);
	}
	if (do_expire_report) {
		time_t t = expire;

		printf("expiration time is %s", ctime(&t));
	}
	return (0);
}

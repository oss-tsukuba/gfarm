#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
#include <gfarm/gfarm_config.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
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
usage()
{
	fprintf(stderr, "Usage: %s [option]\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\tcreate new key, if doesn't exist or expired\n");
	fprintf(stderr, "\t-f\tforce to create new key\n");
	fprintf(stderr, "\t-l\tlist existing key\n");
	fprintf(stderr, "\t-e\treport expire time\n");
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	extern int optind;
	int ch, do_list = 0, do_expire_report = 0;
	char *e, *home;
	unsigned int expire;
	char shared_key[GFARM_AUTH_SHARED_KEY_LEN];
	int mode = GFARM_AUTH_SHARED_KEY_GET;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "cefl")) != -1) {
		switch (ch) {
		case 'c':
			mode = GFARM_AUTH_SHARED_KEY_CREATE;
			break;
		case 'e':
			do_expire_report = 1;
			break;
		case 'f':
			mode = GFARM_AUTH_SHARED_KEY_CREATE_FORCE;
			break;
		case 'l':
			do_list = 1;
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

	gfarm_error_initialize();
	e = gfarm_set_user_for_this_local_account();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", program_name, e);
		exit(1);
	}
	home = gfarm_get_local_homedir();

	e = gfarm_auth_shared_key_get(&expire, shared_key, home, mode);
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		exit(1);
	}
	if (do_list) {
		printf("%08x ", expire);
		write_hex(stdout, shared_key, GFARM_AUTH_SHARED_KEY_LEN);
		putc('\n', stdout);
	}
	if (do_expire_report) {
		time_t t = expire;

		printf("expire time is %s", ctime(&t));
	}
	return (0);
}

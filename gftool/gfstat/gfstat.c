/*
 * $Id$
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

/*
 * Display struct gfs_stat.
 */

void
display_stat(char *fn, struct gfs_stat *st)
{
	printf("  File: \"%s\"\n", fn);
	printf("  Size: %-12" PR_FILE_OFFSET " Filetype: ", st->st_size);
	switch (st->st_mode & GFARM_S_IFMT) {
	case GFARM_S_IFREG:
		puts("regular file");
		break;
	case GFARM_S_IFDIR:
		puts("directory");
		break;
	default:
		printf("unknown\n");
	}
	printf("  Num of sections: %d\n", st->st_nsections);
	printf("  Mode: (%04o) Uid: (%8s) Gid: (%8s)\n",
	       st->st_mode & GFARM_S_ALLPERM,
	       st->st_user, st->st_group);

	printf("Access: %s", ctime((time_t *)&st->st_atimespec.tv_sec));
	printf("Modify: %s", ctime((time_t *)&st->st_mtimespec.tv_sec));
	printf("Change: %s", ctime((time_t *)&st->st_ctimespec.tv_sec));
}

void
usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s file1 [file2 ...]\n",
		prog_name);
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = basename(argv[0]);
	char c, *e;
	extern int optind;
	int r = 0;

	while ((c = getopt(argc, argv, "h")) != -1) {
		switch (c) {
		case 'h':
		case '?':
		default:
			usage(prog_name);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage(prog_name);

	e = gfarm_initialize(&argc, &argv);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", prog_name, e);
		exit(1);
	}

	while (*argv) {
		struct gfs_stat st;
		char *url;

		e = gfs_realpath(*argv, &url);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", *argv, e);
			r = 1;
			++argv;
			continue;
		}

		e = gfs_stat(url, &st);
		if (e != NULL) {
			fprintf(stderr, "%s: %s\n", url, e);
			r = 1;
			free(url);
			++argv;
			continue;
		}
		display_stat(url, &st);

		gfs_stat_free(&st);
		free(url);
		++argv;
	}

	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", prog_name, e);
		exit(1);
	}

	exit(r);
}

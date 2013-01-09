/*
 * $Id$
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#include "gfm_client.h"
#include "lookup.h"

/*
 * Display struct gfs_stat.
 */

void
display_gfm_server(struct gfm_connection *gfm_server)
{
	printf("MetadataHost: %s\n", gfm_client_hostname(gfm_server));
	printf("MetadataPort: %d\n", gfm_client_port(gfm_server));
	printf("MetadataUser: %s\n", gfm_client_username(gfm_server));
}

void
display_stat(char *fn, struct gfs_stat *st)
{
	time_t clock;

	printf("  File: \"%s\"\n", fn);
	printf("  Size: %-12" GFARM_PRId64 " Filetype: ", st->st_size);
	switch (st->st_mode & GFARM_S_IFMT) {
	case GFARM_S_IFREG:
		puts("regular file");
		break;
	case GFARM_S_IFDIR:
		puts("directory");
		break;
	case GFARM_S_IFLNK:
		puts("symbolic link");
		break;
	default:
		printf("unknown\n");
	}
	printf("  Mode: (%04o) Uid: (%8s) Gid: (%8s)\n",
	       st->st_mode & GFARM_S_ALLPERM,
	       st->st_user, st->st_group);
	printf(" Inode: %-12" GFARM_PRId64 " Gen: %-12" GFARM_PRId64
	       " Links: %-12" GFARM_PRId64 "\n",
	       st->st_ino, st->st_gen, st->st_nlink);
	printf(" Ncopy: %-12" GFARM_PRId64 "\n", st->st_ncopy);

	clock = st->st_atimespec.tv_sec; printf("Access: %s", ctime(&clock));
	clock = st->st_mtimespec.tv_sec; printf("Modify: %s", ctime(&clock));
	clock = st->st_ctimespec.tv_sec; printf("Change: %s", ctime(&clock));
}

void
display_ncopy(char *fn, struct gfs_stat *st)
{
	printf("%" GFARM_PRId64 "\n", st->st_ncopy);
}

void
usage(char *prog_name)
{
	fprintf(stderr, "Usage: %s [option] file1 [file2 ...]\n",
		prog_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-M\t\tdisplay metadata server information too\n");
	fprintf(stderr, "\t-c\t\tdisplay number of file replicas only\n");
	fprintf(stderr, "\t-l\t\tshow symbolic links\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = argc > 0 ? basename(argv[0]) : "gfstat";
	gfarm_error_t e;
	extern int optind;
	int show_gfm_server = 0, show_ncopy_only = 0, show_symlink = 0;
	int c, first_entry = 1, r = 0;

	while ((c = getopt(argc, argv, "Mchl?")) != -1) {
		switch (c) {
		case 'M':
			show_gfm_server = 1;
			break;
		case 'c':
			show_ncopy_only = 1;
			break;
		case 'l':
			show_symlink = 1;
			break;
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
	if (show_gfm_server && show_ncopy_only) {
		fprintf(stderr, "%s: cannot specify -M and -c at once",
		    prog_name);
		usage(prog_name);
	}

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}

	for (; *argv; ++argv) {
		struct gfs_stat st;
		struct gfm_connection *gfm_server;

		if (show_symlink)
			e = gfs_lstat(*argv, &st);
		else
			e = gfs_stat(*argv, &st);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", *argv,
				gfarm_error_string(e));
			r = 1;
			continue;
		}

		gfm_server = NULL;
		if (show_gfm_server && (e = (*
		    (show_symlink
		    ? gfm_client_connection_and_process_acquire_by_path
		    : gfm_client_connection_and_process_acquire_by_path_follow)
					
		    )(*argv, &gfm_server)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: showing metadata server: %s",
			    *argv, gfarm_error_string(e));
			r = 1;
			continue;
		}

		if (show_ncopy_only) {
			display_ncopy(*argv, &st);
		} else {
			if (!first_entry)
				putchar('\n');
			display_stat(*argv, &st);
		}
		if (gfm_server != NULL) {
			display_gfm_server(gfm_server);
			gfm_client_connection_free(gfm_server);
		}

		gfs_stat_free(&st);
		first_entry = 0;
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}

	exit(r);
}

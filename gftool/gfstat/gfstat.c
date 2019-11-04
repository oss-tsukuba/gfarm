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
#include "gfarm_path.h"
#include "gfs_pio.h"

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

static void
display_time(const char *name, struct gfarm_timespec *ts)
{
#if 1	/* support nanosecond */
	char s[64];
	time_t t = ts->tv_sec;
	struct tm *tm = localtime(&t);

	strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", tm);
	printf("%s: %s.%09d", name, s, ts->tv_nsec);
	strftime(s, sizeof(s), "%z", tm);
	printf(" %s\n", s);
#else	/* old format */
	time_t t = ts->tv_sec;

	printf("%s: %s", name, ctime(&t));
#endif
}

void
display_stat(char *fn, struct gfs_stat *st)
{
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
	printf(" Inode: %-12" GFARM_PRId64 " Gen: %-12" GFARM_PRId64 "\n",
	       st->st_ino, st->st_gen);
	printf("       (%016llX%016llX)\n",
	       (long long)st->st_ino, (long long)st->st_gen);
	printf(" Links: %-12" GFARM_PRId64 " Ncopy: %-12" GFARM_PRId64 "\n",
	       st->st_nlink, st->st_ncopy);

	display_time("Access", &st->st_atimespec);
	display_time("Modify", &st->st_mtimespec);
	display_time("Change", &st->st_ctimespec);
}

void
display_ncopy(char *fn, struct gfs_stat *st)
{
	printf("%" GFARM_PRId64 "\n", st->st_ncopy);
}

gfarm_error_t
gfs_stat_realsize(char *path, char *host, struct gfs_stat *st)
{
	GFS_File gf;
	gfarm_error_t e;

	e = gfs_pio_open(path, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	if (host == NULL || (e = gfs_pio_internal_set_view_section(gf, host))
	    == GFARM_ERR_NO_ERROR)
		e = gfs_pio_stat(gf, st);
	(void)gfs_pio_close(gf);
	return (e);
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
	fprintf(stderr, "\t-r\t\tshow file size of file data\n");
	fprintf(stderr, "\t-h host\t\tspecify the file system node\n");
	exit(2);
}

/*
 *
 */

int
main(int argc, char *argv[])
{
	char *prog_name = argc > 0 ? basename(argv[0]) : "gfstat";
	char *host = NULL;
	gfarm_error_t e;
	int show_gfm_server = 0, show_ncopy_only = 0, show_symlink = 0;
	int show_realsize = 0;
	int c, first_entry = 1, r = 0;

	while ((c = getopt(argc, argv, "Mch:lr?")) != -1) {
		switch (c) {
		case 'M':
			show_gfm_server = 1;
			break;
		case 'c':
			show_ncopy_only = 1;
			break;
		case 'h':
			host = optarg;
			break;
		case 'l':
			show_symlink = 1;
			break;
		case 'r':
			show_realsize = 1;
			break;
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
		char *realpath = NULL;

		e = gfarm_realpath_by_gfarm2fs(*argv, &realpath);
		if (e == GFARM_ERR_NO_ERROR)
			*argv = realpath;
		if (show_symlink)
			e = gfs_lstat(*argv, &st);
		else if (show_realsize)
			e = gfs_stat_realsize(*argv, host, &st);
		else
			e = gfs_stat(*argv, &st);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", *argv,
				gfarm_error_string(e));
			free(realpath);
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
			free(realpath);
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
		free(realpath);
		first_entry = 0;
	}

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", prog_name, gfarm_error_string(e));
		exit(1);
	}

	exit(r);
}

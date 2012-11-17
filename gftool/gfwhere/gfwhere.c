#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <gfarm/gfarm.h>
#include "gfarm_foreach.h"
#include "gfarm_path.h"

char *program_name = "gfwhere";

struct options {
	int long_format;
	int type_suffix;

	int print_dead_host;
	int print_incomplete_copy;
	int print_dead_copy;

	int do_not_display_name;

	int flags;
};

static void
display_name(char *name)
{
	static int print_ln;

	if (print_ln)
		printf("\n");
	else
		print_ln = 1;

	printf("%s:\n", name);
}

void
display_replica(struct gfs_stat *st, struct gfs_replica_info *ri, int i,
	struct options *opt)
{
	int need_space;

	if (opt->long_format)
		putchar('\t');
	else if (i > 0)
		putchar(' ');

	fputs(gfs_replica_info_nth_host(ri, i), stdout);

	if (opt->type_suffix) {
		if (opt->print_dead_copy &&
		    gfs_replica_info_nth_is_dead_copy(ri, i))
			printf(";%llu",
			    (long long)gfs_replica_info_nth_gen(ri, i));
		if (gfs_replica_info_nth_is_incomplete(ri, i))
			putchar('?');
		if (gfs_replica_info_nth_is_dead_host(ri, i))
			putchar('-');
	}

	if (opt->long_format) {
		if (opt->print_dead_host ||
		    opt->print_incomplete_copy ||
		    opt->print_dead_copy)
			putchar('\t');
		if (opt->print_dead_copy)
			printf("%10llu\t", (unsigned long long)
			    gfs_replica_info_nth_gen(ri, i));
		need_space = 0;
		if (opt->print_dead_host) {
			putchar(gfs_replica_info_nth_is_dead_host(ri, i) ?
			    'd' : '-');
			need_space = 1;
		}
		if (opt->print_incomplete_copy) {
			if (need_space)
				putchar(' ');
			putchar(gfs_replica_info_nth_is_incomplete(ri, i) ?
			    'i' : '-');
			need_space = 1;
		}
		if (opt->print_dead_copy) {
			if (need_space)
				putchar(' ');
			putchar(gfs_replica_info_nth_is_dead_copy(ri, i) ?
			    'o' : '-');
		}
		putchar('\n');
	}
}

static gfarm_error_t
display_copy(char *path, struct gfs_stat *st, struct options *opt)
{
	gfarm_error_t e;
	int n, i;
	struct gfs_replica_info *ri;

	e = gfs_replica_info_by_name(path, opt->flags, &ri);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	if (!opt->do_not_display_name)
		display_name(path);

	n = gfs_replica_info_number(ri);
	for (i = 0; i < n; ++i) {
		display_replica(st, ri, i, opt);
	}
	printf("\n");

	gfs_replica_info_free(ri);

	return (e);
}

static gfarm_error_t
display_replica_catalog(char *path, struct gfs_stat *st, void *arg)
{
	gfarm_error_t e;
	struct options *opt = arg;
	gfarm_mode_t mode;

	mode = st->st_mode;
	if (GFARM_S_ISDIR(mode))
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (GFARM_S_ISLNK(mode))
		e = GFARM_ERR_IS_A_SYMBOLIC_LINK;
	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_NOT_A_REGULAR_FILE;
	else if (!opt->print_dead_host &&
		 !opt->print_incomplete_copy &&
		 !opt->print_dead_copy &&
		 st->st_ncopy == 0 && st->st_size > 0)
		/* XXX - GFARM_ERR_NO_REPLICA */
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = display_copy(path, st, opt);

	if (e != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", path, gfarm_error_string(e));
	return (e);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-a\t\tall replicas; equals -dio\n");
	fprintf(stderr, "\t-d\t\tdead filesystem node is displayed\n");
	fprintf(stderr, "\t-F\t\tappend indicator (-, ?, ;gen) to replicas\n");
	fprintf(stderr, "\t-i\t\tincomplete replica is displayed\n");
	fprintf(stderr, "\t-l\t\tlong format\n");
	fprintf(stderr, "\t-o\t\tobsolete replica is displayed\n");
	fprintf(stderr, "\t-r, -R\t\tdisplay subdirectories recursively\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, n, ch, opt_recursive = 0;
	struct options opt;
	gfarm_stringlist paths;
	gfs_glob_t types;

	if (argc >= 1)
		program_name = basename(argv[0]);

	opt.long_format = 0;
	opt.type_suffix = 0;
	opt.print_dead_host = 0;
	opt.print_incomplete_copy = 0;
	opt.print_dead_copy = 0;

	while ((ch = getopt(argc, argv, "adFilorR?")) != -1) {
		switch (ch) {
		case 'a':
			opt.print_dead_host =
			opt.print_incomplete_copy =
			opt.print_dead_copy = 1;
			break;
		case 'd':
			opt.print_dead_host = 1;
			break;
		case 'F':
			opt.type_suffix = 1;
			break;
		case 'i':
			opt.print_incomplete_copy = 1;
			break;
		case 'l':
			opt.long_format = 1;
			break;
		case 'o':
			opt.print_dead_copy = 1;
			break;
		case 'r':
		case 'R':
			opt_recursive = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	opt.flags = 0;
	if (opt.print_dead_host)
		opt.flags |= GFS_REPLICA_INFO_INCLUDING_DEAD_HOST;
	if (opt.print_incomplete_copy)
		opt.flags |= GFS_REPLICA_INFO_INCLUDING_INCOMPLETE_COPY;
	if (opt.print_dead_copy)
		opt.flags |= GFS_REPLICA_INFO_INCLUDING_DEAD_COPY;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if (argc == 0) {
		usage();
	}

	e = gfarm_stringlist_init(&paths);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = gfs_glob_init(&types);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < argc; i++)
		gfs_glob(argv[i], &paths, &types);
	gfs_glob_free(&types);

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *pi = NULL, *p = gfarm_stringlist_elem(&paths, i);
		struct gfs_stat st;

		opt.do_not_display_name = 0;
		e = gfarm_realpath_by_gfarm2fs(p, &pi);
		if (e == GFARM_ERR_NO_ERROR)
			p = pi;
		if ((e = gfs_lstat(p, &st)) != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: %s\n", p, gfarm_error_string(e));
		} else {
			if (GFARM_S_ISDIR(st.st_mode) && opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
				    display_replica_catalog, NULL, NULL,
				    p, &opt);
			else {
				opt.do_not_display_name = (n == 1);
				e = display_replica_catalog(p, &st, &opt);
			}
			gfs_stat_free(&st);
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(pi);
	}
	gfs_glob_free(&types);
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}

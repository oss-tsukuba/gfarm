#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>

#include <gfarm/gfarm.h>

#include "gfarm_foreach.h"
#include "gfarm_path.h"
#include "gfs_pio.h"

char *program_name = "gfcksum";
static int opt_verbose = 0;
static int opt_print_filename = 1;
static int opt_print_path = 0;

static gfarm_error_t
display_dir(char *p, struct gfs_stat *st, void *arg)
{
	static int print_ln = 0;

	if (print_ln)
		printf("\n");
	else
		print_ln = 1;

	printf("%s:\n", p);
	return (GFARM_ERR_NO_ERROR);
}

static void
display_cksum(char *p, struct gfs_stat_cksum *c)
{
	const char *name;

	if (opt_print_path)
		name = p;
	else
		name = gfarm_url_dir_skip(p);
	if (c == NULL || c->len == 0)
		printf("%s: no checksum\n", name);
	else
		printf("%.*s (%s) %d %s\n", (int)c->len, c->cksum, c->type,
		    c->flags, name);
}

static void
display_time(const char *name, struct gfarm_timespec *ts)
{
#define BUFSIZE	64
	char s[BUFSIZE];
	time_t t = ts->tv_sec;
	struct tm *tm = localtime(&t);

	strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", tm);
	printf("%s: %s.%09d", name, s, ts->tv_nsec);
	strftime(s, sizeof(s), "%z", tm);
	printf(" %s\n", s);
}

static gfarm_error_t
display_stat(GFS_File gf)
{
	gfarm_error_t e;
	struct gfs_stat st;

	e = gfs_pio_stat(gf, &st);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	display_time("Modify", &st.st_mtimespec);
	printf("\n");
	gfs_stat_free(&st);
	return (e);
}

static gfarm_error_t
stat_cksum(char *p, struct gfs_stat *st, void *arg)
{
	struct gfs_stat_cksum c;
	const char *b = gfarm_url_dir_skip(p);
	gfarm_error_t e;

	if (!GFARM_S_ISREG(st->st_mode))
		return (GFARM_ERR_NO_ERROR);
	if ((e = gfs_stat_cksum(p, &c)) != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", b, gfarm_error_string(e));
	else {
		display_cksum(p, &c);
		gfs_stat_cksum_free(&c);
	}
	return (e);
}

static int
cmp_size(GFS_File gf, struct gfs_stat *st2)
{
	struct gfs_stat st1;
	int r;

	if (gfs_pio_stat(gf, &st1) != GFARM_ERR_NO_ERROR)
		return (0);
	r = st1.st_size == st2->st_size;
	gfs_stat_free(&st1);
	return (r);
}

static int
cmp_cksum(struct gfs_stat_cksum *c1, struct gfs_stat_cksum *c2)
{
	if (c1->len == 0 || c2->len == 0)
		return (1);
	return (strcmp(c1->type, c2->type) == 0 && c1->len == c2->len &&
	    memcmp(c1->cksum, c2->cksum, c1->len) == 0);
}

static gfarm_error_t
calc_cksum(char *p, struct gfs_stat *st, void *arg)
{
	char *host = arg;
	struct gfs_stat_cksum c, c2;
	const char *b = gfarm_url_dir_skip(p);
	gfarm_error_t e, e2;
	GFS_File gf;

	if (!GFARM_S_ISREG(st->st_mode))
		return (GFARM_ERR_NO_ERROR);
	e = gfs_stat_cksum(p, &c2);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = gfs_pio_open(p, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		gfs_stat_cksum_free(&c2);
		return (e);
	}
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	if (host != NULL && (e = gfs_pio_internal_set_view_section(gf, host))
	    != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", host, gfarm_error_string(e));
	else if ((e = gfs_pio_cksum(gf, c2.type, &c)) != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", b, gfarm_error_string(e));
	else {
		display_cksum(p, &c);
		if (!cmp_cksum(&c, &c2)) {
			fprintf(stderr, "%s: %s differs\n", p, c.type);
			e = GFARM_ERR_INVALID_FILE_REPLICA;
		}
		if (!cmp_size(gf, st)) {
			fprintf(stderr, "%s: size differs\n", p);
			e = GFARM_ERR_INVALID_FILE_REPLICA;
		}
		if (opt_verbose)
			display_stat(gf);
		gfs_stat_cksum_free(&c);
	}
	e2 = gfs_pio_close(gf);
	gfs_stat_cksum_free(&c2);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

static gfarm_error_t
display_cksum_type(char *p, struct gfs_stat *st, int current_type)
{
	gfarm_error_t e;
	struct gfs_stat_cksum c;
	const char *type;

	if (!GFARM_S_ISREG(st->st_mode))
		return (GFARM_ERR_NO_ERROR);
	e = gfs_stat_cksum(p, &c);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	type = c.type;
	if (current_type && c.len == 0)
		type = "";
	if (opt_print_filename)
		printf("%s: %s\n", p, type);
	else
		printf("%s\n", type);
	e = type[0] == '\0' ? GFARM_ERR_NO_SUCH_OBJECT : GFARM_ERR_NO_ERROR;
	gfs_stat_cksum_free(&c);
	return (e);
}

static gfarm_error_t
query_cksum_calculation_type(char *p, struct gfs_stat *st, void *arg)
{
	return (display_cksum_type(p, st, 0));
}

static gfarm_error_t
query_cksum_type(char *p, struct gfs_stat *st, void *arg)
{
	return (display_cksum_type(p, st, 1));
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <path>...\n", program_name);
	fprintf(stderr, "       %s [option] -c <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\tcompute checksum\n");
	fprintf(stderr, "\t-h host\tspecify file system node\n");
	fprintf(stderr, "\t-l\tdisplay a path for each file\n");
	fprintf(stderr, "\t-r\tdisplay subdirectories recursively\n");
	fprintf(stderr, "\t-T\tdisplay which cksum type will be calculated\n");
	fprintf(stderr, "\t-t\tdisplay cksum type\n");
	fprintf(stderr, "\t-v\tverbose output\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	int argc_save = argc;
	char **argv_save = argv, *host = NULL;
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	int i, n, ch, opt_recursive = 0;
	gfarm_stringlist paths;
	gfs_glob_t types;
	gfarm_error_t (*op)(char *, struct gfs_stat *, void *) = stat_cksum;

	if (argc >= 1)
		program_name = basename(argv[0]);

	while ((ch = getopt(argc, argv, "ch:lrTtv?")) != -1) {
		switch (ch) {
		case 'c':
			op = calc_cksum;
			break;
		case 'h':
			host = optarg;
			break;
		case 'l':
			opt_print_path = 1;
			break;
		case 'r':
			opt_recursive = 1;
			break;
		case 'T':
			op = query_cksum_calculation_type;
			break;
		case 't':
			op = query_cksum_type;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	e = gfarm_initialize(&argc_save, &argv_save);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	if (argc == 0)
		usage();

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

	if (argc <= 1 && !opt_recursive && !opt_print_path &&
	    (op == query_cksum_calculation_type || op == query_cksum_type))
		opt_print_filename = 0;

	n = gfarm_stringlist_length(&paths);
	for (i = 0; i < n; i++) {
		char *pi = NULL, *p = gfarm_stringlist_elem(&paths, i);
		struct gfs_stat st;

		e = gfarm_realpath_by_gfarm2fs(p, &pi);
		if (e == GFARM_ERR_NO_ERROR)
			p = pi;
		if ((e = gfs_lstat(p, &st)) != GFARM_ERR_NO_ERROR)
			fprintf(stderr, "%s: %s\n", p, gfarm_error_string(e));
		else {
			if (GFARM_S_ISDIR(st.st_mode) && opt_recursive)
				e = gfarm_foreach_directory_hierarchy(
				    op, display_dir, NULL, p, host);
			else
				e = op(p, &st, host);
			gfs_stat_free(&st);
		}
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
		free(pi);
	}
	gfarm_stringlist_free_deeply(&paths);

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n", program_name,
		    gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	return (e_save == GFARM_ERR_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE);
}

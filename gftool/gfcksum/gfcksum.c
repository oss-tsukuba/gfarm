#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <gfarm/gfarm.h>

#include "queue.h" /* for gfs_file */

#include "gfarm_foreach.h"
#include "gfarm_path.h"

/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
#include <openssl/evp.h>
#include "gfs_pio.h"

char *program_name = "gfcksum";

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
	const char *b = gfarm_url_dir_skip(p);

	if (c == NULL || c->len == 0)
		printf("%s: no checksum\n", b);
	else
		printf("%.*s (%s) %d %s\n", c->len, c->cksum, c->type,
		    c->flags, b);
}

static gfarm_error_t
stat_cksum(char *p, struct gfs_stat *st, void *arg)
{
	struct gfs_stat_cksum c;
	const char *b = gfarm_url_dir_skip(p);
	gfarm_error_t e;

	if ((e = gfs_stat_cksum(p, &c)) != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", b, gfarm_error_string(e));
	else {
		display_cksum(p, &c);
		gfs_stat_cksum_free(&c);
	}
	return (e);
}

static gfarm_error_t
calc_cksum(char *p, struct gfs_stat *st, void *arg)
{
	char *host = arg;
	struct gfs_stat_cksum c;
	const char *b = gfarm_url_dir_skip(p);
	gfarm_error_t e, e2;
	GFS_File gf;

	e = gfs_pio_open(p, GFARM_FILE_RDONLY, &gf);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	/* XXX FIXME: INTERNAL FUNCTION SHOULD NOT BE USED */
	if (host != NULL && (e = gfs_pio_internal_set_view_section(gf, host))
	    != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", host, gfarm_error_string(e));
	else if ((e = gfs_pio_cksum(gf, "md5", &c)) != GFARM_ERR_NO_ERROR)
		fprintf(stderr, "%s: %s\n", b, gfarm_error_string(e));
	else {
		display_cksum(p, &c);
		gfs_stat_cksum_free(&c);
	}
	e2 = gfs_pio_close(gf);
	return (e != GFARM_ERR_NO_ERROR ? e : e2);
}

void
usage(void)
{
	fprintf(stderr, "Usage: %s [option] <path>...\n", program_name);
	fprintf(stderr, "       %s [option] -c <path>...\n", program_name);
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\tcompute checksum\n");
	fprintf(stderr, "\t-h host\tspecify file system node\n");
	fprintf(stderr, "\t-r\tdisplay subdirectories recursively\n");
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

	while ((ch = getopt(argc, argv, "ch:r?")) != -1) {
		switch (ch) {
		case 'c':
			op = calc_cksum;
			break;
		case 'h':
			host = optarg;
			break;
		case 'r':
			opt_recursive = 1;
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
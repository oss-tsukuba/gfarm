#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

#include <gfarm/gfarm.h>

#define EXIT_USAGE	250

char *program_name = "gfs_dir_test";

#define OP_READ_ALL	'A'
#define OP_READ		'R'
#define OP_SEEK		'S'
#define OP_TELL		'T'

struct op {
	unsigned char op;
	union param {
		gfarm_off_t off; /* for OP_SEEK */
	} u;
};

void
usage(void)
{
	fprintf(stderr, "Usage: %s [-ARSTelsv] <dirname>\n",
	    program_name);
	exit(EXIT_USAGE);
}

#define MAX_OPS	1024

struct op ops[MAX_OPS];
int nops = 0;

void
add_op(unsigned char op, union param *p)
{
	if (nops >= MAX_OPS) {
		fprintf(stderr,
		    "%s: number of operations reaches its limit (%d)\n",
		    program_name, MAX_OPS);
		usage();
	}
	ops[nops].op = op;
	if (p != NULL)
		ops[nops].u = *p;
	++nops;
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	GFS_Dir gd;
	int enable_caching = 0, long_format = 0, verbose = 0, silent_read = 0;
	int c, i, all;
	union param p;
	struct gfs_dirent *gde;
	gfarm_off_t roff;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	while ((c = getopt(argc, argv, "ARS:Telsv")) != -1) {
		switch (c) {
		case OP_READ_ALL:
			add_op(c, NULL);
			break;
		case OP_READ:
			add_op(c, NULL);
			break;
		case OP_SEEK:
			p.off = strtol(optarg, NULL, 0);
			add_op(c, &p);
			break;
		case OP_TELL:
			add_op(c, NULL);
			break;
		case 'e':
			enable_caching = 1;
			break;
		case 'l':
			long_format = 1;
			break;
		case 's':
			silent_read = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			fprintf(stderr, "%s: unknown option -%c\n",
			    program_name, c);
			usage();
		}
       }
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		fprintf(stderr, "%s: %s <filename>\n", program_name,
		    argc == 0 ? "missing" : "extra arguments after");
		usage();
	}

	e = (enable_caching ? gfs_opendir_caching : gfs_opendir)(argv[0], &gd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s(): %s\n",
		    (enable_caching ? "gfs_opendir_caching" : "gfs_opendir"),
		    gfarm_error_string(e));
		return (2);
	}
	if (verbose)
		fprintf(stderr, "%s(\"%s\")\n",
		    (enable_caching ? "gfs_opendir_caching" : "gfs_opendir"),
		    argv[0]);
	for (i = 0; i < nops; i++) {
		c = ops[i].op;
		all = 0;
		switch (c) {
		case OP_READ_ALL:
			all = 1;
			/*FALLTHROUGH*/
		case OP_READ:
			do {
				e = gfs_readdir(gd, &gde);
				if (e != GFARM_ERR_NO_ERROR) {
					fprintf(stderr, "gfs_readdir(): %s\n",
					    gfarm_error_string(e));
					return (c);
				}
				if (verbose)
					fprintf(stderr, "gfs_readdir():\n");
					/* print nothing */
				if (gde == NULL) {
					if (!silent_read)
						printf("/EOF/\n");
					break;
				}
				if (silent_read) {
					/* print nothing */
				} else if (long_format) {
					printf("%lld %d %d %s\n",
					    (long long)gde->d_fileno,
					    gde->d_reclen, gde->d_type,
					    gde->d_name);
				} else {
					printf("%s\n", gde->d_name);
				}
			} while (all);
			break;
		case OP_SEEK:
			e = gfs_seekdir(gd, ops[i].u.off);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr,
				    "gfs_seekdir(%lld): %s\n",
				    (long long)ops[i].u.off,
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_seekdir(%lld)\n",
				    (long long)ops[i].u.off);
			break;
		case OP_TELL:
			e = gfs_telldir(gd, &roff);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "gfs_telldir(): %s\n",
				    gfarm_error_string(e));
				return (c);
			}
			if (verbose)
				fprintf(stderr, "gfs_tell()\n");
			printf("%lld\n", (long long)roff);
			break;
		default:
			assert(0);
		}
	}
	e = gfs_closedir(gd);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfs_closedir(): %s\n", gfarm_error_string(e));
		return (3);
	}
	if (verbose)
		fprintf(stderr, "gfs_closedir()\n");

	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (4);
	}
	return (0);
}

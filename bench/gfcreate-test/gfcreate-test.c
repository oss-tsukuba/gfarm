/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libgen.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>

#include <gfarm/gfarm.h>

#include "gfarm_path.h"

char *program_name = "gfcreate-test";

#define BUFSIZE 1024
#define DEFAULT_NUM_DIR  100
#define DEFAULT_NUM_FILE 100
#define DEFAULT_NUM_SIZE 1024LL
#define DEFAULT_NUM_PARA 2

static int verbose = 0;

#define VERBOSE(...)						   \
	{							   \
		if (verbose) {					   \
			printf(__VA_ARGS__);			   \
			puts("");				   \
		}						   \
	}

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s [-v(verbose)] [-d num_dir(%d)] [-f num_file(%d)]\n"
	    "\t[-s file_size(%lld)] [-p num_parallel(%d)] new_directory\n",
	    program_name, DEFAULT_NUM_DIR, DEFAULT_NUM_FILE, DEFAULT_NUM_SIZE,
	    DEFAULT_NUM_PARA);
}

static gfarm_error_t
make_a_file(const char *gfpath, long long size)
{
	GFS_File gf;
	char buf[BUFSIZE];
	int bufsize, sz;
	gfarm_error_t e;

	e = gfs_pio_create(gfpath, GFARM_FILE_WRONLY,
	    0644 & GFARM_S_ALLPERM, &gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfs_pio_create(%s): %s\n",
		    program_name, gfpath, gfarm_error_string(e));
		return (e);
	}
	memset(buf, 0, BUFSIZE);
	while  (size > 0) {
		if (size >= BUFSIZE)
			bufsize = BUFSIZE;
		else
			bufsize = size;
		e = gfs_pio_write(gf, buf, bufsize, &sz);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfs_pio_write: %s\n",
				program_name, gfarm_error_string(e));
			break;
		}
		size -= sz;
	}
	e = gfs_pio_close(gf);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfs_pio_close: %s\n",
		    program_name, gfarm_error_string(e));
	}
	return (e);
}

static pid_t
make_files(const char *dir, int dir_start, int dir_end, int n_file,
    long long size)
{
	pid_t pid;

	if ((pid = fork()) == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		int i, j;
		char name[PATH_MAX];
		gfarm_error_t e;

		setvbuf(stdout, (char *) NULL, _IOLBF, 0);
		setvbuf(stderr, (char *) NULL, _IOLBF, 0);
		e = gfarm_initialize(NULL, NULL);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfarm_initialize: %s\n",
			    program_name,  gfarm_error_string(e));
			_exit(1);
		}
		for (i = dir_start; i <= dir_end; i++) {
			snprintf(name, sizeof(name), "%s/%05d", dir, i);
			e = gfs_mkdir(name, 0755);
			if (e != GFARM_ERR_NO_ERROR) {
				fprintf(stderr, "%s: gfs_mkdir(%s): %s\n",
				    program_name, dir, gfarm_error_string(e));
				_exit(1);
			}
			VERBOSE("DIRECTORY: %s", name);

			for (j = 0; j < n_file; j++) {
				snprintf(name, sizeof(name), "%s/%05d/%05d",
				    dir, i, j);
				e = make_a_file(name, size);
				if (e != GFARM_ERR_NO_ERROR)
					_exit(1);
				VERBOSE("FILE: %s", name);
			}
		}
		e = gfarm_terminate();
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s: gfarm_terminate: %s\n",
			    program_name, gfarm_error_string(e));
			_exit(1);
		}

		_exit(0);
	}
	return (pid);
}

int
main(int argc, char **argv)
{
	int c, i, dir_start, dir_end, range, range_rem;
	int n_dir = DEFAULT_NUM_DIR;
	int n_file = DEFAULT_NUM_FILE;
	long long size = DEFAULT_NUM_SIZE;
	int n_para = DEFAULT_NUM_PARA;
	char *dir, *dir_real = NULL;
	pid_t *pids;
	gfarm_error_t e;

	if (argc > 0)
		program_name = basename(argv[0]);

	while ((c = getopt(argc, argv, "d:f:p:s:hv?")) != -1) {
		switch (c) {
		case 'd':
			n_dir = atoi(optarg);
			break;
		case 'f':
			n_file = atoi(optarg);
			break;
		case 'p':
			n_para = atoi(optarg);
			break;
		case 's':
			size = atoll(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		return (0);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc <= 0) {
		usage();
		exit(EXIT_FAILURE);
	}
	dir = argv[0];

	e = gfarm_initialize(NULL, NULL);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_initialize: %s\n",
		    program_name,  gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	e = gfarm_realpath_by_gfarm2fs(dir, &dir_real);
	if (e == GFARM_ERR_NO_ERROR)
		dir = dir_real;
	e = gfs_mkdir(dir, 0755);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfs_mkdir(%s): %s\n",
		    program_name, dir, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}
	VERBOSE("DIRECTORY: %s", dir);
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: gfarm_terminate: %s\n",
		    program_name, gfarm_error_string(e));
		exit(EXIT_FAILURE);
	}

	GFARM_MALLOC_ARRAY(pids, n_para);
	if (pids == NULL) {
		fprintf(stderr, "%s: no memory\n", program_name);
		exit(EXIT_FAILURE);
	}

	range = n_dir / n_para;
	range_rem = n_dir % n_para;
	dir_end = -1;
	i = 0;
	for (i = 0; i < n_para; i++) {
		int rem = 0;

		if (range_rem > 0) {
			rem = 1;
			range_rem--;
		}
		dir_start = dir_end + 1;
		dir_end = dir_start + range + rem - 1;
		pids[i] = make_files(dir, dir_start, dir_end, n_file, size);
	}
	free(dir_real);

	for (i = 0; i < n_para; i++) {
		int status;

		waitpid(pids[i], &status, 0);
	}
	free(pids);

	return (0);
}

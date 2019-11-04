#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

const char *program_name = "gfdirpath";

void
usage()
{
	fprintf(stderr, "Usage: %s inum gen\n", program_name);
	exit(2);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	char *path;
	GFS_Dir dir;

	if (argc > 0)
		program_name = basename(argv[0]);

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}
	--argc;
	++argv;

	if (argc != 2)
		usage();
	inum = atoll(argv[0]);
	gen = atoll(argv[1]);

	e = gfs_fhopendir(inum, gen, &dir);
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_fgetdirpath(dir, &path);
		if (e == GFARM_ERR_NO_ERROR) {
			printf("%s\n", path);
			free(path);
		}
		gfs_closedir(dir);
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s: %s\n",
		    program_name, gfarm_error_string(e));
		exit(1);
	}
	return (0);
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gfarm/gfarm.h>

/*
 *  Register a local file to the Gfarm Meta DB.
 *
 *  gfreg <local_filename> <gfarm_url>
 */

char *program_name = "gfreg";

void
usage()
{
    fprintf(stderr, "Usage: %s <local_filename> <gfarm_url>\n",
	    program_name);
    exit(1);
}

#define GFS_FILE_BUFSIZE 65536

char *
gfarm_url_fragment_register(char *gfarm_url, int index, int nfrags,
			    char *filename)
{
	char *e, *e_save = NULL;
	int fd;
	size_t rv;
	int length; /* XXX - should be size_t */
	GFS_File gf;
	struct stat s;
	char buffer[GFS_FILE_BUFSIZE];

	if (stat(filename, &s) == -1) {
		perror(filename);
		return "no such file or directory";
	}

	/*
	 * register the fragment
	 */
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror(filename);
		return "cannot open";
	}
	/* XXX - overwrite case */
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY,
	    s.st_mode & GFARM_S_ALLPERM, &gf);
	if (e != NULL) {
		close(fd);
		fprintf(stderr, "%s\n", e);
		return e;
	}
	e = gfs_pio_set_view_index(gf, nfrags, index, NULL, 0);
	if (e != NULL) {
		gfs_pio_close(gf);
		close(fd);
		fprintf(stderr, "%s\n", e);
		return e;
	}
	for (;;) {
		rv = read(fd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		/* XXX - partial write case ? */
		e = gfs_pio_write(gf, buffer, rv, &length);
		if (e != NULL)
			break;
	}
	e_save = e;
	e = gfs_pio_close(gf);
	close(fd);
	if (e_save != NULL) {
		fprintf(stderr, "%s\n", e_save);
		return e_save;
	}
	if (e != NULL) {
		fprintf(stderr, "%s\n", e);
		return e;
	}
	return e;
}

int
main(int argc, char *argv[])
{
    int argc_save = argc;
    char **argv_save = argv;
    char *filename, *gfarm_url;
    char *node_index = NULL;
    int total_nodes = -1;
    char *e = NULL, *architecture = NULL;
    struct stat s;
    extern char *optarg;
    extern int optind;
    int c;

    /*  Command options  */

    if (argc >= 1)
	program_name = argv[0];

    while ((c = getopt(argc, argv, "I:N:a:")) != -1) {
	switch (c) {
	case 'I':
	    node_index = optarg;
	    break;
	case 'N':
	    total_nodes = strtol(optarg, NULL, 0);
	    break;
	case 'a':
	    architecture = optarg;
	    break;
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
	fprintf(stderr,
		"%s: missing a local filename\n",
		program_name);
	usage();
	exit(1);
    }
    filename = argv[0];
    argc--;
    argv++;

    if (argc == 0) {
	fprintf(stderr,
		"%s: missing a Gfarm URL\n",
		program_name);
	usage();
	exit(1);
    }
    gfarm_url = argv[0];
    argc--;
    argv++;

    /* */

    e = gfarm_initialize(&argc_save, &argv_save);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    if (stat(filename, &s) == -1) {
	perror(filename);
	usage();
	exit(1);
    }

    if (S_ISREG(s.st_mode) &&
	(s.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0 && node_index == NULL) {
	if (architecture == NULL) {
	    char *self_name;

	    e = gfarm_host_get_canonical_self_name(&self_name);
	    if (e != NULL) {
		fprintf(stderr, "%s: %s\n", gfarm_host_get_self_name(), e);
		exit(1);
	    }
	    architecture = gfarm_host_info_get_architecture_by_host(self_name);
	}
	if (architecture == NULL) {
	    fprintf(stderr, "%s: missing -a <architecture> for %s\n",
		    program_name, gfarm_host_get_self_name());
	    usage();
	    exit(1);
	}
	if (total_nodes <= 0)
	    total_nodes = 1;
	e = gfarm_url_program_register(gfarm_url, architecture,
				       filename, total_nodes);
    } else {
	int index;
	if (node_index == NULL) {
	    fprintf(stderr, "%s: missing -I <Gfarm index>\n",
		    program_name);
	    usage();
	    exit(1);
	}
	if (total_nodes <= 0) {
	    fprintf(stderr, "%s: missing -N <total num of fragments>\n",
		    program_name);
	    usage();
	    exit(1);
	}
	index = strtol(node_index, NULL, 0);
	e = gfarm_url_fragment_register(gfarm_url, index,
					total_nodes, filename);
    }
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    e = gfarm_terminate();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    exit(0);
}

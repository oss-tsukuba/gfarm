#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gfarm/gfarm.h>

/*
 *  Register a local file to Gfarm filesystem
 *
 *  gfreg <local_filename> <gfarm_url>
 *
 *  $Id$
 */

char *program_name = "gfreg";

void
usage()
{
    fprintf(stderr, "Usage: %s [option] <local_filename> <gfarm_url>\n",
	    program_name);
    fprintf(stderr, "Register a local file to Gfarm filesystem.\n\n");
    fprintf(stderr, "option:\n");
    fprintf(stderr, "\t-I fragment-index\tspecify a fragment index\n");
    fprintf(stderr, "\t-N number\t\ttotal number of fragments\n");
    fprintf(stderr, "\t-a architecture\t\tspecify an architecture\n");
    fprintf(stderr, "\t-h hostname\t\tspecify a hostname\n");
    exit(1);
}

#define GFS_FILE_BUFSIZE 65536

char *
gfarm_url_fragment_register(char *gfarm_url, int index, char *hostname,
	int nfrags, char *filename)
{
	char *e, *e_save = NULL;
	int fd;
	size_t rv;
	int length; /* XXX - should be size_t */
	GFS_File gf;
	struct stat s;
	char buffer[GFS_FILE_BUFSIZE];

	if (stat(filename, &s) == -1)
		return "no such file or directory";

	/*
	 * register the fragment
	 */
	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return "cannot open";

	/* XXX - overwrite case */
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY,
		s.st_mode & GFARM_S_ALLPERM, &gf);
	if (e != NULL) {
		close(fd);
		return e;
	}
	e = gfs_pio_set_view_index(gf, nfrags, index, hostname, 0);
	if (e != NULL) {
		gfs_pio_close(gf);
		close(fd);
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

	if (e_save != NULL)
		return e_save;

	return e;
}

int
main(int argc, char *argv[])
{
    char *filename, *gfarm_url, *target_url;
    char *node_index = NULL, *hostname = NULL, *e;
    int total_nodes = -1;
    struct stat s;
    struct gfs_stat gs;
    extern char *optarg;
    extern int optind;
    static const char gfarm_prefix[] = "gfarm:";
    int c;

    e = gfarm_initialize(&argc, &argv);
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    /*  Command options  */

    while ((c = getopt(argc, argv, "I:N:a:h:")) != -1) {
	switch (c) {
	case 'I':
	case 'a':
	    node_index = optarg;
	    break;
	case 'N':
	    total_nodes = strtol(optarg, NULL, 0);
	    break;
	case 'h':
	    hostname = optarg;
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
    }
    filename = argv[0];
    argc--;
    argv++;

    if (argc == 0) {
	fprintf(stderr,
		"%s: missing a Gfarm URL\n",
		program_name);
	usage();
    }
    gfarm_url = argv[0];
    argc--;
    argv++;

    /* */

    if (stat(filename, &s) == -1) {
	perror(filename);
	exit(1);
    }
    if (!S_ISREG(s.st_mode)) {
	    fprintf(stderr, "%s: not a regular file\n", filename);
	    exit(1);
    }

    e = gfs_stat(gfarm_url, &gs);
    if (e == NULL && GFARM_S_ISDIR(gs.st_mode)
#if 1 /* XXX - Currently, GFARM_S_ISDIR cannot work correctly... */
	|| (e == GFARM_ERR_NO_SUCH_OBJECT
	    && (strcmp(gfarm_url, gfarm_prefix) == 0
		|| gfarm_url[strlen(gfarm_url) - 1] == '/'))
#endif
	) {
	/* gfarm_url is a directory */
	char *bname = basename(filename);

	target_url = malloc(strlen(gfarm_url) + strlen(bname) + 2);
	if (target_url == NULL)
	    fputs("not enough memory", stderr), exit(1);

	strcpy(target_url, gfarm_url);
	if (strcmp(target_url, gfarm_prefix) != 0
	    && target_url[strlen(target_url) - 1] != '/')
	    strcat(target_url, "/");
	strcat(target_url, bname);
    }
    else if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
	fprintf(stderr, "%s: %s\n", gfarm_url, e);
	exit(1);
    }
    else {
	target_url = strdup(gfarm_url);
	if (target_url == NULL)
	    fputs("not enough memory", stderr), exit(1);
    }

    if (S_ISREG(s.st_mode) && (s.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) != 0) {
	/* register a binary executable. */
	if (node_index == NULL) {
	    char *self_name;

	    e = gfarm_host_get_canonical_self_name(&self_name);
	    if (e == NULL)
		node_index = gfarm_host_info_get_architecture_by_host(
		    self_name);
	    else
		fprintf(stderr, "%s: %s\n", gfarm_host_get_self_name(), e);
	}
	if (node_index == NULL) {
	    fprintf(stderr, "%s: cannot determine the architecture of %s.\n",
		    program_name, gfarm_host_get_self_name());
	    exit(1);
	}
	if (total_nodes <= 0) {
	    e = gfs_pio_get_node_size(&total_nodes);
	    if (e != NULL)
		total_nodes = 1;
	}

	e = gfarm_url_program_register(target_url, node_index,
				       filename, total_nodes);
    } else {
	int index;
	/* register a file fragment. */
	if (node_index == NULL) {
	    e = gfs_pio_get_node_rank(&index);
	    if (e != NULL) {
		fprintf(stderr, "%s: missing -I <Gfarm index>\n",
			program_name);
		exit(1);
	    }
	}
	else
	    index = strtol(node_index, NULL, 0);

	if (total_nodes <= 0) {
	    e = gfs_pio_get_node_size(&total_nodes);
	    if (e != NULL) {
		fprintf(stderr, "%s: missing -N <total num of fragments>\n",
			program_name);
		exit(1);
	    }
	}

	e = gfarm_url_fragment_register(target_url, index, hostname,
					total_nodes, filename);
    }
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", target_url, e);
	exit(1);
    }

    free(target_url);

    e = gfarm_terminate();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    exit(0);
}

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "gfarm.h"

/*
 *  Register a local file to the Gfarm Meta DB.
 *
 *  gfreg <local_filename> <gfarm_url> <index>
 */

char *program_name = "gfreg";

main(int argc, char * argv[])
{
    char * filename, * gfarm_url;
    int index;
    char * e = (char *)NULL;
    struct stat sb;

    if (argc != 4)
	fprintf(stderr, "Usage: %s filename gfarm_url index\n",
		program_name), exit(1);

    filename = argv[1];
    gfarm_url = argv[2];
    index = atoi(argv[3]);

    if (stat(filename, &sb))
	perror(filename);

    e = gfarm_initialize();
    if (e != NULL) {
	fprintf(stderr, "%s: %s\n", program_name, e);
	exit(1);
    }

    e = gfs_pio_set_fragment_info_local(filename, gfarm_url, index);
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

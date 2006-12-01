#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	char *print_argv = getenv("GFARM_TEST_PRINT_ARGV");
	int i;

	e = gfarm_initialize(&argc, &argv);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_initialize: %s\n",
		    gfarm_error_string(e));
		return (1);
	}

	if (print_argv != NULL) {
		for (i = 0; i < argc; i++)
			printf("argv[%d] = %s\n", i, argv[i]);
	}
	
	e = gfarm_terminate();
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_terminate: %s\n",
		    gfarm_error_string(e));
		return (1);
	}
	return (0);
}

#include <stdlib.h>
#include <stdio.h>
#include <gfarm/gfarm.h>

void
error_check(gfarm_error_t e, char *diag)
{
	if (e == GFARM_ERR_NO_ERROR)
		return;
	fprintf(stderr, "%s: %s\n", diag, gfarm_error_string(e));
	exit(1);
}

int
main(int argc, char *argv[])
{
	gfarm_error_t e;
	int i;

	e = gfarm_initialize(&argc, &argv);
	error_check(e, "gfarm_initialize");

	for (i = 0; i < 128; ++i)
		printf("%03d %s\n", i, gfarm_error_string(i));

	e = gfarm_terminate();
	error_check(e, "gfarm_terminate");
	return (0);
}

#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist list;

	if (argc != 1) {
		fprintf(stderr, "Usage: stringlist_init\n");
		return (2);
	}
	e = gfarm_stringlist_init(&list);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s\n", gfarm_error_string(e));
		return (1);
	}
	gfarm_stringlist_free_deeply(&list);

	return (0);
}

#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist list;

	if (argc != 2) {
		fprintf(stderr, "Usage: STRINGLIST_ELEM <string>\n");
		return (2);
	}
	e = gfarm_stringlist_init(&list);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_stringlist_init: %s\n",
			gfarm_error_string(e));
		return (2);
	}
	GFARM_STRINGLIST_ELEM(list, 0) = argv[1];
	printf("%s", gfarm_stringlist_elem(&list, 0));
	return (0);
}

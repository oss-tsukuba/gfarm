#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist list;
	int i;

	e = gfarm_stringlist_init(&list);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_stringlist_init: %s\n",
			gfarm_error_string(e));
		return (2);
	}
	for (i = 1; i < argc; i++) {
		e = gfarm_stringlist_add(&list, argv[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "gfarm_stringlist_add: %s\n",
				gfarm_error_string(e));
			return (2);
		}
	}
	for (i = 0; i < gfarm_stringlist_length(&list); i++)
		printf("%s", GFARM_STRINGLIST_STRARRAY(list)[i]);

	return (0);
}

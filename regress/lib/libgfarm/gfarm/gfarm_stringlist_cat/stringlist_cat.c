#include <stdio.h>
#include <stdlib.h>

#include <gfarm/gfarm.h>

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	gfarm_stringlist list;
	int i;
	char *v[2];

	e = gfarm_stringlist_init(&list);
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "gfarm_stringlist_init: %s\n",
			gfarm_error_string(e));
		return (2);
	}
	v[1] = NULL;
	for (i = 1; i < argc; i++) {
		v[0] = argv[i];
		e = gfarm_stringlist_cat(&list, v);
		if (e != GFARM_ERR_NO_ERROR) {
			fprintf(stderr, "%s\n", gfarm_error_string(e));
			return (1);
		}
	}
	for (i = 0; i < gfarm_stringlist_length(&list); i++)
		printf("%s", gfarm_stringlist_elem(&list, i));
	return (0);
}

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

/* gfarm_fixedstrings: fixed length array of dynamically allocated strings */

int
gfutil_fixedstrings_dup(int n, char **dst, char **src)
{
	int i;

	for (i = 0; i < n; i++) {
		dst[i] = strdup(src[i]);
		if (dst[i] == NULL) {
			while (--i >= 0) {
				free(dst[i]);
				dst[i] = NULL;
			}
			gflog_debug(GFARM_MSG_1000915,
			    "allocation of string 'dst' failed: %s",
			    strerror(ENOMEM));
			return (ENOMEM);
		}
	}
	return (0);
}

/* gfarm_strarray: NULL terminated gfarm_strings */

int
gfarm_strarray_length(char **array)
{
	int i;

	for (i = 0; array[i] != NULL; i++)
		;
	return (i);
}

char**
gfarm_strarray_dup(char **array)
{
	int n = gfarm_strarray_length(array);
	char **v;

	GFARM_MALLOC_ARRAY(v, n + 1);
	if (v == NULL) {
		gflog_debug(GFARM_MSG_1000917,
		    "allocation of string failed: %s", strerror(ENOMEM));
		return (v);
	}
	if (gfutil_fixedstrings_dup(n, v, array) != 0)
		return (NULL);
	v[n] = NULL;
	return (v);
}

void
gfarm_strarray_free(char **array)
{
	int i;

	for (i = 0; array[i] != NULL; i++)
		free(array[i]);
	free(array);
}

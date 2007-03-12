/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>

#include "gfarm_list.h"

#define GFARM_LIST_INITIAL	50
#define GFARM_LIST_DELTA	50

/* gfarm_list: variable size array */

char *
gfarm_list_init(gfarm_list *listp)
{
	void **v;

	GFARM_MALLOC_ARRAY(v, GFARM_LIST_INITIAL);
	if (v == NULL)
		return (GFARM_ERR_NO_MEMORY);
	listp->size = GFARM_LIST_INITIAL;
	listp->length = 0;
	listp->array = v;
	v[0] = NULL;
	return (NULL);
}

void
gfarm_list_free(gfarm_list *listp)
{
	free(listp->array);

	/* the following is not needed, but to make erroneous program abort */
	listp->size = 0;
	listp->length = 0;
	listp->array = NULL;
}

void
gfarm_list_free_deeply(gfarm_list *listp, void (*freep)(void *))
{
	int i, length = gfarm_list_length(listp);

	for (i = 0; i < length; i++) {
		if (listp->array[i] != NULL)
			freep(listp->array[i]);
	}
	gfarm_list_free(listp);
}

char *
gfarm_list_add_array(gfarm_list *listp, int al, void **av)
{
	int ll = gfarm_list_length(listp);

	if (ll + al > listp->size) {
		int n = listp->size;
		void **t;

		do {
			n += GFARM_LIST_DELTA;
		} while (ll + al > n);
		GFARM_REALLOC_ARRAY(t, listp->array, n);
		if (t == NULL)
			return (GFARM_ERR_NO_MEMORY);
		listp->size = n;
		listp->array = t;
	}
	memcpy(&listp->array[ll], av, sizeof(void *) * al);
	listp->length += al;
	return (NULL);
}

char *
gfarm_list_add_list(gfarm_list *listp, gfarm_list *addp)
{
	return (gfarm_list_add_array(listp,
	    gfarm_list_length(addp), addp->array));
}

char *
gfarm_list_add(gfarm_list *listp, void *s)
{
	int length = gfarm_list_length(listp);

	if (length >= listp->size) {
		int n = listp->size + GFARM_LIST_DELTA;
		void **t;

		GFARM_REALLOC_ARRAY(t, listp->array, n);
		if (t == NULL)
			return (GFARM_ERR_NO_MEMORY);
		listp->size = n;
		listp->array = t;
	}
	listp->array[length] = s;
	listp->length++;
	return (NULL);
}

/* gfarm_fixedarray: fixed length array of dynamically allocated entries */

char *
gfarm_fixedarray_dup(int n, void **dst, void **src, void *(*dup)(void *))
{
	int i;

	for (i = 0; i < n; i++) {
		dst[i] = dup(src[i]);
		if (dst[i] == NULL) {
			while (--i >= 0) {
				free(dst[i]);
				dst[i] = NULL;
			}
			return (GFARM_ERR_NO_MEMORY);
		}
	}
	return (NULL);
}


/* gfarm_array: dynamically allocated entry array and entry contents */

void *
gfarm_array_alloc_from_list(gfarm_list *listp)
{
	int n = gfarm_list_length(listp);
	void **t;

	GFARM_MALLOC_ARRAY(t, n);
	if (t == NULL)
		return (NULL);
	memcpy(t, listp->array, sizeof(void *) * n);
	return (t);
}

void
gfarm_array_free_deeply(int n, void *array, void (*freep)(void *))
{
	int i;
	void **a = array;

	for (i = 0; i < n; i++) {
		if (a[i] != NULL)
			freep(a[i]);
	}
	free(array);
}

#include <errno.h>
#include <stdlib.h>

#include <gfarm/gfarm_misc.h>

void *
gfarm_malloc_array(size_t number, size_t size)
{
	size_t total_size = number * size;

	if (number != 0 && total_size / number != size) {
		errno = ENOMEM;
		return NULL;
	}
	return (malloc(total_size));
}

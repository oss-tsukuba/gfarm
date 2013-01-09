/*
 * list
 *
 * $Id*
 */

typedef struct gfarm_list {
	void **array;
	int length, size;
} gfarm_list;

#define GFARM_LIST_STRARRAY(list)	(list).array
#define GFARM_LIST_ELEM(list, i)	(list).array[i]
#define gfarm_list_length(list)		(list)->length
#define gfarm_list_elem(list, i)	GFARM_LIST_ELEM(*(list), i)

gfarm_error_t gfarm_list_init(gfarm_list *);
gfarm_error_t gfarm_list_add_list(gfarm_list *, gfarm_list *);
gfarm_error_t gfarm_list_add(gfarm_list *, void *);
void gfarm_list_free(gfarm_list *);
void gfarm_list_free_deeply(gfarm_list *, void (*)(void *));

void *gfarm_array_alloc_from_list(gfarm_list *);
void gfarm_array_free_deeply(int, void *, void (*)(void *));

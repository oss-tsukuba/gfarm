/*
 * stringlist
 */
typedef struct gfarm_stringlist {
	char **array;
	int length, size;
} gfarm_stringlist;

#define GFARM_STRINGLIST_STRARRAY(stringlist)	(stringlist).array
#define GFARM_STRINGLIST_ELEM(stringlist, i)	(stringlist).array[i]
#define gfarm_stringlist_length(stringlist)	(stringlist)->length
#define gfarm_stringlist_elem(stringlist, i)	\
	GFARM_STRINGLIST_ELEM(*(stringlist), i)

char *gfarm_stringlist_init(gfarm_stringlist *);
char *gfarm_stringlist_add_list(gfarm_stringlist *, gfarm_stringlist *);
char *gfarm_stringlist_add(gfarm_stringlist *, char *);
char *gfarm_stringlist_cat(gfarm_stringlist *, char **);
void gfarm_stringlist_free(gfarm_stringlist *);
void gfarm_stringlist_free_deeply(gfarm_stringlist *);

char **gfarm_strings_alloc_from_stringlist(gfarm_stringlist *);

/*
 * $Id$
 */

struct gfarm_str_list {
    char *el;
    struct gfarm_str_list *next;
};

struct gfarm_str_list *gfarm_str_list_cons(char *, struct gfarm_str_list *);
void gfarm_str_list_free(struct gfarm_str_list *);
void gfarm_str_list_free_deeply(struct gfarm_str_list *);
char *gfarm_str_list_car(struct gfarm_str_list *);
struct gfarm_str_list *gfarm_str_list_cdr(struct gfarm_str_list *);
char *gfarm_str_list_nth(int, struct gfarm_str_list *);
struct gfarm_str_list *gfarm_str_list_append(
	struct gfarm_str_list *, struct gfarm_str_list *);
struct gfarm_str_list *gfarm_str_list_reverse(struct gfarm_str_list *);
struct gfarm_str_list *gfarm_str_list_remove(char *, struct gfarm_str_list *);
struct gfarm_str_list *gfarm_str_list_uniq(struct gfarm_str_list *);

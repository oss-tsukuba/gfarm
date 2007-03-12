/*
 * $Id
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gfarm/gfarm_misc.h>
#include "str_list.h"

struct gfarm_str_list *
gfarm_str_list_cons(char *el, struct gfarm_str_list *ls)
{
	struct gfarm_str_list *t;

	GFARM_MALLOC(t);
	if (t == NULL)
		return (NULL);
	t->next = ls;
	t->el = el;
	return (t);
}

void
gfarm_str_list_free(struct gfarm_str_list *ls)
{
	struct gfarm_str_list *next;

	while (ls) {
		next = ls->next;
		free(ls);
		ls = next;
	}
}

void
gfarm_str_list_free_deeply(struct gfarm_str_list *ls)
{
	struct gfarm_str_list *next;

	while (ls) {
		next = ls->next;
		free(ls->el);
		free(ls);
		ls = next;
	}
}

char *
gfarm_str_list_car(struct gfarm_str_list *ls)
{
	return (ls->el);
}

struct gfarm_str_list *
gfarm_str_list_cdr(struct gfarm_str_list *ls)
{
	return (ls->next);
}

char *
gfarm_str_list_nth(int n, struct gfarm_str_list *ls)
{
	for (; n > 0; --n)
		ls = ls->next;
	return (gfarm_str_list_car(ls));
}

int
gfarm_str_list_length(struct gfarm_str_list *ls)
{
	int i;

	for (i = 0; ls; ls = ls->next, ++i);
	return (i);
}

struct gfarm_str_list *
gfarm_str_list_append(struct gfarm_str_list *ls1, struct gfarm_str_list *ls2)
{
	if (ls1 == NULL)
		return (ls2);
	else
		return gfarm_str_list_cons(gfarm_str_list_car(ls1),
			gfarm_str_list_append(gfarm_str_list_cdr(ls1), ls2));
}

static struct gfarm_str_list *
gfarm_str_list_reverse_internal(
	struct gfarm_str_list *ls1, struct gfarm_str_list *ls2)
{
	if (ls1 == NULL)
		return (ls2);
	else
		return gfarm_str_list_reverse_internal(gfarm_str_list_cdr(ls1),
			gfarm_str_list_cons(gfarm_str_list_car(ls1), ls2));
}

struct gfarm_str_list *
gfarm_str_list_reverse(struct gfarm_str_list *ls)
{
	return (gfarm_str_list_reverse_internal(ls, NULL));
}

struct gfarm_str_list *
gfarm_str_list_remove(char *el, struct gfarm_str_list *ls)
{
	if (ls)
		if (strcmp(el, gfarm_str_list_car(ls)) == 0)
			return (gfarm_str_list_remove(
					el, gfarm_str_list_cdr(ls)));
		else
			return (gfarm_str_list_cons(gfarm_str_list_car(ls),
				    gfarm_str_list_remove(
					    el, gfarm_str_list_cdr(ls))));
	else
		return (NULL);
}

struct gfarm_str_list *
gfarm_str_list_uniq(struct gfarm_str_list *ls)
{
	if (ls)
		return (gfarm_str_list_cons(gfarm_str_list_car(ls),
			  gfarm_str_list_uniq(gfarm_str_list_remove(
			    gfarm_str_list_car(ls), gfarm_str_list_cdr(ls)))));
	else
		return (NULL);
}

void
gfarm_str_list_print(struct gfarm_str_list *ls)
{
	int i;

	for (i = 0; ls; ls = ls->next, ++i)
		printf("gfarm_str_list[%d] = %s\n", i, ls->el);
}

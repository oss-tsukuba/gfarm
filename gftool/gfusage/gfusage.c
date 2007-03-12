/*
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gfarm/gfarm.h>
#include "metadb_access.h"
#include "hash.h"

#define USAGE_HASH_SIZE 53

struct pathname_list {
	char *pathname;
	struct pathname_list *next;
};

struct pathnames_size {
	struct pathname_list *pathnames;
	file_offset_t size;
	unsigned long long nfiles;
	unsigned long long nsections;
	unsigned long long ncopies;
};

struct gfarm_usage_st {
	struct gfarm_hash_table *table;
};

typedef struct gfarm_usage_st *gfarm_usage;

char *
gfarm_usage_initialize(gfarm_usage *gup)
{
	gfarm_usage gu;

	GFARM_MALLOC(gu);
	if (gu == NULL)
		return (GFARM_ERR_NO_MEMORY);

	gu->table = gfarm_hash_table_alloc(USAGE_HASH_SIZE, gfarm_hash_default,
					   gfarm_hash_key_equal_default);
	if (gu->table == NULL) {
		free(gu);
		return (GFARM_ERR_NO_MEMORY);
	} else {
		*gup = gu;
		return (NULL);
	}
}

char *
gfarm_usage_terminate(gfarm_usage gu)
{
	struct gfarm_hash_iterator iterator;
	struct gfarm_hash_entry *he;
	struct pathnames_size *ps;
	struct pathname_list *now, *tmp;

	gfarm_hash_iterator_begin(gu->table, &iterator);
	while (1) {
		he = gfarm_hash_iterator_access(&iterator);
		if (he == NULL)
			break;
		ps = gfarm_hash_entry_data(he);
		now = ps->pathnames;
		while (now) {
			if (now->pathname)
				free(now->pathname);
			tmp = now->next;
			free(now);
			now = tmp;
		}
		gfarm_hash_iterator_next(&iterator);
	}
	gfarm_hash_table_free(gu->table);
	free(gu);

	return (NULL);
}

char *
gfarm_usage_pathname_add(gfarm_usage gu, const char *user,
			 const char *pathname)
{
	struct gfarm_hash_entry *he;
	int created;
	struct pathnames_size *ps;
	struct pathname_list *now;

	he = gfarm_hash_enter(gu->table, user, strlen(user),
			      sizeof(struct pathnames_size), &created);
	if (he == NULL)
		return (GFARM_ERR_NO_MEMORY);
	ps = gfarm_hash_entry_data(he);
	if (created) {
		ps->pathnames = NULL;
		ps->size = 0;
		ps->nfiles = 0;
		ps->nsections = 0;
		ps->ncopies = 0;
	}
	GFARM_MALLOC(now);
	if (now == NULL)
		return (GFARM_ERR_NO_MEMORY);
	now->pathname = strdup(pathname);

	if (ps->pathnames == NULL) {
		now->next = NULL;
		ps->pathnames = now;
	} else {
		now->next = ps->pathnames;
		ps->pathnames = now;
	}

	return (NULL);
}

char *
gfarm_usage_all_users_get(gfarm_usage gu, char ***usersp, int *nusersp)
{
	char *e;
	struct gfarm_hash_iterator iterator;
	struct gfarm_hash_entry *he;
	char *key, *user;
	char **users;
	int nusers, memlen, keylen;

	memlen = 8;
	GFARM_MALLOC_ARRAY(users, memlen);
	if (users == NULL)
		return (GFARM_ERR_NO_MEMORY);

	nusers = 0;
	gfarm_hash_iterator_begin(gu->table, &iterator);
	while (1) {
		he = gfarm_hash_iterator_access(&iterator);
		if (he == NULL)
			break;
		key = gfarm_hash_entry_key(he);
		keylen = gfarm_hash_entry_key_length(he);
		GFARM_CALLOC_ARRAY(user, keylen + 1);
		if (user == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		memcpy(user, key, keylen);
		if (nusers + 1 > memlen) {
			memlen = memlen * 2;
			GFARM_REALLOC_ARRAY(users, users, memlen);
			if (users == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
		}
		users[nusers] = user;
		nusers++;
		gfarm_hash_iterator_next(&iterator);
	}
	*usersp = users;
	*nusersp = nusers;

	return (NULL);
}

void
gfarm_usage_all_users_free(gfarm_usage gu, char **users, int nusers)
{
	int i;

	for (i = 0; i < nusers; i++)
		if (users[i])
			free(users[i]);
	if (users)
		free(users);
}

static char *
gfarm_usage_pathnames_size_get(gfarm_usage gu, const char *user,
			       struct pathnames_size **psp)
{
	struct gfarm_hash_entry *he;

	he = gfarm_hash_lookup(gu->table, user, strlen(user));
	if (he == NULL)
		return (GFARM_ERR_NO_SUCH_OBJECT);
	*psp = gfarm_hash_entry_data(he);

	return (NULL);
}

char *
gfarm_usage_size_get(gfarm_usage gu, const char *user, file_offset_t *sizep,
		     unsigned long long *nfilesp,
		     unsigned long long *nsectionsp,
		     unsigned long long *ncopiesp)
{
	char *e;
	struct pathnames_size *ps;

	e = gfarm_usage_pathnames_size_get(gu, user, &ps);
	if (e != NULL)
		return (e);
	*sizep = ps->size;
	*nfilesp = ps->nfiles;
	*nsectionsp = ps->nsections;
	*ncopiesp = ps->ncopies;

	return (NULL);
}

char *
gfarm_file_section_ncopies(const char *pathname, const char *section,
			   int *ncopiesp)
{
	char *e;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
		pathname, section, ncopiesp, &copies);
	if (e == NULL)
		gfarm_file_section_copy_info_free_all(*ncopiesp, copies);

	return (e);
}

static void
gfarm_usage_callback(void *arg, struct gfarm_path_info *info)
{
	gfarm_usage gu = arg;

	gfarm_usage_pathname_add(gu, info->status.st_user, info->pathname);

	return;
}

static char *
gfarm_usage_user_pathname_collect(gfarm_usage gu)
{
	char *e;

	e = gfarm_metadb_path_info_get_all_foreach(gfarm_usage_callback, gu);

	return (e);
}

static char *
gfarm_usage_error(const char *pathname, const char *message)
{
	static char msg[512];

	snprintf(msg, 512, "%s: %s", pathname, message);

	return msg;
}

static char *
gfarm_usage_user_size_calculate(gfarm_usage gu, const char *user,
				int ncopies_mode)
{
	char *e, *e_save = NULL;
	struct pathnames_size *ps;
	struct pathname_list *pl;
	struct gfarm_file_section_info *sections;
	int i, nsections, ncopies;

	e = gfarm_usage_pathnames_size_get(gu, user, &ps);
	if (e != NULL) goto end;

	pl = ps->pathnames;
	while (pl) {
		e = gfarm_file_section_info_get_all_by_file(
			pl->pathname, &nsections, &sections);
		if (e != NULL) {
			if (e_save == NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
				e_save = gfarm_usage_error(pl->pathname, e);
			goto next_pathname;
		}
		ps->nfiles++;
		ps->nsections += nsections;
		for (i = 0; i < nsections; ++i) {
			if (ncopies_mode) {
				e = gfarm_file_section_ncopies(
					pl->pathname, sections[i].section,
					&ncopies);
				if (e != NULL) {
					if (e_save == NULL &&
					    e != GFARM_ERR_NO_SUCH_OBJECT)
						e_save = gfarm_usage_error(
							pl->pathname, e);
					continue;
				}
				ps->size += sections[i].filesize * ncopies;
				ps->ncopies += ncopies;
			} else {
				ps->size += sections[i].filesize;
			}
		}
		gfarm_file_section_info_free_all(nsections, sections);
	next_pathname:
		pl = pl->next;
	}
end:
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = NULL;
	if (e != NULL && e_save != NULL)
		e = e_save;

	return (e);
}

static char *
gfarm_usage_calculate_common(gfarm_usage gu, int ncopies_mode,
			     int calc_mode, FILE *output)
{
	char *e, *e_save = NULL;
	int i, nusers;
	char **users;
	file_offset_t size;
	unsigned long long nfiles, nsections, ncopies;

	if (output != NULL) {
		if (ncopies_mode)
			fprintf(output, "# user size files sections copies\n");
		else
			fprintf(output, "# user size files sections\n");
	}
	if (calc_mode) {
		e = gfarm_usage_user_pathname_collect(gu);
		if (e != NULL) goto end;
	}
	e = gfarm_usage_all_users_get(gu, &users, &nusers);
	if (e != NULL) goto end;

	for (i = 0; i < nusers; i++) {
		if (calc_mode) {
			e = gfarm_usage_user_size_calculate(gu, users[i],
							    ncopies_mode);
			if (e != NULL && e_save == NULL)
				e_save = e;
		}
		if (output != NULL) {
			e = gfarm_usage_size_get(gu, users[i], &size, &nfiles,
						 &nsections, &ncopies);
			if (e != NULL && e_save == NULL)
				e_save = e;
			if (ncopies_mode)
				fprintf(output, "%s %" PR_FILE_OFFSET
					" %llu %llu %llu\n",
					users[i], size,
					nfiles, nsections, ncopies);
			else
				fprintf(output, "%s %" PR_FILE_OFFSET
					" %llu %llu\n",
					users[i], size, nfiles, nsections);
		}
	}
	gfarm_usage_all_users_free(gu, users, nusers);

	if (e_save != NULL)
		e = e_save;
end:
	return (e);
}

char *
gfarm_usage_calculate(gfarm_usage gu, int ncopies_mode, FILE *output)
{
	return gfarm_usage_calculate_common(gu, ncopies_mode, 1, output);
}

char *
gfarm_usage_result_write(gfarm_usage gu, FILE *output)
{
	return gfarm_usage_calculate_common(gu, 0, 0, output);
}

static void
usage(void)
{
	fprintf(stderr, "usage: gfusage [-c]\n");
	fprintf(stderr, "option:\n");
	fprintf(stderr, "\t-c\t\ttotal size in consideration of replicas\n");
	fflush(stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *e, c;
	int ncopies_mode = 0;
	gfarm_usage gu;

	while ((c = getopt(argc, argv, "ch?")) != -1) {
		switch (c) {
		case 'c':
			ncopies_mode = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	e = gfarm_initialize(NULL, NULL);
	if (e != NULL) goto end;

	e = gfarm_usage_initialize(&gu);
	if (e != NULL) goto terminate;

	e = gfarm_usage_calculate(gu, ncopies_mode, stdout);
	if (e != NULL)
		fprintf(stderr, "%s: %s\n", argv[0], e);
#if 0
	e = gfarm_usage_result_write(gu, stdout);
	if (e != NULL)
		fprintf(stderr, "%s: %s\n", argv[0], e);
#endif
	e = gfarm_usage_terminate(gu);
	if (e != NULL)
		fprintf(stderr, "%s: %s\n", argv[0], e);
terminate:
	e = gfarm_terminate();
end:
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		return (1);
	} else
		return (0);
}

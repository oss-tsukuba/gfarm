/*
 * $Id$
 */
#include <stdio.h> /* snprintf() */
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "host.h"
#include "repattr.h"

#define FSNGROUP_HASHTAB_SIZE	317	/* prime number */

#define SPACE_CHARS " \t\r\n"
#define IS_SPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define IS_DIGIT(c) ('0' <= (c) && (c) <= '9')

static int
count_char(const char *s, size_t len, char c)
{
	int n = 0;
	size_t i;

	for (i = 0; i < len; i++) {
		if (s[i] == c)
			n++;
	}
	return (n);
}

struct repplace {
	char **fsngroup_names;
	int fsngroup_number;
	int amount;
};

/* this assumes ASCII compatible charset */
static int
is_fsngroup_char(char c)
{
	return (c == '-' || c == '_' || IS_DIGIT(c) ||
	    ('A' <= c && c <= 'Z') ||
	    ('a' <= c && c <= 'z'));
}

static void
repplace_free(struct repplace *repplace)
{
	int i;

	for (i = 0; i < repplace->fsngroup_number; i++)
		free(repplace->fsngroup_names[i]);
	free(repplace->fsngroup_names);
}

int
repplace_get_fsngroup_number(struct repplace *repplace)
{
	return (repplace->fsngroup_number);
}

const char *
repplace_get_fsngroup(struct repplace *repplace, int i)
{
	assert(i >= 0 && i < repplace->fsngroup_number);
	return (repplace->fsngroup_names[i]);
}

int
repplace_get_amount(struct repplace *repplace)
{
	return (repplace->amount);
}

static gfarm_error_t
parse_to_repplace(const char **sp, struct repplace *repplace)
{
	const char *s = *sp, *token;
	int n_fsngroups, i;
	size_t fsnset_len, len;

	fsnset_len = strcspn(s, ":,");
	n_fsngroups = count_char(s, fsnset_len, '+') + 1;
	GFARM_MALLOC_ARRAY(repplace->fsngroup_names, n_fsngroups);
	if (repplace->fsngroup_names == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "repplace_parse(%s): no memory for %d fsngroups",
		    s, n_fsngroups);
		return (GFARM_ERR_NO_MEMORY);
	}

	repplace->fsngroup_number = 0;
	for (i = 0; i < n_fsngroups; i++) {
		while (IS_SPACE(*s))
			s++;
		if (!is_fsngroup_char(*s)) { /* syntax error */
			repplace_free(repplace);
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		token = s;
		len = 1;
		while (is_fsngroup_char(token[len]))
			len++;
		s += len;

		GFARM_MALLOC_ARRAY(repplace->fsngroup_names[i], len + 1);
		if (repplace->fsngroup_names[i] == NULL) {
			repplace_free(repplace);
			gflog_error(GFARM_MSG_UNFIXED,
			    "repplace_parse(%s): no memory for fsngroup",
			    token);
			return (GFARM_ERR_NO_MEMORY);
		}
		memcpy(repplace->fsngroup_names[i], token, len);
		repplace->fsngroup_names[i][len] = '\0';
		repplace->fsngroup_number = i + 1;

		while (IS_SPACE(*s))
			s++;

		if (*s == '+') {
			s++;
			continue;
		}
		break;
	}
	if (*s != ':') { /* syntax error */
		repplace_free(repplace);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	s++; /* ':' */

	while (IS_SPACE(*s))
		s++;
	if (!IS_DIGIT(*s)) { /* syntax error */
		repplace_free(repplace);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	token = s;
	len = 1;
	while (IS_DIGIT(token[len]))
		len++;
	s += len;

	repplace->amount = atoi(token);

	while (IS_SPACE(*s))
		s++;
	if (*s != ',' && *s != '\0') { /* syntax error */
		repplace_free(repplace);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}

	*sp = s;
	return (GFARM_ERR_NO_ERROR);
}

struct repspec {
	struct repplace *repplaces;
	int repplace_number;
};

void
repspec_free(struct repspec *repspec)
{
	int i;

	for (i = 0; i < repspec->repplace_number; i++)
		repplace_free(&repspec->repplaces[i]);
	free(repspec->repplaces);
	free(repspec);
}

int
repspec_get_total_amount(struct repspec *repspec)
{
	int i, amount = 0;

	for (i = 0; i < repspec->repplace_number; i++)
		amount += repspec->repplaces[i].amount;
	return (amount);
}

int
repspec_get_repplace_number(struct repspec *repspec)
{
	return (repspec->repplace_number);
}

struct repplace *
repspec_get_repplace(struct repspec *repspec, int i)
{
	assert(i >= 0 && i < repspec->repplace_number);
	return (&repspec->repplaces[i]);
}

gfarm_error_t
repattr_parse_to_repspec(const char *repattr, struct repspec **repspecp)
{
	gfarm_error_t e;
	int n_repplaces, i;
	struct repspec *repspec;

	GFARM_MALLOC(repspec);
	if (repspec == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "repspec_parse(%s): no memory", repattr);
		return (GFARM_ERR_NO_MEMORY);
	}

	n_repplaces = count_char(repattr, strlen(repattr), ',') + 1;
	GFARM_MALLOC_ARRAY(repspec->repplaces, n_repplaces);
	if (repspec->repplaces == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "repspec_parse(%s): no memory for %d repplaces",
		    repattr, n_repplaces);
		free(repspec);
		return (GFARM_ERR_NO_MEMORY);
	}
	repspec->repplace_number = n_repplaces;

	for (i = 0; i < n_repplaces; i++) {
		e = parse_to_repplace(&repattr, &repspec->repplaces[i]);
		if (e != GFARM_ERR_NO_ERROR) {
			repspec->repplace_number = i == 0 ? 0 : i - 1;
			repspec_free(repspec);
			return (e);
		}
		if (*repattr == '\0')
			break;
		assert(*repattr == ',');
		repattr++;
	}
	*repspecp = repspec;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
repspec_all_fsngroups_are_valid(struct repspec *repspec)
{
	int i, j, n_repplaces, n_fsngroups;

	n_repplaces = repspec->repplace_number;
	for (i = 0; i < n_repplaces; i++) {
		n_fsngroups = repspec->repplaces[i].fsngroup_number;
		for (j = 0; j < n_fsngroups; j++) {
			gfarm_error_t e = fsngroup_does_exist(
			    repspec->repplaces[i].fsngroup_names[j]);

			if (e != GFARM_ERR_NO_ERROR)
				return (e);
		}
	}

	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
repspec_there_is_no_duplicate_fsngroup(struct repspec *repspec)
{
	/*
	 * since SF.net #1045,
	 * each fsngroup is allowed to appear at most once
	 */
	struct gfarm_hash_table *hashtab = gfarm_hash_table_alloc(
	    FSNGROUP_HASHTAB_SIZE,
	    gfarm_hash_default, gfarm_hash_key_equal_default);
	struct gfarm_hash_entry *entry;
	int i, j, n_repplaces, n_fsngroups, created;
	const char *fsngroup;
	size_t len;

	if (hashtab == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "respec_normalize: allocating hashtab: no memory");
		return (GFARM_ERR_NO_MEMORY);
	}

	n_repplaces = repspec->repplace_number;
	for (i = 0; i < n_repplaces; i++) {
		n_fsngroups = repspec->repplaces[i].fsngroup_number;
		for (j = 0; j < n_fsngroups; j++) {
			fsngroup = repspec->repplaces[i].fsngroup_names[j];
			len = strlen(fsngroup);
			entry = gfarm_hash_enter(
			    hashtab, fsngroup, len + 1, 0, &created);
			if (entry == NULL) {
				gfarm_hash_table_free(hashtab);
				return (GFARM_ERR_NO_MEMORY);
			}
			if (!created) { /* this fsngroup appeared twice */
				gfarm_hash_table_free(hashtab);
				return (GFARM_ERR_INVALID_ARGUMENT);
			}
		}
	}
	gfarm_hash_table_free(hashtab);

	return (GFARM_ERR_NO_ERROR);
}

/*
 * repspec_validate() is for validating RPC requests from users.
 * this does NOT intend to be used for validating metadata loading
 * at gfmd startup.
 *
 * especially, repspec_all_fsngroups_are_valid() check may not
 * be appropriate in case of temporary removal of filesystem nodes. 
 */
gfarm_error_t
repspec_validate(struct repspec *repspec)
{
	gfarm_error_t e;

	e = repspec_all_fsngroups_are_valid(repspec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	e = repspec_there_is_no_duplicate_fsngroup(repspec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	return (GFARM_ERR_NO_ERROR);
}

static int
strptr_compare(const void *a, const void *b)
{
	char *const *aa = a;
	char *const *bb = b;

	return (strcmp(*aa, *bb));
}

static int
repplace_compare(const void *a, const void *b)
{
	const struct repplace *aa = a;
	const struct repplace *bb = b;
	static int first = 1;

	if (aa->fsngroup_number <= 0 ||
	    bb->fsngroup_number <= 0) {
		/* this should not happen */
		if (first) {
			first = 0;
			gflog_error(GFARM_MSG_UNFIXED,
			    "repplace_compare: invalid repspec, %d vs %d",
			    aa->fsngroup_number,
			    bb->fsngroup_number);
		}
		if (aa->fsngroup_number != 0)
			return (1);
		else if (aa->fsngroup_number != 0)
			return (-1);
		else
			return (0);
	}
	return (strcmp(aa->fsngroup_names[0], bb->fsngroup_names[0]));
}

gfarm_error_t
repspec_normalize(struct repspec *repspec)
{
	int i, n_repplaces, n_fsngroups;

	n_repplaces = repspec->repplace_number;
	for (i = 0; i < n_repplaces; i++) {
		n_fsngroups = repspec->repplaces[i].fsngroup_number;
		qsort(repspec->repplaces[i].fsngroup_names, n_fsngroups,
		    sizeof(*repspec->repplaces[i].fsngroup_names),
		    strptr_compare);
	}
	qsort(repspec->repplaces, n_repplaces, sizeof(*repspec->repplaces),
	    repplace_compare);

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
repspec_to_string(struct repspec *repspec, char **repattrp)
{
	int i, j, n_repplaces, n_fsngroups;
	size_t l, len = 1; /* for '\0' */
	int of = 0;
	char *repattr, buf[GFARM_INT32STRLEN + 1];

	/* count string length */

	n_repplaces = repspec->repplace_number;
	for (i = 0; i < n_repplaces; i++) {
		n_fsngroups = repspec->repplaces[i].fsngroup_number;
		for (j = 0; j < n_fsngroups; j++) {
			len = gfarm_size_add(&of, len,
			    strlen(repspec->repplaces[i].fsngroup_names[j]));
			if (of)
				return (GFARM_ERR_NO_MEMORY);
			if (j + 1 < n_fsngroups) { /* for '+' */
				len = gfarm_size_add(&of, len, 1);
				if (of)
					return (GFARM_ERR_NO_MEMORY);
			}
		}
		/* for ':' */
		len = gfarm_size_add(&of, len, 1);
		if (of)
			return (GFARM_ERR_NO_MEMORY);
		snprintf(buf, sizeof buf, "%u", repspec->repplaces[i].amount);
		len = gfarm_size_add(&of, len, strlen(buf));
		if (of)
			return (GFARM_ERR_NO_MEMORY);

		if (i + 1 < n_repplaces) { /* for ',' */
			len = gfarm_size_add(&of, len, 1);
			if (of)
				return (GFARM_ERR_NO_MEMORY);
		}
	}

	GFARM_MALLOC_ARRAY(repattr, len);
	if (repattr == NULL) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "repspec_to_string(): no memory for %zd chars", len);
		return (GFARM_ERR_NO_MEMORY);
	}

 	l = 0;
	for (i = 0; i < n_repplaces; i++) {
		n_fsngroups = repspec->repplaces[i].fsngroup_number;
		for (j = 0; j < n_fsngroups; j++) {
			strcpy(&repattr[l],
			    repspec->repplaces[i].fsngroup_names[j]);
			l += strlen(&repattr[l]);
			if (j + 1 < n_fsngroups)
				repattr[l++] = '+';
		}
		repattr[l++] = ':';
		snprintf(&repattr[l], len - l, "%u",
		    repspec->repplaces[i].amount);
		l += strlen(&repattr[l]);
		if (i + 1 < n_repplaces)
			repattr[l++] = ',';
	}
	repattr[l++] = '\0';

	*repattrp = repattr;
	return (GFARM_ERR_NO_ERROR);
}

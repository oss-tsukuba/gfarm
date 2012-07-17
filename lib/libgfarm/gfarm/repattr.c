/*
 * $Id$
 */
#include <limits.h>
#include <stddef.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <assert.h>
#include <gfarm/gfarm.h>

#include "bool.h"
#include "gfutil.h"
#include "hash.h"
#include "repattr.h"

/*****************************************************************************/

#define FOR_EACH_IN_HASH(t, it)				\
	for (gfarm_hash_iterator_begin(t, (it));	\
	     !gfarm_hash_iterator_is_end(it);		\
	     gfarm_hash_iterator_next(it))

#define hash_lookup(t, key)			\
	gfarm_hash_lookup(t, &(key), sizeof(key))
#define hash_new_entry(t, key, vallen, isnew)	\
	gfarm_hash_enter(t, &(key), sizeof(key), vallen, isnew)

#define entry_key(type, e)			\
	(type)(*((type *)gfarm_hash_entry_key(e)))
#define entry_keylen(e)				\
	(size_t)(gfarm_hash_entry_key_length(e))
#define entry_vallen(e)				\
	(size_t)(gfarm_hash_entry_data_length(e))
#define entry_getval(type, e)			\
	(type)(*((type *)gfarm_hash_entry_data(e)))
#define entry_setval(e, val)			\
	(void)memcpy((gfarm_hash_entry_data(e)),	\
		(void *)&(val), entry_vallen(e))

#define iter_entry(it)				\
	gfarm_hash_iterator_access(it)

#define iter_key(type, it)			\
	entry_key(type, iter_entry(it))
#define iter_keylen(it)				\
	entry_keylen(iter_entry(it))
#define iter_vallen(it)				\
	entry_vallen(iter_entry(it))
#define iter_getval(type, it)			\
	entry_getval(type, iter_entry(it))
#define iter_setval(it, val)			\
	entry_setval(iter_entry(it), val)

#define iter_purge(it)				\
	(void)gfarm_hash_iterator_purge(it)

#define fsngroup_hash_new_entry(t, key, isnew)	\
	hash_new_entry(t, key, sizeof(size_t), isnew)

#define entry_fsngroupname(e)			\
	entry_key(const char *, e)
#define entry_amount_get(e)			\
	entry_getval(size_t, e)
#define entry_amount_set(e, n)			\
	entry_setval(e, n)

#define iter_fsngroupname(it)			\
	iter_key(const char *, it)
#define iter_amount_get(it)			\
	iter_getval(size_t, it)
#define iter_amount_set(it, n)			\
	iter_setval(it, n)

/*****************************************************************************/

#define skip_spaces(s)					    \
	while (*(s) != '\0' && isspace((int)*(s)) != 0) {   \
		(s)++;					    \
	}
#define trim_spaces(b, s)                                \
	while ((s) >= (b) && isspace((int)*(s)) != 0) {	 \
		*(s)-- = '\0';				 \
	}

/*****************************************************************************/

struct gfarm_repattr {
	char *fsngroupname;
	size_t n;
};

/*****************************************************************************/
/*
 * Internals:
 */

static size_t
tokenize(char *buf, char **tokens, int max, const char *delm) {
	size_t n = 0;
	int non_delm = 0;

	while (*buf != '\0' && n < max) {
		while (strchr(delm, (int)*buf) != NULL && *buf != '\0') {
			buf++;
		}
		if (*buf == '\0')
			break;

		if (tokens != NULL)
			tokens[n] = buf;

		non_delm = 0;
		while (strchr(delm, (int)*buf) == NULL && *buf != '\0') {
			non_delm++;
			buf++;
		}
		if (*buf == '\0') {
			if (non_delm > 0)
				n++;
			break;
		}
		if (tokens != NULL)
			*buf = '\0';
		n++;
		if (*(buf + 1) == '\0')
			break;
		else
			buf++;
	}

	return (n);
}

static bool
parse_int64_by_base(const char *str, int64_t *val, int base) {
	/*
	 * str := 
	 *	[[:space:]]*[\-\+][[:space:]]*[0-9]+[[:space:]]*[kKmMgGtTpP]
	 */
	char *e_ptr = NULL;
	bool ret = false;
	int64_t t = 1;
	char *buf = NULL;
	int64_t tmp_val;
	size_t len;
	char *end_ptr = NULL;
	int64_t neg = 1;

	skip_spaces(str);
	switch ((int)str[0]) {
        case '-':
		neg = -1;
		str++;
		break;
        case '+':
		str++;
		break;
	}
	skip_spaces(str);

	buf = strdup(str);
	if (buf == NULL) {
		return false;
	}
	len = strlen(buf);
	if (len == 0) {
		return false;
	}
	end_ptr = &(buf[len - 1]);
	trim_spaces(buf, end_ptr);
	len = strlen(buf);

	if (base == 10) {
		bool do_trim = false;
		size_t lc = len - 1;

		switch ((int)(buf[lc])) {
		case 'k': case 'K':
			t = 1024;
			do_trim = true;
			break;
		case 'm': case 'M':
			t = 1024 * 1024;
			do_trim = true;
			break;
		case 'g': case 'G':
			t = 1024 * 1024 * 1024;
			do_trim = true;
			break;
		case 't': case 'T':
			t = 1099511627776LL;	/* == 2 ^ 40 */
			do_trim = true;
			break;
		case 'p': case 'P':
			t = 1125899906842624LL;	/* == 2 ^ 50 */
			do_trim = true;
			break;
		default:
			if (isspace((int)buf[lc]) != 0)
				do_trim = true;
			break;
		}

		if (do_trim == true) {
			buf[lc] = '\0';
			end_ptr = &(buf[lc - 1]);
			trim_spaces(buf, end_ptr);
			len = strlen(buf);
		}
	}

	tmp_val = (int64_t)strtoll(buf, &e_ptr, base);
	if (e_ptr == (buf + len)) {
		ret = true;
		*val = tmp_val * t * neg;
	}

	free(buf);

	return (ret);
}

static bool
parse_int32_by_base(const char *str, int32_t *val, int base) {
	int64_t val64;
	bool ret = false;

	if ((ret = parse_int64_by_base(str, &val64, base)) == true) {
		if (val64 > (int64_t)(INT_MAX) ||
		    val64 < -(((int64_t)(INT_MAX) + 1LL))) {
			ret = false;
		} else {
			*val = (int32_t)val64;
		}
	}

	return (ret);
}

static bool
parse_int32(const char *str, int32_t *val) {
	bool ret = false;
	int32_t base = 10;
	int32_t neg = 1;

	skip_spaces(str);
	switch ((int)str[0]) {
        case '-':
		neg = -1;
		str++;
		break;
        case '+':
		str++;
		break;
	}
	skip_spaces(str);

	if (strncasecmp(str, "0x", 2) == 0 ||
	    strncasecmp(str, "\\x", 2) == 0) {
		base = 16;
		str += 2;
	} else if (strncasecmp(str, "\\0", 2) == 0) {
		base = 8;
		str += 2;
	} else if (str[0] == 'H' || str[0] == 'h') {
		base = 16;
		str += 1;
	} else if (str[0] == 'B' || str[0] == 'b') {
		base = 2;
		str += 1;
	}

	ret = parse_int32_by_base(str, val, base);
	if (ret == true)
		*val = *val * neg;

	return (ret);
}

static gfarm_repattr_t
allocate_repattr(const char *fsngroupname, size_t n)
{
	bool is_ok = false;
	char *tmp_fsng = NULL;
	struct gfarm_repattr *ret =	(struct gfarm_repattr *)
		malloc(sizeof(struct gfarm_repattr));
	if (ret == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocate_repattr(): %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	tmp_fsng = strdup(fsngroupname);
	if (tmp_fsng == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocate_repattr(): %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	ret->fsngroupname = tmp_fsng;
	ret->n = n;
	is_ok = true;

done:
	if (!is_ok) {
		free(tmp_fsng);
		free(ret);
		ret = NULL;
	}

	return ((gfarm_repattr_t)ret);
}

static void
destroy_repattr(gfarm_repattr_t rep)
{
	struct gfarm_repattr *r = (struct gfarm_repattr *)rep;

	if (r != NULL) {
		free(r->fsngroupname);
		free(r);
	}
}

/*****************************************************************************/
/*
 * Exported APIs:
 */

gfarm_error_t
gfarm_repattr_parse(const char *s, gfarm_repattr_t **retp, size_t *n_retp)
{
	/*
	 * anattr := string ':' number
	 *
	 * attr := anattr | anattr ',' attr
	 */

	size_t ret = 0;
	size_t n_maxreps;
	size_t n_tokens;
	size_t i;
	int32_t spec_n;
	char **tokens = NULL;
	char *tokens2[3];
	bool is_ok = false;
	gfarm_repattr_t *reps = NULL;
	gfarm_repattr_t rep;
	char *buf = NULL;
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	buf = strdup(s);
	if (buf == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED, "gfarm_repattr_parse(): %s",
			gfarm_error_string(e));
		goto done;
	}

	n_tokens = tokenize(buf, NULL, INT_MAX, ",");
	if (n_tokens == 0)
		goto done;

	GFARM_MALLOC_ARRAY(tokens, n_tokens);
	if (tokens == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "gfarm_repattr_parse:() %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}

	n_tokens = tokenize(buf, tokens, n_tokens, ",");
	if (n_tokens == 0)
		goto done;

	GFARM_MALLOC_ARRAY(reps, n_tokens);
	if (reps == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED, "gfarm_repattr_parse:() %s",
			gfarm_error_string(e));
		goto done;
	}
	(void)memset((void *)reps, 0, sizeof(*reps) * n_tokens);
	n_maxreps = n_tokens;

	for (i = 0; i < n_maxreps; i++) {
		if (strchr(tokens[i], ':') == NULL)
			continue;
		n_tokens = tokenize(tokens[i], tokens2, GFARM_ARRAY_LENGTH(tokens2), ": \t\r\n");
		if (n_tokens == 2) {
			if (!parse_int32(tokens2[1], &spec_n))
				continue;
			if (spec_n < 1)
				continue;
			rep = allocate_repattr(tokens2[0], spec_n);
			if (rep != NULL)
				reps[ret++] = rep;
			else
				goto done;
		}
	}
	is_ok = true;

done:
	free(buf);
	free(tokens);
	if (!is_ok || retp == NULL) {
		if (reps != NULL) {
			for (i = 0; i < ret; i++)
				destroy_repattr(reps[i]);
			free(reps);
		}
		if (!is_ok)
			ret = 0;
	} else {
		if (retp != NULL)
			*retp = reps;
	}

	if (e == GFARM_ERR_NO_ERROR)
		*n_retp = ret;
	return (e);
}

void
gfarm_repattr_free(gfarm_repattr_t rep)
{
	destroy_repattr(rep);
}

const char *
gfarm_repattr_group(gfarm_repattr_t rep)
{
	struct gfarm_repattr *r = rep;
	return (r->fsngroupname);
}

size_t
gfarm_repattr_amount(gfarm_repattr_t rep)
{
	struct gfarm_repattr *r = rep;
	return (r->n);
}

gfarm_error_t
gfarm_repattr_stringify(gfarm_repattr_t *reps, size_t n, char **str_repattr)
{
	size_t i;
	int plen = 0;
	size_t len = 1;	/* for '\0' */
	int of;
	char *ret = NULL;
	char *last;

	for (i = 0, of = 0; i < n && of == 0; i++) {
		len = gfarm_size_add(&of, len,
		    gfarm_size_add(&of,
		    GFARM_INT32STRLEN + 2 /*':' + ',' */,
		    strlen(gfarm_repattr_group(reps[i]))));
	}
	if (of == 0 && len > 0)
		ret = (char *)malloc(len);
	if (ret == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_repattr_reduce(): %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}

	for (i = 0; i < n; i++) {
		plen += snprintf((ret + plen), len - (size_t)plen, "%s:%zu,",
				gfarm_repattr_group(reps[i]),
				0xffffffff &
				gfarm_repattr_amount(reps[i]));
	}
	last = strrchr(ret, ',');
	if (last != NULL)
		*last = '\0';

	*str_repattr = ret;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_repattr_reduce(const char *s, gfarm_repattr_t **retp, size_t *n_repp)
{
	bool is_ok = false;
	size_t i;
	size_t ret = 0;
	size_t nreps_before = 0;
	gfarm_repattr_t *reps_before = NULL;
	gfarm_repattr_t *reps = NULL;
	gfarm_repattr_t tmp;
	struct gfarm_hash_table *tbl = NULL;
	struct gfarm_hash_iterator it;
	struct gfarm_hash_entry *entry;
	const char *fsngroupname;
	size_t amount;
	int isnew;
	gfarm_error_t e;

	/*
	 * Parse the repattr string first.
	 */
	if ((e = gfarm_repattr_parse(s, &reps_before, &nreps_before))
	    != GFARM_ERR_NO_ERROR)
		return (e);
	if (nreps_before == 0) {
		if (retp != NULL)
			*retp = NULL;
		*n_repp = 0;
		return (e);
	}

	/*
	 * Initialize a hash table.
	 */
	tbl = gfarm_hash_table_alloc(3079,
		gfarm_hash_strptr, gfarm_hash_key_equal_strptr);
	if (tbl == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_repattr_reduce(): %s",
			gfarm_error_string(e));
		goto done;
	}

	/*
	 * Merge the repattr(s) into the hash table.
	 */
	for (i = 0; i < nreps_before; i++) {
		isnew = 0;
		fsngroupname = gfarm_repattr_group(reps_before[i]);
		amount = gfarm_repattr_amount(reps_before[i]);
		entry = fsngroup_hash_new_entry(tbl, fsngroupname, &isnew);
		if (entry == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_UNFIXED,
				"gfarm_repattr_reduce(): %s",
				gfarm_error_string(e));
			goto done;
		}

		if (isnew == 0)
			/*
			 * Increase the amount.
			 */
			amount += entry_amount_get(entry);
		else
			/*
			 * It's a new groupname. Increase the total info #.
			 */
			ret++;

		entry_amount_set(entry, amount);
	}

	assert(ret > 0);

	/*
	 * Allocate repattr(s) to be returned.
	 */
	GFARM_MALLOC_ARRAY(reps, ret);
	if (reps == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfarm_repattr_reduce(): %s",
			gfarm_error_string(e));
		goto done;
	}
	(void)memset((void *)reps, 0, sizeof(*reps) * ret);

	/*
	 * Generate repattr(s) from the contents of the hash table.
	 */
	i = 0;
	FOR_EACH_IN_HASH(tbl, &it) {
		tmp = allocate_repattr(
			iter_fsngroupname(&it), iter_amount_get(&it));
		if (tmp != NULL)
			reps[i++] = tmp;
		else
			goto done;
	}

	assert(ret == i);

	is_ok = true;

done:
	if (tbl != NULL) {
		gfarm_hash_table_free(tbl);
	}
	if (nreps_before > 0 && reps_before != NULL)
		for (i = 0; i < nreps_before; i++)
			destroy_repattr(reps_before[i]);
	free(reps_before);

	if (!is_ok || retp == NULL) {
		if (reps != NULL)
			for (i = 0; i < ret; i++)
				destroy_repattr(reps[i]);
		free(reps);
		if (!is_ok)
			ret = 0;
	} else {
		if (retp != NULL)
			*retp = reps;
	}

	if (e == GFARM_ERR_NO_ERROR)
		*n_repp = ret;
	return (e);
}

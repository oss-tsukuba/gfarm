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
#if __STDC_VERSION__ >= 199901L
#include <stdbool.h>
#endif /* __STDC_VERSION__ >= 199901L */

#include <gfarm/gfarm.h>

/*****************************************************************************/

struct gfarm_replicainfo {
	char *fsngroupname;
	size_t n;
};

#if !defined(__cplusplus) || __STDC_VERSION__ < 199901L
typedef enum {
	false = 0,
	true = 1
} bool;
#endif /* !__cplusplus || __STDC_VERSION__ < 199901L */

/*****************************************************************************/
/*
 * Internals:
 */

#define skip_spaces(s)					    \
	while (*(s) != '\0' && isspace((int)*(s)) != 0) {   \
		(s)++;					    \
	}

#define trim_spaces(b, s)                                \
	while ((s) >= (b) && isspace((int)*(s)) != 0) {	 \
		*(s)-- = '\0';				 \
	}

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

	free((void *)buf);

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

static gfarm_replicainfo_t
allocate_replicainfo(const char *fsngroupname, size_t n)
{
	bool is_ok = false;
	char *tmp_fsng = NULL;
	struct gfarm_replicainfo *ret =	(struct gfarm_replicainfo *)
		malloc(sizeof(struct gfarm_replicainfo));
	if (ret == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocate_replicainfo: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	tmp_fsng = strdup(fsngroupname);
	if (tmp_fsng == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocate_replicainfo: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	ret->fsngroupname = tmp_fsng;
	ret->n = n;
	is_ok = true;

done:
	if (is_ok == false) {
		if (tmp_fsng != NULL)
			free((void *)tmp_fsng);
		if (ret != NULL) {
			free((void *)ret);
			ret = NULL;
		}
	}

	return ((gfarm_replicainfo_t)ret);
}

static void
destroy_replicainfo(gfarm_replicainfo_t rep)
{
	struct gfarm_replicainfo *r = (struct gfarm_replicainfo *)rep;

	if (r != NULL) {
		if (r->fsngroupname != NULL)
			free((void *)r->fsngroupname);
		free((void *)r);
	}
}

/*****************************************************************************/
/*
 * Exported APIs:
 */

size_t
gfarm_replicainfo_parse(const char *s, gfarm_replicainfo_t **retp)
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
	char *tokens[4096];
	char *tokens2[3];
	bool is_ok = false;
	gfarm_replicainfo_t *reps = NULL;
	gfarm_replicainfo_t rep;
	size_t len = strlen(s);
	char *buf = (char *)alloca(len + 1);

	if (buf == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "gfarm_replicainfo_parse: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	(void)memcpy((void *)buf, (void *)s, len);
	buf[len] = '\0';

	n_tokens = tokenize(buf, tokens, 4096, ",");
	if (n_tokens == 0)
		goto done;

	reps = (gfarm_replicainfo_t *)malloc(sizeof(gfarm_replicainfo_t) *
					n_tokens);
	if (reps == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "gfarm_replicainfo_parse: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		goto done;
	}
	n_maxreps = n_tokens;

	for (i = 0; i < n_maxreps; i++) {
		if (strchr(tokens[i], ':') == NULL)
			continue;
		n_tokens = tokenize(tokens[i], tokens2, 3, ": \t\r\n");
		if (n_tokens == 2) {
			if (parse_int32(tokens2[1], &spec_n) != true)
				continue;
			if (spec_n < 1)
				continue;
			rep = allocate_replicainfo(tokens2[0], spec_n);
			if (rep != NULL)
				reps[ret++] = rep;
		}
	}
	is_ok = true;

done:
	if (is_ok == false || retp == NULL) {
		if (reps != NULL) {
			for (i = 0; i < ret; i++)
				destroy_replicainfo(reps[i]);
			free((void *)reps);
		}
	} else {
		if (retp != NULL)
			*retp = reps;
	}

	return (ret);
}

void
gfarm_replicainfo_free(gfarm_replicainfo_t rep)
{
	destroy_replicainfo(rep);
}

const char *
gfarm_replicainfo_group(gfarm_replicainfo_t rep)
{
	struct gfarm_replicainfo *r = (struct gfarm_replicainfo *)rep;
	return (r->fsngroupname);
}

size_t
gfarm_replicainfo_amount(gfarm_replicainfo_t rep)
{
	struct gfarm_replicainfo *r = (struct gfarm_replicainfo *)rep;
	return (r->n);
}

char *
gfarm_replicainfo_stringify(gfarm_replicainfo_t *reps, size_t n)
{
	size_t i;
	int plen = 0;
	size_t len = 1;	/* for '\0' */
	char *ret = NULL;
	char *last;

	for (i = 0; i < n; i++) {
		/*
		 * 12 = 32 * log10(2) + ':' + ','
		 */
	    len += (strlen(gfarm_replicainfo_group(reps[i])) + 12);
	}
	ret = (char *)malloc(len);

	for (i = 0; i < n; i++) {
		plen = snprintf((ret + plen), len - (size_t)plen, "%s:%zu,",
				gfarm_replicainfo_group(reps[i]),
				0xffffffff &
				gfarm_replicainfo_amount(reps[i]));
	}
	last = strrchr(ret, ',');
	if (last != NULL)
		*last = '\0';

	return (ret);
}

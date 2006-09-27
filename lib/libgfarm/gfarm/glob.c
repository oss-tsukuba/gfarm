#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#include <gfarm/gfarm.h>
#include "gfutil.h"

#define GFS_GLOB_INITIAL	200
#define GFS_GLOB_DELTA		200

char *
gfs_glob_init(gfs_glob_t *listp)
{
	unsigned char *v;

	GFARM_MALLOC_ARRAY(v, GFS_GLOB_INITIAL);
	if (v == NULL)
		return (GFARM_ERR_NO_MEMORY);
	listp->size = GFS_GLOB_INITIAL;
	listp->length = 0;
	listp->array = v;
	return (NULL);
}

void
gfs_glob_free(gfs_glob_t *listp)
{
	free(listp->array);

	/* the following is not needed, but to make erroneous program abort */
	listp->size = 0;
	listp->length = 0;
	listp->array = NULL;
}

char *
gfs_glob_add(gfs_glob_t *listp, int dtype)
{
	int length = gfs_glob_length(listp);

	if (length >= listp->size) {
		int n = listp->size + GFS_GLOB_DELTA;
		unsigned char *t;

		GFARM_REALLOC_ARRAY(t, listp->array, n);
		if (t == NULL)
			return (GFARM_ERR_NO_MEMORY);
		listp->size = n;
		listp->array = t;
	}
	listp->array[length] = dtype;
	listp->length++;
	return (NULL);
}


/*
 * gfs_glob
 */
static void
glob_pattern_to_name(char *name, const char *pattern, int length)
{
	int i, j;

	for (i = j = 0; j < length; i++, j++) {
		if (pattern[j] == '\\') {
			if (pattern[j + 1] != '\0' &&
			    pattern[j + 1] != '/')
				j++;
		}
		name[i] = pattern[j];
	}
	name[i] = '\0';
}

static int
glob_charset_parse(const char *pattern, int index, int *ip)
{
	int i = index;

	if (pattern[i] == '!')
		i++;
	if (pattern[i] != '\0') {
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	while (pattern[i] != ']') {
		if (pattern[i] == '\0') {
			/* end of charset isn't found */
			if (ip != NULL)
				*ip = index;
			return (0);
		}
		if (pattern[i + 1] == '-' && pattern[i + 2] != '\0')
			i += 3;
		else
			i++;
	}
	if (ip != NULL)
		*ip = i;
	return (1);
}

static int
glob_charset_match(int ch, const char *pattern, int pattern_length)
{
	int i = 0, negate = 0;
	unsigned char c = ch, *p = (unsigned char *)pattern;

	if (p[i] == '!') {
		negate = 1;
		i++;
	}
	while (i < pattern_length) {
		if (p[i + 1] == '-' && p[i + 2] != '\0') {
			if (p[i] <= c && c <= p[i + 2])
				return (!negate);
			i += 3;
		} else {
			if (c == p[i])
				return (!negate);
			i++;
		}
	}
	return (negate);
}

static int
glob_name_submatch(char *name, const char *pattern, int namelen)
{
	int w;

	for (; --namelen >= 0; name++, pattern++){
		if (*pattern == '?')
			continue;
		if (*pattern == '[' &&
		    glob_charset_parse(pattern, 1, &w)) {
			if (glob_charset_match(*(unsigned char *)name,
			    pattern + 1, w - 1)) {
				pattern += w;
				continue;
			}
			return (0);
		}
		if (*pattern == '\\' &&
		    pattern[1] != '\0' && pattern[1] != '/') {
			if (*name == pattern[1]) {
				pattern++;
				continue;
			}
		}
		if (*name != *pattern)
			return (0);
	}
	return (1);
}

static int
glob_prefix_length_to_asterisk(const char *pattern, int pattern_length,
	const char **asterisk)
{
	int i, length = 0;

	for (i = 0; i < pattern_length; length++, i++) {
		if (pattern[i] == '\\') {
			if (i + 1 < pattern_length  &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '*') {
			*asterisk = &pattern[i];
			return (length);
		} else if (pattern[i] == '[') {
			glob_charset_parse(pattern, i + 1, &i);
		}
	}
	*asterisk = &pattern[i];
	return (length);
}

static int
glob_name_match(char *name, const char *pattern, int pattern_length)
{
	const char *asterisk;
	int residual = strlen(name);
	int sublen = glob_prefix_length_to_asterisk(pattern, pattern_length,
	    &asterisk);

	if (residual < sublen || !glob_name_submatch(name, pattern, sublen))
		return (0);
	if (*asterisk == '\0')
		return (residual == sublen);
	for (;;) {
		name += sublen; residual -= sublen;
		pattern_length -= asterisk + 1 - pattern;
		pattern = asterisk + 1;
		sublen = glob_prefix_length_to_asterisk(pattern,
		    pattern_length, &asterisk);
		if (*asterisk == '\0')
			break;
		for (;; name++, --residual){
			if (residual < sublen)
				return (0);
			if (glob_name_submatch(name, pattern, sublen))
				break;
		}
	}
	return (residual >= sublen &&
	    glob_name_submatch(name + residual - sublen, pattern, sublen));
}

static char GFARM_ERR_PATHNAME_TOO_LONG[] = "pathname too long";

#define GLOB_PATH_BUFFER_SIZE	(PATH_MAX * 2)

static char *
gfs_glob_sub(char *path_buffer, char *path_tail, const char *pattern,
	gfarm_stringlist *paths, gfs_glob_t *types)
{
	char *s, *e, *e_save = NULL;
	int i, nomagic, dirpos = -1;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	struct gfs_stat st;

	for (i = 0; pattern[i] != '\0'; i++) {
		if (pattern[i] == '\\') {
			if (pattern[i + 1] != '\0' &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '/') {
			dirpos = i;
		} else if (pattern[i] == '?' || pattern[i] == '*') {
			break;
		} else if (pattern[i] == '[') {
			if (glob_charset_parse(pattern, i + 1, NULL))
				break;
		}
	}
	if (pattern[i] == '\0') { /* no magic */
		if (path_tail - path_buffer + strlen(pattern) >
		    GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		glob_pattern_to_name(path_tail, pattern, strlen(pattern));
		e = gfs_stat(path_buffer, &st);
		if (e != NULL)
			return (e);
		s = gfarm_url_prefix_add(path_buffer);
		if (s == NULL) {
			gfs_stat_free(&st);
			return (GFARM_ERR_NO_MEMORY);
		}
		gfarm_stringlist_add(paths, s);
		if (GFARM_S_ISDIR(st.st_mode))
			gfs_glob_add(types, GFS_DT_DIR);
		else
			gfs_glob_add(types, GFS_DT_REG);
		gfs_stat_free(&st);
		return (NULL);
	}
	nomagic = i;
	if (dirpos >= 0) {
		int dirlen = dirpos == 0 ? 1 : dirpos;

		if (path_tail - path_buffer + dirlen > GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		glob_pattern_to_name(path_tail, pattern, dirlen);
		path_tail += strlen(path_tail);
	}
	dirpos++;
	for (i = nomagic; pattern[i] != '\0'; i++) {
		if (pattern[i] == '\\') {
			if (pattern[i + 1] != '\0' &&
			    pattern[i + 1] != '/')
				i++;
		} else if (pattern[i] == '/') {
			break;
		} else if (pattern[i] == '?' || pattern[i] == '*') {
		} else if (pattern[i] == '[') {
			glob_charset_parse(pattern, i + 1, &i);
		}
	}
	e = gfs_opendir(path_buffer, &dir);
	if (e != NULL)
		return (e);
	if (path_tail > path_buffer && path_tail[-1] != '/') {
		if (path_tail - path_buffer + 1 > GLOB_PATH_BUFFER_SIZE)
			return (GFARM_ERR_PATHNAME_TOO_LONG);
		*path_tail++ = '/';
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		if (entry->d_name[0] == '.' && pattern[dirpos] != '.')
			continue; /* initial '.' must be literally matched */
		if (!glob_name_match(entry->d_name, &pattern[dirpos],
		    i - dirpos))
			continue;
		if (path_tail - path_buffer + strlen(entry->d_name) >
		    GLOB_PATH_BUFFER_SIZE) {
			if (e_save == NULL)
				e_save = GFARM_ERR_PATHNAME_TOO_LONG;
			continue;
		}
		strcpy(path_tail, entry->d_name);
		if (pattern[i] == '\0') {
			s = gfarm_url_prefix_add(path_buffer);
			if (s == NULL)
				return (GFARM_ERR_NO_MEMORY);
			gfarm_stringlist_add(paths, s);
			gfs_glob_add(types, entry->d_type);
			continue;
		}
		e = gfs_glob_sub(path_buffer, path_tail + strlen(path_tail),
		    pattern + i, paths, types);
		if (e_save == NULL)
			e_save = e;
	}
	gfs_closedir(dir);
	return (e_save);
}

char *
gfs_glob(const char *pattern, gfarm_stringlist *paths, gfs_glob_t *types)
{
	const char *s;
	char *p = NULL, *e = NULL;
	int len, n = gfarm_stringlist_length(paths);
	char path_buffer[GLOB_PATH_BUFFER_SIZE + 1];
	size_t size;
	int overflow = 0;

	pattern = gfarm_url_prefix_skip(pattern);
	if (*pattern == '~') {
		if (pattern[1] == '\0' || pattern[1] == '/') {
			s = gfarm_get_global_username();
			if (s == NULL)
				return (
				    "gfs_glob(): programming error, "
				    "gfarm library isn't properly initialized");
			len = strlen(s);
			pattern++;
		} else {
			s = pattern + 1;
			len = strcspn(s, "/");
			pattern += 1 + len;
		}
		size = gfarm_size_add(&overflow, 1 + len, strlen(pattern) + 1);
		if (!overflow)
			GFARM_MALLOC_ARRAY(p, size);
		if (overflow || p == NULL) {
			e = GFARM_ERR_PATHNAME_TOO_LONG;
		} else {
			p[0] = '/';
			memcpy(p + 1, s, len);
			strcpy(p + 1 + len, pattern);
			pattern = p;
		}
	} else {
		strcpy(path_buffer, ".");
	}
	if (e == NULL) {
		e = gfs_glob_sub(path_buffer, path_buffer, pattern,
		    paths, types);
	}
	if (gfarm_stringlist_length(paths) <= n) {
		gfarm_stringlist_add(paths, gfarm_url_prefix_add(pattern));
		gfs_glob_add(types, GFS_DT_UNKNOWN);
	}
	if (p != NULL)
		free(p);
	return (e);
}


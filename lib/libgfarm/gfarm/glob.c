#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "liberror.h"
#include "patmatch.h"

#define GFS_GLOB_INITIAL	200
#define GFS_GLOB_DELTA		200

gfarm_error_t
gfs_glob_init(gfs_glob_t *listp)
{
	unsigned char *v;

	GFARM_MALLOC_ARRAY(v, GFS_GLOB_INITIAL);
	if (v == NULL) {
		gflog_debug(GFARM_MSG_1001414,
			"allocation of array failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	listp->size = GFS_GLOB_INITIAL;
	listp->length = 0;
	listp->array = v;
	return (GFARM_ERR_NO_ERROR);
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

gfarm_error_t
gfs_glob_add(gfs_glob_t *listp, int dtype)
{
	int length = gfs_glob_length(listp);

	if (length >= listp->size) {
		int n = listp->size + GFS_GLOB_DELTA;
		unsigned char *t;

		GFARM_REALLOC_ARRAY(t, listp->array, n);
		if (t == NULL) {
			gflog_debug(GFARM_MSG_1001415,
				"re-allocation of array failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		listp->size = n;
		listp->array = t;
	}
	listp->array[length] = dtype;
	listp->length++;
	return (GFARM_ERR_NO_ERROR);
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

#define GLOB_PATH_BUFFER_SIZE	(PATH_MAX * 2)

static gfarm_error_t
gfs_glob_sub(char *path_buffer, char *path_tail, const char *pattern,
	gfarm_stringlist *paths, gfs_glob_t *types)
{
	gfarm_error_t e, e_save = GFARM_ERR_NO_ERROR;
	char *s;
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
			if (gfarm_pattern_charset_parse(pattern, i + 1, NULL))
				break;
		}
	}
	if (pattern[i] == '\0') { /* no magic */
		if (path_tail - path_buffer + strlen(pattern) >
		    GLOB_PATH_BUFFER_SIZE) {
			gflog_debug(GFARM_MSG_1001416,
				"File name is too long: %s",
				gfarm_error_string(
					GFARM_ERR_FILE_NAME_TOO_LONG));
			return (GFARM_ERR_FILE_NAME_TOO_LONG);
		}
		glob_pattern_to_name(path_tail, pattern, strlen(pattern));
		e = gfs_lstat(path_buffer, &st);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001417,
				"gfs_lstat(%s) failed: %s",
				path_buffer,
				gfarm_error_string(e));
			return (e);
		}
		s = strdup(path_buffer);
		if (s == NULL) {
			gfs_stat_free(&st);
			gflog_debug(GFARM_MSG_1001418,
				"allocation of path string failed: %s",
				gfarm_error_string(GFARM_ERR_NO_MEMORY));
			return (GFARM_ERR_NO_MEMORY);
		}
		gfarm_stringlist_add(paths, s);
		gfs_glob_add(types, gfs_mode_to_type(st.st_mode));
		gfs_stat_free(&st);
		return (GFARM_ERR_NO_ERROR);
	}
	nomagic = i;
	if (dirpos >= 0) {
		int dirlen = dirpos == 0 ? 1 : dirpos;

		if (path_tail - path_buffer + dirlen > GLOB_PATH_BUFFER_SIZE) {
			gflog_debug(GFARM_MSG_1001419,
				"File name is too long: %s",
				gfarm_error_string(
					GFARM_ERR_FILE_NAME_TOO_LONG));
			return (GFARM_ERR_FILE_NAME_TOO_LONG);
		}
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
			; /* nothing to do */;
		} else if (pattern[i] == '[') {
			gfarm_pattern_charset_parse(pattern, i + 1, &i);
		}
	}
	e = gfs_opendir(path_buffer, &dir);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001420,
			"gfs_opendir(%s) failed: %s",
			path_buffer,
			gfarm_error_string(e));
		return (e);
	}
	if (path_tail > path_buffer && path_tail[-1] != '/') {
		if (path_tail - path_buffer + 1 > GLOB_PATH_BUFFER_SIZE) {
			gflog_debug(GFARM_MSG_1001421,
				"File name is too long: %s",
				gfarm_error_string(
					GFARM_ERR_FILE_NAME_TOO_LONG));
			return (GFARM_ERR_FILE_NAME_TOO_LONG);
		}
		*path_tail++ = '/';
	}
	while ((e = gfs_readdir(dir, &entry)) == GFARM_ERR_NO_ERROR &&
	    entry != NULL) {
		if (entry->d_name[0] == '.' && pattern[dirpos] != '.')
			continue; /* initial '.' must be literally matched */
		if (!gfarm_pattern_submatch(&pattern[dirpos], i - dirpos,
		    entry->d_name, GFARM_PATTERN_PATHNAME))
			continue;
		if (path_tail - path_buffer + strlen(entry->d_name) >
		    GLOB_PATH_BUFFER_SIZE) {
			if (e_save == GFARM_ERR_NO_ERROR)
				e_save = GFARM_ERR_FILE_NAME_TOO_LONG;
			continue;
		}
		strcpy(path_tail, entry->d_name);
		if (pattern[i] == '\0') {
			s = strdup(path_buffer);
			if (s == NULL) {
				gflog_debug(GFARM_MSG_1001422,
				    "allocation of path string failed:"
				    " %s",
				    gfarm_error_string(GFARM_ERR_NO_MEMORY));
				return (GFARM_ERR_NO_MEMORY);
			}
			gfarm_stringlist_add(paths, s);
			gfs_glob_add(types, entry->d_type);
			continue;
		}
		e = gfs_glob_sub(path_buffer, path_tail + strlen(path_tail),
		    pattern + i, paths, types);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	gfs_closedir(dir);

	if (e_save != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001423,
			"error occurred during process(%s): %s",
			path_buffer,
			gfarm_error_string(e_save));
	}

	return (e_save);
}

gfarm_error_t
gfs_glob(const char *pattern, gfarm_stringlist *paths, gfs_glob_t *types)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	char *p = NULL;
	int n = gfarm_stringlist_length(paths);
	char path_buffer[GLOB_PATH_BUFFER_SIZE + 1];
#if 0 /* XXX FIXME - "~" handling isn't implemented on v2, yet */
	size_t size;
	int overflow = 0;

	const char *s;
	int len;

	if (*pattern == '~') {
		if (pattern[1] == '\0' || pattern[1] == '/') {
			s = gfarm_get_global_username();
			if (s == NULL)
				return (
				 GFARM_ERRMSG_GFS_GLOB_NOT_PROPERLY_INITIALIZED
				);
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
			e = GFARM_ERR_FILE_NAME_TOO_LONG;
		} else {
			p[0] = '/';
			memcpy(p + 1, s, len);
			strcpy(p + 1 + len, pattern);
			pattern = p;
		}
	} else
#endif
	{
		strcpy(path_buffer, ".");
	}
	if (e == GFARM_ERR_NO_ERROR) {
		e = gfs_glob_sub(path_buffer, path_buffer, pattern,
		    paths, types);
	}
	if (gfarm_stringlist_length(paths) <= n) { /* doesn't match */
		/* in that case, add the pattern itself */
		gfarm_stringlist_add(paths, strdup(pattern));
		gfs_glob_add(types, GFS_DT_UNKNOWN);
	}
	if (p != NULL)
		free(p);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001424,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}

	return (e);
}


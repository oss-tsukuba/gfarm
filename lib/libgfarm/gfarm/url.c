/*
 * $Id$
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <gfarm/gfarm_error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

/*
 * GFarm-URL:
 *	gfarm:path/name
 *	gfarm:~/path/name
 *		= ${gfarm_spool_root}/${USER}/path/name
 *	gfarm:/path/name
 *		= ${gfarm_spool_root}/path/name
 */

/*
 * Remove "gfarm:" prefix and expand "~" and current directory.
 * Return malloc(3)ed string, thus caller should free(3) the memory.
 *
 * Do not add ${gfarm_spool_root}, because it is only available on
 * gfarm pool hosts.
 *
 * i.e.
 *	input: gfarm:path/name
 *	output: ${USER}/path/name
 *
 *	input: gfarm:~/path/name
 *	output: ${USER}/path/name
 *
 *	input: gfarm:/path/name
 *	output: path/name
 *
 *	input: gfarm:~user/path/name
 *	output: user/path/name
 */

char GFARM_URL_PREFIX[] = "gfarm:";

char *
gfarm_canonical_path(const char *gfarm_file, char **canonic_pathp)
{
	char *s, *user, *e, *t;

	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (gfarm_file[0] == '~' &&
	    (gfarm_file[1] == '\0' || gfarm_file[1] == '/')) {
		user = gfarm_get_global_username();
		s = malloc(strlen(user) + strlen(&gfarm_file[1]) + 2);
		if (s == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(s, "/%s%s", user, &gfarm_file[1]);
	} else {
		s = strdup(gfarm_file);
		if (s == NULL)
			return (GFARM_ERR_NO_MEMORY);
		if (gfarm_file[0] == '~') /* ~username/... */
			*s = '/';
	}
	e = gfs_realpath(s, &t);
	free(s);
	if (e != NULL)
		return(e);
	*canonic_pathp = strdup(t + GFARM_URL_PREFIX_LENGTH + 1);
	free(t);
	if (*canonic_pathp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	return (NULL);
}

char *
gfarm_canonical_path_for_creation(const char *gfarm_file, char **canonic_pathp)
{
	const char *basename;
	char *dir, *e, *dir_canonic;

	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	basename = gfarm_path_dir_skip(gfarm_file);
	dir = NULL;
	if (basename == gfarm_file) { /* "filename" */ 
		dir = strdup(".");
		if (dir == NULL)
			return (GFARM_ERR_NO_MEMORY);
	} else if (basename == gfarm_file + 1) { /* "/filename" */
		dir = strdup("/");
		if (dir == NULL)
			return (GFARM_ERR_NO_MEMORY);
	} else { /* /.../.../filename */
		dir = malloc(basename - 2 - gfarm_file + 2);
		if (dir == NULL)
			return (GFARM_ERR_NO_MEMORY);
		strncpy(dir, gfarm_file, basename - 2 - gfarm_file + 1);
		dir[basename - 2 - gfarm_file + 1] = '\0';
	}
	e = gfarm_canonical_path(dir, &dir_canonic);
	if (e != NULL)
		return(e);
	free(dir);
	*canonic_pathp = malloc(strlen(dir_canonic) + 1 +
				strlen(basename) + 1); 
	if (*canonic_pathp == NULL)
		return (GFARM_ERR_NO_MEMORY);
	/*
	 * When the 'dir_canonic' is a null string, *canonic_pathp
	 * will start with '/' incorrectly.
	 */
	if (strcmp(dir_canonic, "") == 0)
		strcpy(*canonic_pathp, basename);
	else
		sprintf(*canonic_pathp, "%s/%s", dir_canonic, basename);
	free(dir_canonic);
	return (NULL);
}

char *
gfarm_url_make_path(const char *gfarm_url, char **canonic_pathp)
{
	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (!gfarm_is_url(gfarm_url))
		return (GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING);
	gfarm_url += GFARM_URL_PREFIX_LENGTH;

	return (gfarm_canonical_path(gfarm_url, canonic_pathp));
}

char *
gfarm_url_make_path_for_creation(const char *gfarm_url, char **canonic_pathp)
{
	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (!gfarm_is_url(gfarm_url))
		return (GFARM_ERR_GFARM_URL_PREFIX_IS_MISSING);
	gfarm_url += GFARM_URL_PREFIX_LENGTH;

	return (gfarm_canonical_path_for_creation(gfarm_url, canonic_pathp));
}

int
gfarm_is_url(const char *gfarm_url)
{
	return (memcmp(gfarm_url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH)
	    == 0);
}

/*
 * Append section suffix to pathname.
 * Return malloc(3)ed string, thus caller should free(3) the memory.
 * i.e.
 *	input1: pathname
 *	input2: section
 *	output: pathname:section
 */
char *
gfarm_path_section(const char *pathname, const char *section,
	char **section_pathp)
{
	char *s;

	*section_pathp = NULL; /* cause SEGV, if return value is ignored */

	s = malloc(strlen(pathname) + 1 + strlen(section) + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(s, "%s:%s", pathname, section);
	*section_pathp = s;
	return (NULL);
}

/*
 * Add spool_root prefix and section suffix to canonic path.
 * Return malloc(3)ed string, thus caller should free(3) the memory.
 *
 * i.e.
 *	input1: /spool_root
 *	input2: path/name
 *	input3: section
 *	output: /spool_root/path/name:section
 */
char *
gfarm_full_path_file_section(
	char *spool_root, char *canonic_path, char *section,
	char **abs_pathp)
{
	char *s;

	*abs_pathp = NULL; /* cause SEGV, if return value is ignored */

	s = malloc(strlen(spool_root) + 1 + strlen(canonic_path) +
		   1 + strlen(section) + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(s, "%s/%s:%s", spool_root, canonic_path, section);
	*abs_pathp = s;
	return (NULL);
}

/*
 * Add ${gfarm_spool_root} prefix to canonic path.
 * Return malloc(3)ed string, thus caller should free(3) the memory.
 *
 * Should be called on gfarm pool hosts, because ${gfarm_spool_root} is
 * only available on pool.
 *
 * i.e.
 *	input: path/name
 *	output: ${gfarm_spool_root}/path/name
 */
char *
gfarm_path_localize(char *canonic_path, char **abs_pathp)
{
	char *s;

	*abs_pathp = NULL; /* cause SEGV, if return value is ignored */

	s = malloc(strlen(gfarm_spool_root) + 1 + strlen(canonic_path) + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(s, "%s/%s", gfarm_spool_root, canonic_path);
	*abs_pathp = s;
	return (NULL);
}

/*
 * Add ${gfarm_spool_root} prefix and section suffix to canonic path.
 * Return malloc(3)ed string, thus caller should free(3) the memory.
 *
 * Should be called on gfarm pool hosts, because ${gfarm_spool_root} is
 * only available on pool.
 *
 * i.e.
 *	input1: path/name
 *	input2: section
 *	output: ${gfarm_spool_root}/path/name:section
 */
char *
gfarm_path_localize_file_section(char *canonic_path, char *section,
				 char **abs_pathp)
{
	return (gfarm_full_path_file_section(gfarm_spool_root,
	    canonic_path, section, abs_pathp));
}

char *
gfarm_path_localize_file_fragment(char *canonic_path, int index,
				  char **abs_pathp)
{
	char buffer[GFARM_INT32STRLEN];

	sprintf(buffer, "%d", index);
	return (gfarm_path_localize_file_section(canonic_path, buffer,
	    abs_pathp));
}

/*
 *  Strip suffix from pathname.
 *
 *  It is necessary to free a returned string.
 */
char *
gfarm_url_remove_suffix(char *gfarm_url, char *suffix, char **out_urlp)
{
	char *r;
	int ulen = strlen(gfarm_url);
	int slen = strlen(suffix);

	if (ulen > slen) {
		if (memcmp(gfarm_url + ulen - slen, suffix, slen) == 0)
			ulen -= slen;
	}
	r = malloc(ulen + 1);
	if (r == NULL)
		return (GFARM_ERR_NO_MEMORY);
	memcpy(r, gfarm_url, ulen);
	r[ulen] = '\0';
	*out_urlp = r;
	return (NULL);
}

/*
 * convenience functions
 */

char *
gfarm_url_prefix_skip(char *gfarm_url)
{
	if (memcmp(gfarm_url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH) == 0)
		gfarm_url += GFARM_URL_PREFIX_LENGTH;
	return (gfarm_url);
}

/*
 * Skip directory in the pathname.
 * We want traditional basename(3) here, rather than weird XPG one.
 */
const char *
gfarm_path_dir_skip(const char *path)
{
	const char *base;

	for (base = path; *path != '\0'; path++) {
		if (*path == '/')
			base = path + 1;
	}
	return (base);
}

#if 0
char *
gfarm_url_make_localized_path(char *gfarm_url, char **abs_pathp)
{
	char *e, *canonic_path;

	e = gfarm_url_make_path(gfarm_url, &canonic_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_localize(canonic_path, abs_pathp);
	free(canonic_path);
	return (e);
}

char *
gfarm_url_make_localized_file_fragment_path(char *gfarm_url, int index,
					    char **abs_pathp)
{
	char *e, *canonic_path;

	e = gfarm_url_make_path(gfarm_url, &canonic_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_localize_file_fragment(canonic_path, index, abs_pathp);
	free(canonic_path);
	return (e);
}
#endif


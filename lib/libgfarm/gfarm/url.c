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
#include <limits.h>
#include <assert.h>

#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "gfs_pio.h" /* gfs_realpath_canonical */

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

/*
 * Expand '~'.  Currently, '~/...' or '~username/...' is transformed
 * to '/username/...'.
 */
char *
gfarm_path_expand_home(const char *gfarm_file, char **pathp)
{
	char *s, *user;

	*pathp = NULL; /* cause SEGV, if return value is ignored */

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
		/* XXX - it is necessary to check the user name. */
	}
	*pathp = s;

	return (NULL);
}

char *
gfarm_canonical_path(const char *gfarm_file, char **canonic_pathp)
{
	char *s, *e;

	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	e = gfarm_path_expand_home(gfarm_file, &s);
	if (e != NULL)
		return (e);

	e = gfs_realpath_canonical(s, canonic_pathp);
	free(s);
	return(e);
}

char *
gfarm_canonical_path_for_creation(const char *gfarm_file, char **canonic_pathp)
{
	const char *basename;
	char *dir, *e, *dir_canonic;
	const char *lastc, *ini_lastc;

	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	/* Expand '~'. */
	if (gfarm_file[0] == '~') {
		char *expanded_gfarm_file;

		e = gfarm_path_expand_home(gfarm_file, &expanded_gfarm_file);
		if (e != NULL)
			return (e);
		assert(expanded_gfarm_file[0] != '~');
		e = gfarm_canonical_path_for_creation(
			expanded_gfarm_file, canonic_pathp);
		free(expanded_gfarm_file);

		return (e);
	}
	/* '' or 'gfarm:' case */
	if (gfarm_file[0] == '\0') {
		char cwd[PATH_MAX + 1];

		e = gfs_getcwd(cwd, sizeof(cwd));
		if (e != NULL)
			return (e);
		return (gfarm_canonical_path_for_creation(cwd, canonic_pathp));
	}
	/* Eliminate unnecessary '/'s following the basename. */
	lastc = ini_lastc = &gfarm_file[strlen(gfarm_file) - 1];
	while (gfarm_file < lastc && *lastc == '/')
		--lastc;
	if (gfarm_file == lastc) {
		/*
		 * In this case, given gfarm_file is '/' or contains
		 * only several '/'s.  This means to attempt to create
		 * the root directory.  Because the root directory
		 * should exist, the attempt will fail with the error
		 * of 'already exist'.  However, this case such that
		 * the canonical name is "" causes many problems.
		 * That is why the error of 'already exist' is
		 * returned here.
		 */
		return (GFARM_ERR_ALREADY_EXISTS);
	}
	else if (lastc != ini_lastc) {
		char *eliminated_gfarm_file;

		++lastc;
		eliminated_gfarm_file = malloc(lastc - gfarm_file + 1);
		if (eliminated_gfarm_file == NULL)
			return (GFARM_ERR_NO_MEMORY);
		strncpy(eliminated_gfarm_file, gfarm_file, lastc - gfarm_file);
		eliminated_gfarm_file[lastc - gfarm_file] = '\0';
		e = gfarm_canonical_path_for_creation(
			eliminated_gfarm_file, canonic_pathp);
		free(eliminated_gfarm_file);

		return (e);
	}

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

	/* Check the existence of the parent directory. */
	e = gfarm_canonical_path(dir, &dir_canonic);
	free(dir);
	if (e != NULL)
		return (e);

	*canonic_pathp = malloc(strlen(dir_canonic) + 1 +
				strlen(basename) + 1); 
	if (*canonic_pathp == NULL)
		return (GFARM_ERR_NO_MEMORY);

	/*
	 * When the 'dir_canonic' is a null string, *canonic_pathp
	 * will start with '/' incorrectly.
	 */
	if (dir_canonic[0] == '\0')
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
 * Translate a canonical path to a Gfarm URL.
 */
char *
gfarm_path_canonical_to_url(const char *canonic_path, char **gfarm_url)
{
	char *url;

	*gfarm_url = NULL;

	url = malloc(GFARM_URL_PREFIX_LENGTH + strlen(canonic_path) + 2);
	if (url == NULL)
		return (GFARM_ERR_NO_MEMORY);

	memcpy(url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH);
	url[GFARM_URL_PREFIX_LENGTH] = '/';
	strcpy(url + GFARM_URL_PREFIX_LENGTH + 1, canonic_path);

	*gfarm_url = url;

	return (NULL);
}

/*
 * gfs_realpath
 */

char *
gfs_realpath(const char *path, char **abspathp)
{
	char *e, *canonic_path;

	if (path == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);

	path = gfarm_url_prefix_skip(path);
	e = gfarm_canonical_path(path, &canonic_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_canonical_to_url(canonic_path, abspathp);
	free(canonic_path);
	return (e);
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

const char *
gfarm_url_prefix_skip(const char *gfarm_url)
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

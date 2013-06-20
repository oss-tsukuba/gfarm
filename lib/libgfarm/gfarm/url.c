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

#include <gfarm/gfarm.h>

#if 0
#include <openssl/evp.h>

#include "config.h"
#include "gfs_misc.h"

/*
 * XXX FIXME
 * note that unlike access(2), gfarm_stat_access() doesn't/can't check
 * access permission of ancestor directories.
 */
char *
gfs_stat_access(struct gfs_stat *gst, int mode)
{
	gfarm_mode_t mask = 0;

	if (strcmp(gst->st_user, gfarm_get_global_username()) == 0) {
		if (mode & X_OK)
			mask |= 0100;
		if (mode & W_OK)
			mask |= 0200;
		if (mode & R_OK)
			mask |= 0400;
#if 0 /* XXX - check against st_group */
	} else if (gfarm_is_group_member(gst->st_group)) {
		if (mode & X_OK)
			mask |= 0010;
		if (mode & W_OK)
			mask |= 0020;
		if (mode & R_OK)
			mask |= 0040;
#endif
	} else {
		if (mode & X_OK)
			mask |= 0001;
		if (mode & W_OK)
			mask |= 0002;
		if (mode & R_OK)
			mask |= 0004;
	}
	return (((gst->st_mode & mask) == mask) ?
		NULL : GFARM_ERR_PERMISSION_DENIED);
}

/*
 * XXX FIXME
 * note that unlike access(2), gfarm_path_info_access() doesn't/can't check
 * access permission of ancestor directories.
 */
char *
gfarm_path_info_access(struct gfarm_path_info *pi, int mode)
{
	return (gfs_stat_access(&pi->status, mode));
}

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
		if (user == NULL)
			return ("gfarm_path_expand_home(): programming error, "
				"gfarm library isn't properly initialized");
		GFARM_MALLOC_ARRAY(s,
			strlen(user) + strlen(&gfarm_file[1]) + 2);
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
	const char *basename, *p0;
	char *e, *p1, *dir, *dir_canonic, *lastc, cwd[PATH_MAX + 1];

	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	/* '' or 'gfarm:' case */
	if (gfarm_file[0] == '\0') {
		e = gfs_getcwd(cwd, sizeof(cwd));
		if (e != NULL)
			return (e);
		p0 = cwd;
	}
	else
		p0 = gfarm_file;

	/* Expand '~'. */
	e = gfarm_path_expand_home(p0, &p1);
	if (e != NULL)
		return (e);

	/* Eliminate unnecessary '/'s following the basename. */
	lastc = &p1[strlen(p1) - 1];
	if (*lastc == '/') {
		while (p1 < lastc && *lastc == '/')
			--lastc;
		if (p1 == lastc) {
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
			free(p1);
			return (GFARM_ERR_ALREADY_EXISTS);
		}
		else {
			*(lastc + 1) = '\0';
		}
	}

	basename = gfarm_path_dir_skip(p1);
	/* '.' or '..' - we do not have that entry. */
	if (basename[0] == '.' && (basename[1] == '\0' ||
		(basename[1] == '.' && basename[2] == '\0'))) {
		e = gfarm_canonical_path(p1, canonic_pathp);
		goto free_p1;
	}
	if (basename == p1)	     /* "filename" */
		dir = ".";
	else if (basename == p1 + 1) /* "/filename" */
		dir = "/";
	else {			     /* /.../.../filename */
		p1[basename - 1 - p1] = '\0';
		dir = p1;
	}
	/* Check the existence of the parent directory. */
	e = gfarm_canonical_path(dir, &dir_canonic);
	if (e != NULL)
		goto free_p1;

	/*
	 * check whether parent directory is writable or not.
	 * XXX this isn't enough yet, due to missing X-bits check.
	 */
	if (dir_canonic[0] != '\0') { /* XXX "/" is always OK for now */
		struct gfarm_path_info pi;
		int is_dir;

		e = gfarm_path_info_get(dir_canonic, &pi);
		if (e != NULL)
			goto free_dir_canonic;

		is_dir = GFARM_S_ISDIR(pi.status.st_mode);
		e = gfarm_path_info_access(&pi, W_OK);
		gfarm_path_info_free(&pi);
		if (!is_dir)
			e = GFARM_ERR_NOT_A_DIRECTORY;
		if (e != NULL)
			goto free_dir_canonic;
	}

	GFARM_MALLOC_ARRAY(*canonic_pathp, 
		strlen(dir_canonic) + 1 + strlen(basename) + 1); 
	if (*canonic_pathp == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_dir_canonic;
	}

	/*
	 * When the 'dir_canonic' is a null string, *canonic_pathp
	 * will start with '/' incorrectly.
	 */
	if (dir_canonic[0] == '\0')
		strcpy(*canonic_pathp, basename);
	else
		sprintf(*canonic_pathp, "%s/%s", dir_canonic, basename);
	e = NULL;
free_dir_canonic:
	free(dir_canonic);
free_p1:
	free(p1);
	return (e);
}

char *
gfarm_url_make_path(const char *gfarm_url, char **canonic_pathp)
{
	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (gfarm_url == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);
	gfarm_url = gfarm_url_prefix_skip(gfarm_url);

	return (gfarm_canonical_path(gfarm_url, canonic_pathp));
}

char *
gfarm_url_make_path_for_creation(const char *gfarm_url, char **canonic_pathp)
{
	*canonic_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (gfarm_url == NULL)
		return (GFARM_ERR_INVALID_ARGUMENT);
	gfarm_url = gfarm_url_prefix_skip(gfarm_url);

	return (gfarm_canonical_path_for_creation(gfarm_url, canonic_pathp));
}

/*
 * Translate a canonical path to a Gfarm URL.
 */
char *
gfarm_path_canonical_to_url(const char *canonic_path, char **gfarm_url)
{
	char *url;

	*gfarm_url = NULL;

	GFARM_MALLOC_ARRAY(url, 
		GFARM_URL_PREFIX_LENGTH + strlen(canonic_path) + 2);
	if (url == NULL)
		return (GFARM_ERR_NO_MEMORY);

	memcpy(url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH);
	url[GFARM_URL_PREFIX_LENGTH] = '/';
	strcpy(url + GFARM_URL_PREFIX_LENGTH + 1, canonic_path);

	*gfarm_url = url;

	return (NULL);
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

	GFARM_MALLOC_ARRAY(s, strlen(pathname) + 1 + strlen(section) + 1);
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

	GFARM_MALLOC_ARRAY(s, strlen(spool_root) + 1 + strlen(canonic_path) +
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
	char *s, *spool_root = gfarm_spool_root_for_compatibility;

	*abs_pathp = NULL; /* cause SEGV, if return value is ignored */

	if (spool_root == NULL)
		return ("gfarm_path_localize(): programming error, "
			"gfarm library isn't properly initialized");

	GFARM_MALLOC_ARRAY(s,
		strlen(spool_root) + 1 + strlen(canonic_path) + 1);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	sprintf(s, "%s/%s", spool_root, canonic_path);
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
	char *spool_root = gfarm_spool_root_for_compatibility;

	if (spool_root == NULL)
		return ("gfarm_path_localize_file_section(): "
			"programming error, "
			"gfarm library isn't properly initialized");

	return (gfarm_full_path_file_section(spool_root,
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
	GFARM_MALLOC_ARRAY(r, ulen + 1);
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
	if (gfarm_is_url(gfarm_url))
		gfarm_url += GFARM_URL_PREFIX_LENGTH;
	return (gfarm_url);
}

char *
gfarm_url_prefix_add(const char *s)
{
	char *p;

	GFARM_MALLOC_ARRAY(p, GFARM_URL_PREFIX_LENGTH + strlen(s) + 1);
	if (p == NULL)
		return (NULL);
	memcpy(p, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH);
	strcpy(p + GFARM_URL_PREFIX_LENGTH, s);
	return (p);
}
#endif

const char GFARM_URL_PREFIX[] = "gfarm:";
const char GFARM_PATH_ROOT[] = "/";

int
gfarm_is_url(const char *gfarm_url)
{
	return (memcmp(gfarm_url, GFARM_URL_PREFIX, GFARM_URL_PREFIX_LENGTH)
	    == 0);
}

const char *
gfarm_url_prefix_hostname_port_skip(const char *url)
{
	if (gfarm_is_url(url)) {
		url += GFARM_URL_PREFIX_LENGTH;
		if (url[0] == '/' && url[1] == '/') {
			url += 2; /* skip "//" */
			/* skip hostname:port */
			while (url[0] != '\0' && url[0] != '/')
				url++;
		}
	}
	return (url);
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

const char *
gfarm_url_dir_skip(const char *url)
{
	return (gfarm_path_dir_skip(gfarm_url_prefix_hostname_port_skip(url)));
}

/* similar to dirname(3) in libc, but returns the result by malloc'ed memory */
char *
gfarm_url_dir(const char *pathname)
{
	char *parent, *top, *dir, *p;
	int had_scheme = 0;
	static const char dot[] = ".";

	if (pathname[0] == '\0')
		return (strdup(dot));
	parent = strdup(pathname);
	if (parent == NULL) {
		gflog_debug(GFARM_MSG_1001463,
			"allocation of dir failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (NULL);
	}

	top = dir = parent;
	if (gfarm_is_url(dir)) {
		had_scheme = 1;
		dir += GFARM_URL_PREFIX_LENGTH;
		top = dir;
		if (dir[0] == '/') {
			if (dir[1] != '/') {
				top = dir;
			} else {
				dir += 2; /* skip "//" */
				/* skip hostname:port */
				while (dir[0] != '\0' && dir[0] != '/')
					dir++;
				top = dir - 1;
				if (dir[0] == '/') {
					for (p = dir + 1; *p == '/'; p++)
						;
					if (*p == '\0') {
						dir[0] = '\0';
						return (parent);
					}
				}
			}
		}
		if (dir[0] == '\0')
			return (parent);
	}
		
	/* remove trailing '/' */
	p = dir + strlen(dir) - 1;
	while (p > dir && *p == '/')
		--p;
	p[1] = '\0';

	p = (char *)gfarm_path_dir_skip(dir); /* UNCONST */
	if (p == dir) { /* i.e. no slash */
		if (had_scheme) {
			dir[0] = '\0';
			return (parent);
		} else {
			free(parent);
			return (strdup(dot));
		}
	}
	--p;

	/* remove trailing '/' */
	while (p > top && *p == '/')
		--p;
	p[1] = '\0';
	return (parent);
}

/* similar to dirname(3) in libc, but returns the result by malloc'ed memory */
char *
gfarm_path_dir(const char *pathname)
{
	char *dir, *p;
	static const char dot[] = ".";

	if (pathname[0] == '\0')
		return (strdup(dot));
	dir = strdup(pathname);
	if (dir == NULL) {
		gflog_debug(GFARM_MSG_1001463,
			"allocation of dir failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (NULL);
	}
		
	/* remove trailing '/' */
	p = dir + strlen(dir) - 1;
	while (p > dir && *p == '/')
		--p;
	p[1] = '\0';

	p = (char *)gfarm_path_dir_skip(dir); /* UNCONST */
	if (p == dir) { /* i.e. no slash */
		free(dir);
		return (strdup(dot));
	}
	--p;

	/* remove trailing '/' */
	while (p > dir && *p == '/')
		--p;
	p[1] = '\0';
	return (dir);
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

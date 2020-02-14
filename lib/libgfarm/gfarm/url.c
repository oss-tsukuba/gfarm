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

const char GFARM_URL_PREFIX[] = "gfarm:";
const char GFARM_PATH_ROOT[] = "/";

int
gfarm_is_url(const char *gfarm_url)
{
	int i;

	if (gfarm_url == NULL)
		return (0);
	for (i = 0; i < GFARM_URL_PREFIX_LENGTH; ++i)
		if (gfarm_url[i] != GFARM_URL_PREFIX[i])
			return (0);
	return (1);
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

/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <gfarm/gfarm.h>
#include "str_list.h"
#include "gfutil.h"
#include "gfpath.h"
#include "spool_root.h"

static char spool_root_default[] = GFARM_SPOOL_ROOT;
static struct gfarm_str_list *spool_list;

char *
gfarm_spool_root_set(char *spool)
{
	struct gfarm_str_list *s;

	spool = strdup(spool);
	if (spool == NULL)
		return (GFARM_ERR_NO_MEMORY);
	s = gfarm_str_list_cons(spool, spool_list);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	spool_list = s;
	return (NULL);
}

void
gfarm_spool_root_clear()
{
	gfarm_str_list_free_deeply(spool_list);
	spool_list = NULL;
}

void
gfarm_spool_root_set_default()
{
	struct gfarm_str_list *l;

	if (spool_list == NULL) {
		gfarm_str_list_cons(spool_root_default, spool_list);
	}
	l = gfarm_str_list_reverse(spool_list);
	gfarm_str_list_free(spool_list);
	spool_list = l;
}

char *
gfarm_spool_path(char *dir, char *file)
{
	char *s, *slash;

	if (dir == NULL || file == NULL)
		return (NULL);

	/* add '/' if necessary */
	if (*gfarm_path_dir_skip(gfarm_url_prefix_skip(dir)))
		slash = "/";
	else
		slash = "";

	GFARM_MALLOC_ARRAY(s, strlen(dir) + strlen(slash) + strlen(file) + 1);
	if (s == NULL)
		return (s);
	sprintf(s, "%s%s%s", dir, slash, file);
	return (s);
}

/* return the first entry for compatibility */
char *
gfarm_spool_root_get_for_compatibility()
{
	if (spool_list == NULL)
		gflog_fatal("gfarm_spool_root_get_for_compatibility(): "
			    "programming error, "
			    "gfarm library isn't properly initialized");

	return (gfarm_str_list_car(spool_list));
}

int
gfarm_spool_root_foreach(int (*func)(char *, void *), char *file, void *a)
{
	char *sp, *p;
	struct gfarm_str_list *s = spool_list;
	int err = -1, saved_errno = 0;

	if (s == NULL)
		gflog_fatal("gfarm_spool_root_foreach(): "
			    "programming error, "
			    "gfarm library isn't properly initialized");

	while (s) {
		sp = gfarm_str_list_car(s);
		p = gfarm_spool_path(sp, file);
		if (func(p, a) == 0)
			err = 0;
		else if (saved_errno == 0)
			saved_errno = errno;
		free(p);
		s = gfarm_str_list_cdr(s);
	}
	return (err == 0 ? err : -saved_errno);
}

#define GFARM_SPOOL_BLOCK_UNIT	1024

char *
gfarm_spool_root_get_for_write()
{
	struct statvfs fsb;
	char *spool, *sp;
	struct gfarm_str_list *s = spool_list;
	unsigned long avail, a;

	if (s == NULL)
		gflog_fatal("gfarm_spool_root_get_for_write(): "
			    "programming error, "
			    "gfarm library isn't properly initialized");

	avail = 0;
	spool = gfarm_spool_root_get_for_compatibility();
	while (s) {
		sp = gfarm_str_list_car(s);
		if (statvfs(sp, &fsb)) {
			gflog_warning_errno("%s", sp);
			s = gfarm_str_list_cdr(s);
			continue;
		}
		a = fsb.f_bsize / GFARM_SPOOL_BLOCK_UNIT * fsb.f_bavail;
		if (avail < a) {
			avail = a;
			spool = sp;
		}
		s = gfarm_str_list_cdr(s);
	}
	return (spool);
}

char *
gfarm_spool_root_get_for_read(char *file)
{
	struct statvfs fsb;
	struct stat stb;
	char *p, *spool, *sp;
	struct gfarm_str_list *s = spool_list;
	unsigned long avail, a;
	int r;

	if (s == NULL)
		gflog_fatal("gfarm_spool_root_get_for_read(): "
			    "programming error, "
			    "gfarm library isn't properly initialized");

	avail = 0;
	spool = gfarm_spool_root_get_for_compatibility();
	while (s) {
		sp = gfarm_str_list_car(s);
		p = gfarm_spool_path(sp, file);
		if (p != NULL) {
			r = stat(p, &stb);
			free(p);
			if (r == 0)
				return (sp);
		}
		if (statvfs(sp, &fsb)) {
			gflog_warning_errno("%s", sp);
			s = gfarm_str_list_cdr(s);
			continue;
		}
		a = fsb.f_bsize / GFARM_SPOOL_BLOCK_UNIT * fsb.f_bavail;
		if (avail < a) {
			avail = a;
			spool = sp;
		}
		s = gfarm_str_list_cdr(s);
	}
	return (spool);
}

void
gfarm_spool_root_check()
{
	struct gfarm_str_list *s = spool_list;
	char *sp;
	struct stat sb;

	while (s) {
		sp = gfarm_str_list_car(s);
		if (stat(sp, &sb) == -1)
			gflog_fatal_errno(sp);
		else if (!S_ISDIR(sb.st_mode))
			gflog_fatal("%s: %s", sp, GFARM_ERR_NOT_A_DIRECTORY);
		s = gfarm_str_list_cdr(s);
	}
}

char *
gfarm_spool_path_localize_for_write(char *canonic_path, char **abs_pathp)
{
	char *spool_root = gfarm_spool_root_get_for_write();

	*abs_pathp = gfarm_spool_path(spool_root, canonic_path);
	return (*abs_pathp == NULL ? GFARM_ERR_NO_MEMORY : NULL);
}

char *
gfarm_spool_path_localize(char *canonic_path, char **abs_pathp)
{
	char *spool_root = gfarm_spool_root_get_for_read(canonic_path);

	*abs_pathp = gfarm_spool_path(spool_root, canonic_path);
	return (*abs_pathp == NULL ? GFARM_ERR_NO_MEMORY : NULL);
}

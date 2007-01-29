#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "config.h"
#include "gfs_misc.h"

char *
gfs_stat_size_canonical_path(
	char *gfarm_file, file_offset_t *size, int *nsection)
{
	char *e, *e_save = NULL;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	file_offset_t s;

	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e != NULL)
		return (e);

	s = 0;
	for (i = 0; i < nsections; i++) {
		e = gfs_file_section_info_check_busy(&sections[i]);
		if (e_save == NULL)
			e_save = e;
		s += sections[i].filesize;
	}
	*size = s;
	*nsection = nsections;

	gfarm_file_section_info_free_all(nsections, sections);

	return (e_save);
}

char *
gfs_stat_canonical_path(char *gfarm_file, struct gfs_stat *s)
{
	struct gfarm_path_info pi;
	unsigned long ino;
	char *e;

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		return (e);

	e = gfs_get_ino(gfarm_file, &ino);
	if (e != NULL)
		return (e);

	*s = pi.status;
	s->st_ino = ino;
	s->st_user = strdup(s->st_user);
	s->st_group = strdup(s->st_group);
	gfarm_path_info_free(&pi);
	if (s->st_user == NULL || s->st_group == NULL) {
		gfs_stat_free(s);
		return (GFARM_ERR_NO_MEMORY);
	}

	if (!GFARM_S_ISREG(s->st_mode))
		return (NULL);

	/* regular file */
	e = gfs_stat_size_canonical_path(
		gfarm_file, &s->st_size, &s->st_nsections);
	/* allow stat() during text file busy */
	if (e == GFARM_ERR_TEXT_FILE_BUSY)
		e = NULL;
	if (e != NULL) {
		gfs_stat_free(s);
		/*
		 * If GFARM_ERR_NO_SUCH_OBJECT is returned here,
		 * gfs_stat() incorrectly assumes that this is a directory,
		 * and reports GFARM_ERR_NOT_A_DIRECTORY.
		 */
		/*
		 * Initialize st_user, st_group, st_size, and
		 * st_nsections to safely call gfs_stat_free()
		 * afterward since no fragment information case is a
		 * special.
		 */
		s->st_user = s->st_group = NULL;
		s->st_size = s->st_nsections = 0;

		return (GFARM_ERR_NO_FRAGMENT_INFORMATION);
	}
	return (e);
}

static double gfs_stat_time;

char *
gfs_stat(const char *path, struct gfs_stat *s)
{
	char *e, *p;
	gfarm_timerval_t t1, t2;
	unsigned long ino;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	path = gfarm_url_prefix_skip(path);
	e = gfarm_canonical_path(path, &p);
	if (e != NULL)
		goto finish;
	e = gfs_stat_canonical_path(p, s);
	if (e == NULL)
		goto finish_free_p;
	if (e != GFARM_ERR_NO_SUCH_OBJECT)
		goto finish_free_p;

	/*
	 * XXX - assume that it's a directory that does not have the
	 * path info.
	 */
	e = gfs_get_ino(p, &ino);
	if (e != NULL)
		goto finish_free_p;
	s->st_ino = ino;
	s->st_mode = GFARM_S_IFDIR | 0777;
	s->st_user = strdup("root");
	s->st_group = strdup("gfarm");
	s->st_atimespec.tv_sec = 0;
	s->st_atimespec.tv_nsec = 0;
	s->st_mtimespec.tv_sec = 0;
	s->st_mtimespec.tv_nsec = 0;
	s->st_ctimespec.tv_sec = 0;
	s->st_ctimespec.tv_nsec = 0;
	s->st_size = 0;
	s->st_nsections = 0;

	e = NULL;
 finish_free_p:
	free(p);
 finish:
	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_stat_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

void
gfs_stat_free(struct gfs_stat *s)
{
	if (s->st_user != NULL)
		free(s->st_user);
	if (s->st_group != NULL)
		free(s->st_group);
}

char *
gfs_stat_section(const char *gfarm_url, const char *section, struct gfs_stat *s)
{
	char *e, *gfarm_file;
	struct gfarm_file_section_info sinfo;
	struct gfarm_path_info pi;
	unsigned long ino;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfs_get_ino(gfarm_file, &ino);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}

	*s = pi.status;
	s->st_ino = ino;
	s->st_user = strdup(s->st_user);
	s->st_group = strdup(s->st_group);
	gfarm_path_info_free(&pi);

	if (!GFARM_S_ISREG(s->st_mode)) {
		free(gfarm_file);
		return (NULL);
	}

	e = gfarm_file_section_info_get(gfarm_file, section, &sinfo);
	free(gfarm_file);
	if (e != NULL)
		return (e);

	s->st_size = sinfo.filesize;
	s->st_nsections = 1;

	gfarm_file_section_info_free(&sinfo);

	return (NULL);
}

char *
gfs_stat_index(char *gfarm_url, int index, struct gfs_stat *s)
{
	char section[GFARM_INT32STRLEN + 1];

	sprintf(section, "%d", index);

	return (gfs_stat_section(gfarm_url, section, s));
}

void
gfs_stat_display_timers(void)
{
	gflog_info("gfs_stat        : %g sec\n", gfs_stat_time);
}

#include <stdlib.h>
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfs_misc.h"

char *
gfarm_url_fragment_number(const char *gfarm_url, int *np)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	if (!GFARM_S_IS_FRAGMENTED_FILE(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	*np = pi.status.st_nsections;
	gfarm_path_info_free(&pi);
	return (NULL);
}

/*  */

/* XXX - should provide parallel version. */
char *
gfarm_foreach_copy(char *(*op)(struct gfarm_file_section_copy_info *, void *),
	const char *gfarm_file, const char *section, void *arg, int *nsuccessp)
{
	char *e, *e_save = NULL;
	int j, ncopies, nsuccess = 0;
	struct gfarm_file_section_copy_info *copies;

	e = gfarm_file_section_copy_info_get_all_by_section(
		gfarm_file, section, &ncopies, &copies);
	if (e == NULL) {
		for (j = 0; j < ncopies; j++) {
			e = op(&copies[j], arg);
			if (e == NULL)
				++nsuccess;
			if (e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_copy_info_free_all(ncopies, copies);
	}
	if (nsuccessp != NULL)
		*nsuccessp = nsuccess;

	return (e_save != NULL ? e_save : e);
}

char *
gfarm_foreach_section(char *(*op)(struct gfarm_file_section_info *, void *),
	const char *gfarm_file, void *arg,
	char *(*undo_op)(struct gfarm_file_section_info *, void *))
{
	char *e, *e_save = NULL;
	int i, nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e == NULL) {
		for (i = 0; i < nsections; i++) {
			e = op(&sections[i], arg);
			if (e != NULL && undo_op) {
				for (; i >= 0; --i)
					undo_op(&sections[i], arg);
				break;
			}
			if (e_save == NULL)
				e_save = e;
		}
		gfarm_file_section_info_free_all(nsections, sections);
	}
	else if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = GFARM_ERR_NO_FRAGMENT_INFORMATION;
	return (e_save != NULL ? e_save : e);
}

/*
 * checksum handling
 */

#define SECTION_BUSY "SECTION BUSY"
#define CHECKSUM_UNKNOWN "UNKNOWN"

char *
gfs_file_section_info_check_checksum_unknown(
	struct gfarm_file_section_info *fi)
{
	if (strncmp(fi->checksum, CHECKSUM_UNKNOWN,
		    sizeof(CHECKSUM_UNKNOWN) - 1) == 0)
		return (GFARM_ERR_CHECKSUM_UNKNOWN);
	return (NULL);
}


char *
gfs_file_section_info_check_busy(struct gfarm_file_section_info *fi)
{
	if (strncmp(fi->checksum, SECTION_BUSY, sizeof(SECTION_BUSY) - 1) == 0)
		return (GFARM_ERR_TEXT_FILE_BUSY);
	return (NULL);
}

char *
gfs_file_section_check_busy(char *pathname, char *section)
{
	struct gfarm_file_section_info fi;
	char *e;

	e = gfarm_file_section_info_get(pathname, section, &fi);
	if (e != NULL)
		return (e);
	e = gfs_file_section_info_check_busy(&fi);
	gfarm_file_section_info_free(&fi);
	return (e);
}

static char *
gfs_file_section_set_status(char *pathname, char *section,
	file_offset_t filesize, char *status)
{
	struct gfarm_file_section_info fi;
	char *e;

	fi.pathname = pathname;
	fi.section = section;
	fi.filesize = filesize;
	fi.checksum_type = GFS_DEFAULT_DIGEST_NAME;
	fi.checksum = status;

	e = gfarm_file_section_info_replace(pathname, section, &fi);
	/* FT */
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = gfarm_file_section_info_set(pathname, section, &fi);
	return (e);
}

char *
gfs_file_section_set_checksum_unknown(char *pathname, char *section,
	file_offset_t filesize)
{
	return (gfs_file_section_set_status(pathname, section, filesize,
	    CHECKSUM_UNKNOWN));
}

char *
gfs_file_section_set_busy(char *pathname, char *section,
	file_offset_t filesize)
{
	return (gfs_file_section_set_status(pathname, section, filesize,
	    SECTION_BUSY));
}

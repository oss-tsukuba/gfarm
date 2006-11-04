/*
 * pio operations for global view
 *
 * $Id$
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "gfs_proto.h"	/* GFARM_FILE_CREATE */
#include "gfs_pio.h"
#include "gfs_misc.h"	/* gfs_unlink_section_internal() */

struct gfs_file_global_context {
	GFS_File fragment_gf;
	int fragment_index;
	char *url;

	file_offset_t *offsets;
};

static char *
gfs_pio_view_global_close(GFS_File gf)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e;

	e = gfs_pio_close_internal(gc->fragment_gf);
	free(gc->url);
	free(gc->offsets);
	free(gc);
	gf->view_context = NULL;
	gfs_pio_set_view_default(gf);
	return (e);
}

/*
 * Instead of just calling gfs_pio_set_view_index(),
 * we use another GFS_File to move to another fragment.
 * This is because we don't want to leave this context inconsistent,
 * if failure happens at the gfs_pio_set_view_index().
 */
static char *
gfs_pio_view_global_move_to(GFS_File gf, int fragment_index)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e;
	GFS_File new_fragment;

	if ((gf->open_flags & GFARM_FILE_CREATE) != 0)
		e = gfs_pio_create(gc->url, gf->open_flags,
		    gf->pi.status.st_mode & GFARM_S_ALLPERM,
		    &new_fragment);
	else
		e = gfs_pio_open(gc->url, gf->open_flags, &new_fragment);
	if (e != NULL)
		return (e);
	e = gfs_pio_set_view_index(new_fragment, gf->pi.status.st_nsections,
	    fragment_index, NULL, gf->view_flags);
	if (e != NULL) {
		gfs_pio_close_internal(new_fragment);
		return (e);
	}
	if (gc->fragment_gf != NULL) {
		gfs_pio_close_internal(gc->fragment_gf);
		/* XXX need a way to report error on here */
	}
	gc->fragment_gf = new_fragment;
	gc->fragment_index = fragment_index;
	return (NULL);
}

static char *
gfs_pio_view_global_adjust(GFS_File gf, const char *buffer, size_t *sizep)
{
	struct gfs_file_global_context *gc = gf->view_context;
	size_t size = *sizep;
	char *e = NULL;

	while (gc->fragment_index < gf->pi.status.st_nsections - 1 &&
	    gf->io_offset >= gc->offsets[gc->fragment_index + 1]) {
		e = gfs_pio_view_global_move_to(gf, gc->fragment_index + 1);
		if (e != NULL)
			return (e);
	}
	if (gc->fragment_index < gf->pi.status.st_nsections - 1 &&
	    gf->io_offset + size > gc->offsets[gc->fragment_index + 1])
		size = gc->offsets[gc->fragment_index + 1] - gf->io_offset;

	*sizep = size;
	return (e);
}

static char *
gfs_pio_view_global_write(GFS_File gf, const char *buffer, size_t size,
			  size_t *lengthp)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e = gfs_pio_view_global_adjust(gf, buffer, &size);
	int length; /* XXX - should be size_t */

	if (e != NULL)
		return (e);
	e = gfs_pio_write(gc->fragment_gf, buffer, size, &length);
	if (e != NULL)
		return (e);
	/* XXX - should notify this change to all of the parallel process. */
	if (gc->fragment_index == gf->pi.status.st_nsections - 1 &&
	    gf->io_offset + length > gc->offsets[gf->pi.status.st_nsections])
		gc->offsets[gf->pi.status.st_nsections] =
			gf->io_offset + length;
	*lengthp = length;
	return (NULL);
}

static char *
gfs_pio_view_global_read(GFS_File gf, char *buffer, size_t size,
			 size_t *lengthp)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e = gfs_pio_view_global_adjust(gf, buffer, &size);
	int length; /* XXX - should be size_t */

	if (e != NULL)
		return (e);
	e = gfs_pio_read(gc->fragment_gf, buffer, size, &length);
	if (e != NULL)
		return (e);
	*lengthp = length;
	return (NULL);
}

static int
gfs_pio_view_global_bsearch(file_offset_t key, file_offset_t *v, size_t n)
{
	size_t m, l = 0, r = n - 1;

	while (l < r) {
		m = (l + r) / 2;
		if (key < v[m])
			r = m - 1;
		else if (key < v[m + 1])
			return (m);
		else
			l = m + 1;
	}
	return (l);
}

static char *
gfs_pio_view_global_seek(GFS_File gf, file_offset_t offset, int whence,
			  file_offset_t *resultp)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e;
	int fragment;

	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += gf->io_offset + gf->p;
		break;
	case SEEK_END:
		offset += gc->offsets[gf->pi.status.st_nsections];
		break;
	}

	if (gc->offsets[gc->fragment_index] <= offset &&
	    offset <= gc->offsets[gc->fragment_index + 1]) {
		/* same file fragment */
		if (offset == gf->io_offset)
			return (NULL);
	} else {
		if (offset < 0)
			return (GFARM_ERR_INVALID_ARGUMENT);
		if (offset >= gc->offsets[gf->pi.status.st_nsections - 1])
			fragment  = gf->pi.status.st_nsections - 1;
		else
			fragment = gfs_pio_view_global_bsearch(
			    offset, gc->offsets, gf->pi.status.st_nsections-1);

		e = gfs_pio_view_global_move_to(gf, fragment);
		if (e != NULL)
			return (e);
	}
	offset -= gc->offsets[gc->fragment_index];
	e = gfs_pio_seek(gc->fragment_gf, offset, SEEK_SET, &offset);
	if (e != NULL)
		return (e);
	if (resultp != NULL)
		*resultp = gc->offsets[gc->fragment_index] + offset;
	return (NULL);
}

static char *
gfs_pio_view_global_ftruncate(GFS_File gf, file_offset_t length)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e;
	int i, fragment, nsections;
	file_offset_t section_length;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info *sections;
	char section_string[GFARM_INT32STRLEN + 1];

	if (length < 0)
		return (GFARM_ERR_INVALID_ARGUMENT);
	if (length >= gc->offsets[gf->pi.status.st_nsections - 1])
		fragment = gf->pi.status.st_nsections - 1;
	else
		fragment = gfs_pio_view_global_bsearch(
		    length, gc->offsets, gf->pi.status.st_nsections - 1);

	section_length = length - gc->offsets[fragment];

	e = gfs_pio_view_global_move_to(gf, fragment);
	if (e != NULL)
		return (e);
	e = gfs_pio_truncate(gc->fragment_gf, section_length);
	if (e != NULL)
		return (e);

	/*
	 * Before updating path_info, try to update most recent information,
	 * because the file mode may be updated by e.g. gfs_chmod().
	 */
	if (gfarm_path_info_get(gf->pi.pathname, &pi) == NULL) {
		gfarm_path_info_free(&gf->pi);
		gf->pi = pi;
	}

#if 0 /* We don't store file size in gfarm_path_info, this is just ignored */
	gf->pi.status.st_size = length;
#endif
	gf->pi.status.st_nsections = fragment + 1;
	e = gfarm_path_info_replace(gf->pi.pathname, &gf->pi);
	if (e != NULL)
		return (e);
	
	e = gfarm_file_section_info_get_sorted_all_serial_by_file(
		gf->pi.pathname, &nsections, &sections);
	if (e != NULL)
		return (e);
	sections[fragment].filesize = section_length;
	sprintf(section_string, "%d", fragment);
	e = gfarm_file_section_info_replace(gf->pi.pathname, section_string,
					    &sections[fragment]);
	for (i = fragment + 1; i < nsections; i++)
		(void)gfs_unlink_section_internal(gf->pi.pathname,
						  sections[i].section);
	gfarm_file_section_info_free_all(nsections, sections);
	
	return (e);
}

static char *
gfs_pio_view_global_fsync(GFS_File gf, int operation)
{
	struct gfs_file_global_context *gc = gf->view_context;
	char *e = NULL;
	int i;

	for (i = 0; i < gf->pi.status.st_nsections; i++) {
		e = gfs_pio_view_global_move_to(gf, i);
		if (e != NULL)
			return (e);

		switch (operation) {
		case GFS_PROTO_FSYNC_WITHOUT_METADATA:
			e = gfs_pio_datasync(gc->fragment_gf);
			break;
		case GFS_PROTO_FSYNC_WITH_METADATA:
			e = gfs_pio_sync(gc->fragment_gf);
			break;
		default:	
			e = GFARM_ERR_INVALID_ARGUMENT;
			break;
		}	
		if (e != NULL)
			return (e);
	}
	return (e);
}

static int
gfs_pio_view_global_fd(GFS_File gf)
{
	struct gfs_file_global_context *gc = gf->view_context;

	return (gfs_pio_fileno(gc->fragment_gf));
}

static char *
gfs_pio_view_global_stat(GFS_File gf, struct gfs_stat *status)
{
	struct gfs_file_global_context *gc = gf->view_context;

	return (gfs_stat(gc->url, status));
}

static char *
gfs_pio_view_global_chmod(GFS_File gf, gfarm_mode_t mode)
{
	return (gfs_chmod_internal(&gf->pi, mode, NULL));
}

struct gfs_pio_ops gfs_pio_view_global_ops = {
	gfs_pio_view_global_close,
	gfs_pio_view_global_write,
	gfs_pio_view_global_read,
	gfs_pio_view_global_seek,
	gfs_pio_view_global_ftruncate,
	gfs_pio_view_global_fsync,
	gfs_pio_view_global_fd,
	gfs_pio_view_global_stat,
	gfs_pio_view_global_chmod
};

char *
gfs_pio_set_view_global(GFS_File gf, int flags)
{
	struct gfs_file_global_context *gc;
	char *e, *arch;
	int i, n;
	struct gfarm_file_section_info *infos;
	static char gfarm_url_prefix[] = "gfarm:/";

	e = gfs_pio_set_view_default(gf);
	if (e != NULL)
		return (e);

	if (GFS_FILE_IS_PROGRAM(gf)) {
		e = gfarm_host_get_self_architecture(&arch);
		if (e != NULL)
			return (gf->error = e);
		e = gfs_pio_set_view_section(gf, arch, NULL, flags);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = gfs_pio_set_view_section(
				gf, "noarch", NULL, flags);
		return (e);
	}

	if ((gf->mode & GFS_FILE_MODE_FILE_CREATED) != 0)
		return (gfs_pio_set_view_index(gf, 1, 0, NULL, flags));

	if (gf->open_flags & GFARM_FILE_TRUNC) {
		int nsections;
		struct gfarm_file_section_info *sections;

		/* XXX this may not be OK, if a parallel process does this */
		/* remove all sections except section "0" */
		e = gfarm_file_section_info_get_all_by_file(gf->pi.pathname,
		    &nsections, &sections);
		if (e != NULL)
			return (e);
		for (i = 0; i < nsections; i++) {
			if (strcmp(sections[i].section, "0") == 0)
				continue;
			(void)gfs_unlink_section_internal(gf->pi.pathname,
			    sections[i].section);
		}
		gfarm_file_section_info_free_all(nsections, sections);

		gf->pi.status.st_nsections = 1;
		return (gfs_pio_set_view_index(gf, 1, 0, NULL, flags));
	}

	/* XXX - GFARM_FILE_APPEND is not supported */
	if (gf->open_flags & GFARM_FILE_APPEND) {
		gf->error = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		return (gf->error);
	}

	GFARM_MALLOC(gc);
	if (gc == NULL) {
		gf->error = GFARM_ERR_NO_MEMORY;
		return (gf->error);
	}

	e = gfarm_file_section_info_get_sorted_all_serial_by_file(
		gf->pi.pathname, &n, &infos);
	if (e != NULL) {
		free(gc);
		gf->error = e;
		return (e);
	}

	if (n != gf->pi.status.st_nsections) {
		gfarm_file_section_info_free_all(n, infos);
		free(gc);
		gf->error = "metainfo inconsitency, fragment number mismatch";
		return (gf->error);
	}

	GFARM_MALLOC_ARRAY(gc->offsets, n + 1);
	GFARM_MALLOC_ARRAY(gc->url,
		sizeof(gfarm_url_prefix) + strlen(gf->pi.pathname));
	if (gc->offsets == NULL || gc->url == NULL) {
		if (gc->offsets != NULL)
			free(gc->offsets);
		if (gc->url != NULL)
			free(gc->url);
		gfarm_file_section_info_free_all(n, infos);
		free(gc);
		gf->error = GFARM_ERR_NO_MEMORY;
		return (gf->error);
	}

	gc->offsets[0] = 0;
	for (i = 0; i < n; i++)
		gc->offsets[i + 1] = gc->offsets[i] + infos[i].filesize;
	gfarm_file_section_info_free_all(n, infos);

	sprintf(gc->url, "%s%s", gfarm_url_prefix, gf->pi.pathname);

	gf->view_context = gc;
	gf->view_flags = flags;
	gc->fragment_gf = NULL;
	e = gfs_pio_view_global_move_to(gf, 0);
	if (e != NULL) {
		free(gc->url);
		free(gc->offsets);
		free(gc);
		gf->view_context = NULL;
		gfs_pio_set_view_default(gf);
		gf->error = e;
		return (e);
	}

	gf->ops = &gfs_pio_view_global_ops;
	gf->p = gf->length = 0;
	gf->io_offset = gf->offset = 0;
	gf->error = NULL;
	return (NULL);
}

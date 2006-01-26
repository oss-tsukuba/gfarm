/*
 * $Id$
 */

#include <sys/socket.h> /* struct sockaddr */ 
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfs_proto.h"
#include "gfs_client.h"
#include "gfs_pio.h"

static char *
change_path_info_mode(struct gfarm_path_info *p, gfarm_mode_t mode)
{
	p->status.st_mode &= ~GFARM_S_ALLPERM;
	p->status.st_mode |= (mode & GFARM_S_ALLPERM);
	return (gfarm_path_info_replace(p->pathname, p));
}

enum exec_change {
	EXECUTABILITY_NOT_CHANGED,
	TO_EXECUTABLE,
	TO_NONEXECUTABLE
};

static char *
change_path_info_mode_nsections(
	struct gfarm_path_info *p,
	gfarm_mode_t mode,
	enum exec_change change)
{
	switch (change) {
	case TO_EXECUTABLE:
		p->status.st_nsections = 0;
		break;
	case TO_NONEXECUTABLE:
		p->status.st_nsections = 1;
		break;
	default:
		break;
	}
	return (change_path_info_mode(p, mode));
}

static enum exec_change
diff_exec(gfarm_mode_t old, gfarm_mode_t new)
{
	gfarm_mode_t xmask = 0111;

	if ((old & xmask) == 0 && (new & xmask) != 0)
		return (TO_EXECUTABLE);
	else if ((old & xmask) != 0 && (new & xmask) == 0)
		return (TO_NONEXECUTABLE);
	else
		return (EXECUTABILITY_NOT_CHANGED);
}

static char *
get_architecture_name(int ncopy, struct gfarm_file_section_copy_info *copies)
{
	char *architecture;

	/* if architecture of this node is known, use it */
	if (gfarm_host_get_self_architecture(&architecture) == NULL)
		return (strdup(architecture));

	/* XXX stopgap. use architecture of a node which has a replica. */
	return (gfarm_host_info_get_architecture_by_host(copies[0].hostname));
}

struct gfs_chmod_args {
	char *path;
	gfarm_mode_t mode;
};

static char *
chmod_dir_request_parallel(struct gfs_connection *gfs_server, void *args)
{
	struct gfs_chmod_args *a = args;
	char *e;

	/*
	 *  XXX - Not to issue a error message "no such object" for directory.
	 *        A directory in spool is created on demand, so generally it
	 *        does not exist in spool though its meta data exist.
	 */
	e = gfs_client_chmod(gfs_server, a->path, a->mode);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		e = NULL;
	return (e);
}

static char *
get_new_section_name(enum exec_change change,
	int ncopy,
	struct gfarm_file_section_copy_info *copies)
{
	if (change == TO_EXECUTABLE)
		return get_architecture_name(ncopy, copies);
	else /* change == TO_NONEXECUTABLE */
		return strdup("0");
}

static char *
gfs_chmod_execfile_metadata(
	struct gfarm_path_info *pi,
	gfarm_mode_t mode,
	struct gfarm_file_section_info *from_section,
	int ncopy,
	struct gfarm_file_section_copy_info *copies,
	enum exec_change change,
	char **changed_sectionp)
{
	struct gfarm_file_section_info to_section;
	char *e;
	int i;

	*changed_sectionp = NULL;
	to_section = *from_section;
	to_section.section = get_new_section_name(change, ncopy, copies);
	if (to_section.section == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_file_section_info_set(
		to_section.pathname, to_section.section, &to_section);
	if (e != NULL)
		goto finish;

	e = change_path_info_mode_nsections(pi, mode, change);
	if (e != NULL) {
		char *e2;

		e2 = gfarm_file_section_info_remove(
				to_section.pathname, to_section.section);
		gflog_warning(
			"gfs_chmod: file_section_info_remove: %s (%s): %s",
			to_section.pathname, to_section.section, e2);
		goto finish;
	}

	for (i = 0; i < ncopy; i++) {
		e = gfarm_file_section_copy_info_remove(copies[i].pathname,
			copies[i].section, copies[i].hostname);
		if (e != NULL)
			gflog_warning(
				"gfs_chmod: file_section_copy_info_remove: "
				"%s (%s) on %s: %s",
				copies[i].pathname, copies[i].section,
				copies[i].hostname, e);
	}

	e = gfarm_file_section_info_remove(from_section->pathname,
					   from_section->section);
	if (e != NULL)
		gflog_warning(
			"gfs_chmod: file_section_info_remove: %s (%s): %s",
			from_section->pathname, from_section->section, e);

 finish:
	if (e == NULL)
		*changed_sectionp = to_section.section;
	else
		free(to_section.section);
	return(e);
}

static char *
gfs_chmod_file_spool(
	gfarm_mode_t mode,
	int nsection,
	struct gfarm_file_section_info *sections,
	int *ncopy,
	struct gfarm_file_section_copy_info **copies,
	enum exec_change change)
{
	int i, j;
	char *e;
	char *from_path_section, *to_path_section;
	struct gfarm_file_section_copy_info new_copy;

	for (i = 0; i < nsection; i++) {
		char *section;

		e = gfarm_path_section(sections[i].pathname,
				       sections[i].section,
				       &from_path_section);
		if (e != NULL)
			return (e);

		section = get_new_section_name(
					change, ncopy[i], copies[i]);
		if (section == NULL) {
			free(from_path_section);
			return (GFARM_ERR_NO_MEMORY);
		}

		e = gfarm_path_section(sections[i].pathname, section,
				       &to_path_section);
		if (e != NULL) {
			free(section);
			free(from_path_section);
			return (e);
		}

		for (j = 0; j < ncopy[i]; j++) {
			struct gfs_connection *gfs_server;
			struct sockaddr peer_addr;

			e = gfarm_host_address_get(copies[i][j].hostname,
				gfarm_spool_server_port, &peer_addr, NULL);
			if (e != NULL) {
				gflog_warning(
					"gfs_chmod: host_address_get: %s: %s",
					copies[i][j].hostname, e);
				continue;
			}

			e = gfs_client_connect(copies[i][j].hostname,
					       &peer_addr, &gfs_server);
			if (e != NULL) {
				gflog_warning(
					"gfs_chmod: gfs_client_connect: %s: %s",
					copies[i][j].hostname, e);
				continue;
			}

			e = gfs_client_chmod(gfs_server, from_path_section,
					     mode);
			if (e != NULL) {
				gflog_warning(
				   "gfs_chmod: gfs_client_chmod: %s on %s: %s",
				   from_path_section, copies[i][j].hostname, e);
				gfs_client_disconnect(gfs_server);
				continue;
			}
			if (change != EXECUTABILITY_NOT_CHANGED) {
				e = gfs_client_rename(gfs_server,
					from_path_section, to_path_section);
				if (e != NULL) {
					gflog_warning(
					   "gfs_chmod: gfs_client_rename"
					   "(%s, %s): %s", from_path_section,
					   to_path_section, e);
					gfs_client_disconnect(gfs_server);
					continue;
				}

				new_copy = copies[i][j];
				new_copy.section = section;
				e = gfarm_file_section_copy_info_set(
					new_copy.pathname, new_copy.section,
					new_copy.hostname, &new_copy);
				if (e != NULL) {
					gflog_warning(
					  "gfs_chmod: "
					  "file_section_copy_info_set: "
					  "%s (%s) on %s: %s",
					  new_copy.pathname, new_copy.section,
					  new_copy.hostname, e);
				}
			}
			gfs_client_disconnect(gfs_server);
		}
		free(to_path_section);
		free(section);
		free(from_path_section);
	}
	return (NULL);
}

/*
 * NOTE: `*changed_sectionp' is always set,
 *	and may be NULL in the following cases:
 * 1. An error occurs.
 * 2. No error occurs, but the file's executability was not changed.
 *    i.e. all execute-bits are unset in both old and new mode,
 *    or some execute-bits are set in both modes.
 */
char *
gfs_chmod_meta_spool(struct gfarm_path_info *pi, gfarm_mode_t mode,
		     char **changed_sectionp)
{
	char *e;
	int nsection, *ncopy, i;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info **copies;
	enum exec_change change;

	*changed_sectionp = NULL;
	if (strcmp(pi->status.st_user, gfarm_get_global_username()) != 0) {
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}

	if (GFARM_S_ISDIR(pi->status.st_mode)) {
		struct gfs_chmod_args a;
		int nhosts_succeed;

		e = change_path_info_mode(pi, mode);
		if (e != NULL)
			return (e);
		a.path = pi->pathname;
		a.mode = mode;
		e = gfs_client_apply_all_hosts(chmod_dir_request_parallel,
					 &a, "gfs_chmod", 1, &nhosts_succeed);
		return (e);
	}

	e = gfarm_file_section_info_get_all_by_file(pi->pathname,
						    &nsection, &sections);
	if (e != NULL)
		return (e);

	change = diff_exec(pi->status.st_mode, mode);
	if (change != EXECUTABILITY_NOT_CHANGED && nsection > 1) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto finish_free_section_info;
	}

	ncopy = malloc(nsection * sizeof(*ncopy));
	if (ncopy == NULL)
		goto finish_free_section_info;

	copies = malloc(nsection * sizeof(*copies));
	if (copies == NULL)
		goto finish_free_ncopy;

	for (i = 0; i < nsection; i++) {
		e = gfarm_file_section_copy_info_get_all_by_section(
			sections[i].pathname, sections[i].section,
			&ncopy[i], &copies[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free_all(
							ncopy[i], copies[i]);
			goto finish_free_copies;
		}
	}

	if (change == EXECUTABILITY_NOT_CHANGED) {
		e = change_path_info_mode(pi, mode);
	} else {
		e = gfs_chmod_execfile_metadata(pi, mode, &sections[0],
						ncopy[0], copies[0], change,
						changed_sectionp);
	}
	if (e != NULL)
		goto finish_free_section_copy_info;

	e = gfs_chmod_file_spool(mode, nsection, sections, ncopy, copies,
				 change);

finish_free_section_copy_info:
	for (i = 0; i < nsection; i++)
		gfarm_file_section_copy_info_free_all(ncopy[i], copies[i]);
finish_free_copies:
	free(copies);
finish_free_ncopy:
	free(ncopy);
finish_free_section_info:
	gfarm_file_section_info_free_all(nsection, sections);
	return (e);
}

char *
gfs_chmod(const char *gfarm_url, gfarm_mode_t mode)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	char *changed_section;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);

	e = gfs_chmod_meta_spool(&pi, mode, &changed_section);
	if (changed_section != NULL)
		free(changed_section);
	gfarm_path_info_free(&pi);
	return (e);
}

char *
gfs_fchmod(GFS_File gf, gfarm_mode_t mode)
{
	return (*gf->ops->view_chmod)(gf, mode);
}

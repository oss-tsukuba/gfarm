/*
 * $Id$
 */

#include <sys/socket.h> /* struct sockaddr */ 
#include <string.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_client.h"
#include "gfs_pio.h"
#include "gfs_misc.h"

static void
change_path_info_mode(struct gfarm_path_info *pi, gfarm_mode_t mode)
{
	pi->status.st_mode &= ~GFARM_S_ALLPERM;
	pi->status.st_mode |= (mode & GFARM_S_ALLPERM);
}

enum exec_change {
	EXECUTABILITY_NOT_CHANGED,
	TO_EXECUTABLE,
	TO_NONEXECUTABLE
};

static void
change_path_info_nsections(
	struct gfarm_path_info *p,
	gfarm_mode_t mode)

{
	gfarm_mode_t xmask = 0111;

	if ((mode & xmask) != 0)
		p->status.st_nsections = 0;
	else
		p->status.st_nsections = 1;
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
get_new_section_name_by_mode(gfarm_mode_t mode,
	int ncopy,
	struct gfarm_file_section_copy_info *copies)
{
	gfarm_mode_t xmask = 0111;

	if ((mode & xmask) != 0)
		return get_architecture_name(ncopy, copies);
	else /* change == TO_NONEXECUTABLE */
		return strdup("0");
}

/*
 * Revert spool file's permissions.
 * If their executabiliy changes, unlinks new links and remove copy infos.
 */
static char *
revert_spool_client(
	struct gfarm_file_section_copy_info *copy,
	gfarm_mode_t old_mode,
	gfarm_mode_t new_mode,
	char *from_path_section,
	char *to_path_section,
	char *section)
{
	char *e;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	e = gfarm_host_address_get(
		copy->hostname, gfarm_spool_server_port, &peer_addr, NULL);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: host_address_get: %s: %s",
			copy->hostname, e);
		return (e);
	}

	e = gfs_client_connect(copy->hostname, &peer_addr, &gfs_server);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: gfs_client_connect: %s: %s",
			copy->hostname, e);
		return (e);
	}

	e = gfs_client_chmod(gfs_server, from_path_section, old_mode);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: gfs_client_chmod: %s on %s: %s",
			from_path_section, copy->hostname, e);
		gfs_client_disconnect(gfs_server);
		return (e);
	}

	if (diff_exec(old_mode, new_mode) != EXECUTABILITY_NOT_CHANGED) {
		e = gfs_client_unlink(gfs_server, to_path_section);
		if (e != NULL) {
			gflog_warning(
				"gfs_chmod: gfs_client_unlink"
				"(%s): %s", to_path_section, e);
			gfs_client_disconnect(gfs_server);
			return (e);
		}

		e = gfarm_file_section_copy_info_remove(
			copy->pathname, section, copy->hostname);
		if (e != NULL) {
			gflog_warning(
				"gfs_chmod: "
				"file_section_copy_info_remove: "
				"%s (%s) on %s: %s",
				copy->pathname, section, copy->hostname, e);
		}
	}
	gfs_client_disconnect(gfs_server);
	return (e);
}

static char *
revert_spool(
	gfarm_mode_t old_mode,
	gfarm_mode_t new_mode,
	int nsection, struct gfarm_file_section_info *sections,
	int *ncopy, struct gfarm_file_section_copy_info **copies)
{
	int i, j;
	char *e;
	char *from_path_section, *to_path_section;

	for (i = 0; i < nsection; i++) {
		char *section;

		e = gfarm_path_section(sections[i].pathname,
				       sections[i].section,
				       &from_path_section);
		if (e != NULL)
			return (e);

		section = get_new_section_name_by_mode(
			new_mode, ncopy[i], copies[i]);
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
			char *e2;

			e2 = revert_spool_client(
				&copies[i][j], old_mode, new_mode,
				from_path_section, to_path_section,
				section);
			if (e == NULL && e2 != NULL)
				e = e2;
		}
		free(to_path_section);
		free(section);
		free(from_path_section);
	}
	return (NULL);
}

/* 
 * change spool file's permission.
 * If their executabiliy changes, links spool files by new section name 
 * and set set copy infos. 
 */	
static char *
change_spool_client(
	struct gfarm_file_section_copy_info *copy,
	gfarm_mode_t old_mode,
	gfarm_mode_t new_mode,
	char *from_path_section,
	char *to_path_section,
	char *section)
{
	char *e;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;
	struct gfarm_file_section_copy_info new_copy;

	e = gfarm_host_address_get(
		copy->hostname, gfarm_spool_server_port, &peer_addr, NULL);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: host_address_get: %s: %s",
			copy->hostname, e);
		return (e);
	}

	e = gfs_client_connect(copy->hostname, &peer_addr, &gfs_server);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: gfs_client_connect: %s: %s",
			copy->hostname, e);
		return (e);
	}

	e = gfs_client_chmod(gfs_server, from_path_section, new_mode);
	if (e != NULL) {
		gflog_warning(
			"gfs_chmod: gfs_client_chmod: %s on %s: %s",
			from_path_section, copy->hostname, e);
		gfs_client_disconnect(gfs_server);
		return (e);
	}

	if (diff_exec(old_mode, new_mode) != EXECUTABILITY_NOT_CHANGED) {
		e = gfs_client_link(gfs_server,
				    from_path_section, to_path_section);
		if (e != NULL) {
			gflog_warning(
				"gfs_chmod: gfs_client_link"
				"(%s, %s): %s", from_path_section,
				to_path_section, e);
			gfs_client_disconnect(gfs_server);
			return (e);
		}

		new_copy = *copy;
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
	return (e);
}

static char *
change_spool(
	gfarm_mode_t old_mode,
	gfarm_mode_t new_mode,
	int nsection, struct gfarm_file_section_info *sections,
	int *ncopy, struct gfarm_file_section_copy_info **copies,
	gfarm_stringlist *failed_sections,
	gfarm_stringlist *failed_hosts)
{
	int i, j;
	char *e;
	char *from_path_section, *to_path_section;

	/*
	 * 1 - succeed in linking at least 1 spool file for all sections
	 * 0 - otherwise
	 */
	int ok;

	e = NULL;
	ok = 1;
	
	for (i = 0; i < nsection; i++) {
		char *section;
		if (ncopy[i] == 0)
			continue;

		e = gfarm_path_section(sections[i].pathname,
				       sections[i].section,
				       &from_path_section);
		if (e != NULL)
			return (e); /* XXX this leaves inconsistent state */

		section = get_new_section_name_by_mode(
					new_mode, ncopy[i], copies[i]);
		if (section == NULL) {
			free(from_path_section);
			/* XXX this leaves inconsistent state */
			return (GFARM_ERR_NO_MEMORY);
		}

		e = gfarm_path_section(sections[i].pathname, section,
				       &to_path_section);
		if (e != NULL) {
			free(section);
			free(from_path_section);
			return (e); /* XXX this leaves inconsistent state */
		}
		
		ok = 0;
		for (j = 0; j < ncopy[i]; j++) {
			e = change_spool_client(
				&copies[i][j], old_mode, new_mode,
				from_path_section, to_path_section,
				section);
			if (e == NULL) {
				ok = 1;
			} else {	
				char *e2;

				e2 = gfarm_stringlist_add(
					failed_sections, copies[i][j].section);
				e2 = gfarm_stringlist_add(
					failed_hosts, copies[i][j].hostname);
			}
		}
		free(to_path_section);
		free(section);
		free(from_path_section);
		if (!ok)
			break;
	}

	if (ok) {
		e = NULL;
	} else {
		/* ignore error code here */
		revert_spool(old_mode, new_mode,
			     nsection, sections, ncopy, copies);
	} 
	return (e);
}

/*
 * change meta data other than copy infos.
 */ 
static char *
update_path_section(
	gfarm_mode_t old_mode, 
	gfarm_mode_t new_mode,
	struct gfarm_path_info *pi,
	int nsection, struct gfarm_file_section_info *sections,
	struct gfarm_file_section_copy_info **copies,
	char **changed_sectionp)
{
	char *e;
	struct gfarm_file_section_info new_si;

	*changed_sectionp = NULL;

	change_path_info_mode(pi, new_mode);
	change_path_info_nsections(pi, new_mode);
	e = gfarm_path_info_replace(pi->pathname, pi);
	if (e != NULL)
		return (e);
	if (diff_exec(old_mode, new_mode) == EXECUTABILITY_NOT_CHANGED)
		return (NULL);
	new_si = sections[0];
	new_si.section = get_new_section_name_by_mode(new_mode, 0, copies[0]);
	if (new_si.section == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto revert_path_info;
		/* XXX - revert path info */
	}
	e = gfarm_file_section_info_set(
		new_si.pathname, new_si.section, &new_si);
	if (e != NULL) {
		goto revert_path_info;
		/* XXX - revert path info */
	}
	*changed_sectionp = new_si.section;

 revert_path_info:
	return (e);
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
gfs_chmod_meta_spool(struct gfarm_path_info *pi, gfarm_mode_t new_mode,
		     char **changed_sectionp)
{
	char *e;
	gfarm_mode_t old_mode;
	int nsection, *ncopy, i;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info **copies;
	enum exec_change change;
	gfarm_stringlist failed_sections, failed_hosts;

	*changed_sectionp = NULL;
	if (strcmp(pi->status.st_user, gfarm_get_global_username()) != 0) {
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
        old_mode = pi->status.st_mode;

	if (GFARM_S_ISDIR(old_mode)) {
		struct gfs_chmod_args a;
		int nhosts_succeed;

		change_path_info_mode(pi, new_mode);
		e = gfarm_path_info_replace(pi->pathname, pi);
		if (e != NULL)
			return (e);
		a.path = pi->pathname;
		a.mode = new_mode;
		e = gfs_client_apply_all_hosts(chmod_dir_request_parallel,
					 &a, "gfs_chmod", 1, &nhosts_succeed);
		return (e);
	}

	e = gfarm_file_section_info_get_all_by_file(pi->pathname,
						    &nsection, &sections);
	if (e != NULL)
		return (e);

	change = diff_exec(old_mode, new_mode);
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
#if 0
	fprintf(stderr, "kill any host's gfsd and hit any key:");
	getchar();
#endif
	e = gfarm_stringlist_init(&failed_sections);
	if (e != NULL)
		goto finish_free_section_copy_info;

	e = gfarm_stringlist_init(&failed_hosts);
	if (e != NULL)
		goto finish_free_failed_sections;

	e = change_spool(old_mode, new_mode,
			 nsection, sections, ncopy, copies,
			 &failed_sections, &failed_hosts);
	if (e != NULL)
		goto finish_free_failed_hosts;

	e = update_path_section(old_mode, new_mode, pi, nsection, sections,
				copies, changed_sectionp);
	if (e != NULL) {
		revert_spool(old_mode, new_mode,
			     nsection, sections, ncopy, copies);
		goto finish_free_failed_hosts;
	}	

	if (change != EXECUTABILITY_NOT_CHANGED) {
		char *e2;

		e2 = gfs_clean_spool(pi->pathname, nsection, sections,
				       ncopy, copies);
		e2 = gfarm_file_section_copy_info_remove_all_by_section(
			pi->pathname, sections[0].section);
		e2 = gfarm_file_section_info_remove(pi->pathname,
						    sections[0].section);
	}	
#if 0 
	fprintf(stderr, "rerun gfsd and hit any key:");
	getchar();
#endif
	for (i = 0; i < gfarm_stringlist_length(&failed_sections); i++) {
		char *e2, *path_section, *section, *hostname;
		struct gfs_connection *gfs_server;
		struct sockaddr peer_addr;

		section = gfarm_stringlist_elem(&failed_sections, i);
		hostname = gfarm_stringlist_elem(&failed_hosts, i); 

		e2 = gfarm_file_section_copy_info_remove(
			pi->pathname, section, hostname);

		e2 = gfarm_path_section(pi->pathname, section, &path_section);
		if (e2 != NULL)
			continue;

		e2 = gfarm_host_address_get(
			hostname, gfarm_spool_server_port, &peer_addr, NULL);
		if (e2 != NULL) {
			free(path_section);
			continue;
		}	

		e2 = gfs_client_connect(hostname, &peer_addr, &gfs_server);
		if (e2 != NULL) {
			free(path_section);
			continue;
		}	

		e2 = gfs_client_unlink(gfs_server, path_section);
		gfs_client_disconnect(gfs_server);
		free(path_section);
	}

finish_free_failed_hosts:
	gfarm_stringlist_free(&failed_hosts);
finish_free_failed_sections:
	gfarm_stringlist_free(&failed_sections);
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

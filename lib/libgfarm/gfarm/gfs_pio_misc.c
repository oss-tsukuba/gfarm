/*
 * $Id$
 */

#include <sys/types.h>
#include <sys/socket.h> /* struct sockaddr */
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h> /* for gfs_utime() */
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "host.h"
#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_client.h"
#include "gfs_pio.h"
#include "schedule.h"
#include "timer.h"
#include "gfutil.h"

char *
gfs_stat_canonical_path(char *gfarm_file, struct gfs_stat *s)
{
	char *e;
	int i, nsections;
	struct gfarm_file_section_info *sections;
	struct gfarm_path_info pi;
	long ino;

	e = gfs_get_ino(gfarm_file, &ino);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
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
	e = gfarm_file_section_info_get_all_by_file(gfarm_file,
	    &nsections, &sections);
	if (e != NULL) {
		gfs_stat_free(s);
		/*
		 * If GFARM_ERR_NO_SUCH_OBJECT is returned here,
		 * gfs_stat() incorrectly assumes that this is a directory,
		 * and reports GFARM_ERR_NOT_A_DIRECTORY.
		 */
		return (GFARM_ERR_NO_FRAGMENT_INFORMATION);
	}

	s->st_size = 0;
	for (i = 0; i < nsections; i++)
		s->st_size += sections[i].filesize;
	s->st_nsections = nsections;

	gfarm_file_section_info_free_all(nsections, sections);

	return (NULL);
}

double gfs_stat_time;

char *
gfs_stat(const char *path, struct gfs_stat *s)
{
	char *e, *p;
	gfarm_timerval_t t1, t2;
	long ino;

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
	long ino;

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

char *
gfs_fstat(GFS_File gf, struct gfs_stat *status)
{
	return ((*gf->ops->view_stat)(gf, status));
}

char *
gfs_access(const char *gfarm_url, int mode)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	gfarm_mode_t stat_mode;
	int stat_nsections, nsections;
	struct gfarm_file_section_info *sections;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	stat_mode = pi.status.st_mode;
	stat_nsections = pi.status.st_nsections;
	e = gfarm_path_info_access(&pi, mode);
	gfarm_path_info_free(&pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	if (GFARM_S_ISDIR(stat_mode)) {
		free(gfarm_file);
		return (NULL);
	}
	/*
	 * Check all fragments are ready or not.
	 * XXX - is this check necessary?
	 */
	e = gfarm_file_section_info_get_all_by_file(
		gfarm_file, &nsections, &sections);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	gfarm_file_section_info_free_all(nsections, sections);
	if (!GFARM_S_IS_PROGRAM(stat_mode)
	    && nsections != stat_nsections)
		e = GFARM_ERR_FRAGMENT_NUMBER_DOES_NOT_MATCH;
	return (e);
}

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
	TO_UNEXECUTABLE
};

static enum exec_change
diff_exec(gfarm_mode_t old, gfarm_mode_t new)
{
	gfarm_mode_t xmask = 0111;

	if ((old & xmask) == 0 && (new & xmask) != 0)
		return (TO_EXECUTABLE);
	else if ((old & xmask) != 0 && (new & xmask) == 0)
		return (TO_UNEXECUTABLE);
	else
		return (EXECUTABILITY_NOT_CHANGED);
}

static char *
get_architecture_name(int ncopy, struct gfarm_file_section_copy_info *copies)
{
	/* XXX - this selection of architecture is stopgap. */

	char *e, *hostname;
	int i;

	e = gfarm_host_get_canonical_self_name(&hostname);
	if (e != NULL)
		hostname = copies[0].hostname;
	for (i = 0; i < ncopy; i++)
		if (strcmp(copies[i].hostname, hostname) == 0) 
			break;
	if (i >= ncopy)
		hostname = copies[0].hostname;
	return gfarm_host_info_get_architecture_by_host(hostname);
}

struct gfs_chmod_args {
	char *path;
	gfarm_mode_t mode;
};

static char *
chmod_request_parallel(struct gfs_connection *gfs_server, void *args)
{
	struct gfs_chmod_args *a = args;

	return (gfs_client_chmod(gfs_server, a->path, a->mode));
}

static char *
get_new_section_name(enum exec_change change,
	int ncopy, 
	struct gfarm_file_section_copy_info *copies)
{
	if (change == TO_EXECUTABLE)
		return get_architecture_name(ncopy, copies);
	else /* change == TO_UNEXECUTABLE */
		return strdup("0");
}	

static char *
gfs_chmod_execfile_metadata(
	struct gfarm_path_info *pi,
	gfarm_mode_t mode,
	struct gfarm_file_section_info *from_section,
	int ncopy,
	struct gfarm_file_section_copy_info *copies,
	enum exec_change change)
{
	struct gfarm_file_section_info to_section;
	char *e;
	int i;

	to_section = *from_section;
	to_section.section = get_new_section_name(change, ncopy, copies);
	if (to_section.section == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_file_section_info_set(to_section.pathname,
					to_section.section,
					&to_section);
	if (e != NULL)
		goto finish_free_to_section_section;

	e = change_path_info_mode(pi, mode);
	if (e != NULL) {
		char *e2;
		e2 = gfarm_file_section_info_remove(
				to_section.pathname, to_section.section);
		gflog_warning("gfs_chmod: gfarm_file_section_info_remove()",
			      e2);
		goto finish_free_to_section_section;
	}

	for (i = 0; i < ncopy; i++) {
		e = gfarm_file_section_copy_info_remove(copies[i].pathname,
							copies[i].section,
							copies[i].hostname);
		if (e != NULL)
			gflog_warning(
			  "gfs_chmod:gfarm_file_section_copy_info_remove()",
			  e);
	}

	e = gfarm_file_section_info_remove(from_section->pathname,
					   from_section->section);
	if (e != NULL)
		gflog_warning("gfs_chmod:gfarm_file_section_info_remove()",
		e);		

finish_free_to_section_section:
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
				  "gfs_chmod:gfarm_host_address_get()", e);
				continue;
			}

			e = gfs_client_connect(copies[i][j].hostname,
						 &peer_addr, &gfs_server);
			if (e != NULL) {
				gflog_warning(
				  "gfs_chmod:gfarm_client_connection()", e);
				continue;
			}

			e = gfs_client_chmod(gfs_server, from_path_section,
					     mode);
			if (e != NULL) {
				gflog_warning("gfs_chmod:gfs_client_chmod()",
					      e);
				gfs_client_disconnect(gfs_server);
				continue;
			}
			if (change != EXECUTABILITY_NOT_CHANGED) {
				e = gfs_client_rename(gfs_server,
						      from_path_section,
						      to_path_section);
				if (e != NULL) {
					gflog_warning(
					  "gfs_chmod:gfs_client_rename()", e);
					gfs_client_disconnect(gfs_server);
					continue;
				}

				new_copy = copies[i][j];
				new_copy.section = section;
				e = gfarm_file_section_copy_info_set(
							new_copy.pathname,
							new_copy.section,
							new_copy.hostname,
							&new_copy);
				if (e != NULL) {
					gflog_warning(
					  "gfs_chmod:"
					  "gfarm_file_section_copy_info_set()",
					  e);
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

static char *
gfs_chmod_internal(struct gfarm_path_info *pi, gfarm_mode_t mode)
{
	char *e;
	int nsection, *ncopy, i;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info **copies;
	enum exec_change change;

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
		e = gfs_client_apply_all_hosts(chmod_request_parallel,
					 &a, "gfs_chmod", &nhosts_succeed);
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
			while (i--)
				gfarm_file_section_copy_info_free_all(
							ncopy[i], copies[i]);
			goto finish_free_copies;
		}
	}

	if (change == EXECUTABILITY_NOT_CHANGED) {
		e = change_path_info_mode(pi, mode);
	} else {
		e = gfs_chmod_execfile_metadata(pi, mode, &sections[0],
						ncopy[0], copies[0], change);
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

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);

	e = gfs_chmod_internal(&pi, mode);
	gfarm_path_info_free(&pi);
	return (e);
}

char *
gfs_fchmod(GFS_File gf, gfarm_mode_t mode)
{
	return (gfs_chmod_internal(&gf->pi, mode));
}

char *
gfs_utimes(const char *gfarm_url, const struct gfarm_timespec *tsp)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct timeval now;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_access(&pi, GFS_W_OK);
	if (e != NULL)
		goto finish_free_path_info;

	gettimeofday(&now, NULL);
	if (tsp == NULL) {
		pi.status.st_atimespec.tv_sec = 
		pi.status.st_mtimespec.tv_sec = now.tv_sec;
		pi.status.st_atimespec.tv_nsec = 
		pi.status.st_mtimespec.tv_nsec = now.tv_usec * 1000;
	} else {
		pi.status.st_atimespec = tsp[0];
		pi.status.st_mtimespec = tsp[1];
	}
	pi.status.st_ctimespec.tv_sec = now.tv_sec;
	pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
	e = gfarm_path_info_replace(pi.pathname, &pi);
 finish_free_path_info:
	gfarm_path_info_free(&pi);
	return (e);
}

static char *	
gfs_rename_clean_spool(
	char *pathname,
	int nsection,
	struct gfarm_file_section_info *sections,
	int *ncopy,
	struct gfarm_file_section_copy_info **copies)
{
	char *e, *e2, *path_section;
	int i, j;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	for (i = 0; i < nsection; i++) {
		e = gfarm_path_section(pathname, 
				       sections[i].section,
				       &path_section);
		if (e != NULL)
			return (e);
		for (j = 0; j < ncopy[i]; j++) {
			e2 = gfarm_host_address_get(copies[i][j].hostname,
						   gfarm_spool_server_port,
						   &peer_addr,
						   NULL);
			if (e2 != NULL)
				continue;
			e2 = gfs_client_connect(copies[i][j].hostname,
						 &peer_addr, &gfs_server);
			if (e2 != NULL)
				continue;
			e2 = gfs_client_unlink(gfs_server, path_section);
			gfs_client_disconnect(gfs_server);
		}
		free(path_section);
	}
	return (NULL);
}	

static char *
gfs_rename_file_spool(
	char *from_pathname,
	char *to_pathname,
	int nsection,
	struct gfarm_file_section_info *sections,
	int *ncopy,
	struct gfarm_file_section_copy_info **copies)
{
	int i, j, ok;
	char *e, *e2;
	char *from_path_section, *to_path_section;
	struct gfarm_file_section_copy_info new_copy;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	e = NULL;
	ok = 1;
	for (i = 0; i < nsection; i++) {
		e = gfarm_path_section(sections[i].pathname,
				       sections[i].section,
			       	       &from_path_section);
		if (e != NULL)
			return (e); /* XXX this leaves inconsistent state */

		e = gfarm_path_section(to_pathname,
				       sections[i].section,
				       &to_path_section);
		if (e != NULL) {
			free(from_path_section);
			return (e); /* XXX this leaves inconsistent state */
		}
		ok = 0;
		for (j = 0; j < ncopy[i]; j++) {
			e = gfarm_host_address_get(copies[i][j].hostname,
				gfarm_spool_server_port, &peer_addr, NULL);
			if (e != NULL) {
				gflog_warning("gfs_rename_file_spool:"
					      "gfarm_host_address_get()", e);
				continue;
			}

			e = gfs_client_connect(copies[i][j].hostname,
						 &peer_addr, &gfs_server);
			if (e != NULL) {
				gflog_warning("gfs_rename_file_spool:"
					      "gfarm_client_connect()", e);
				continue;
			}

			e = gfs_client_link(gfs_server, from_path_section,
					     		to_path_section);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) {
				if (gfs_pio_remote_mkdir_parent_canonical_path(
			    	    gfs_server, to_path_section) == NULL)
					e = gfs_client_link(gfs_server,
							    from_path_section,
							    to_path_section);
			}
			gfs_client_disconnect(gfs_server);
			if (e != NULL) {
				gflog_warning("gfs_rename_file_spool:"
					      "gfarm_client_link()", e);
					continue;
			}

			new_copy = copies[i][j];
			new_copy.pathname = to_pathname;
			e = gfarm_file_section_copy_info_set(new_copy.pathname,
							     new_copy.section,
							     new_copy.hostname,
							     &new_copy);
			if (e != NULL) {
				gflog_warning("gfs_rename_file_spool:"
					  "gfarm_file_section_copy_info_set()",
					  e);
				continue;
			}
			ok = 1;
		}
		if (j == 0) /* this section has no section copy info */
			ok = 1;
		free(to_path_section);
		free(from_path_section);
		if (!ok) /* none of copies of this section can be renamed */
			break;			
	}
	if (ok) { /* unlink old spool file */
		e2 = gfs_rename_clean_spool(from_pathname,
					    nsection, sections,
					    ncopy, copies);		 
		if (e2 != NULL)
			gflog_warning("gfs_rename_file_spool:"
				      "gfs_rename_clean_spool(from_pathname)",
				      e2);
	} else { /* remove new section copy info and new spool file */
		e2 = gfs_rename_clean_spool(to_pathname,
					    nsection, sections,
					    ncopy, copies);		 
	}
	return (e);
}

static char *
gfs_rename_file(struct gfarm_path_info *from_pi, const char *to_url)
{
	char *e, *e2, *gfarm_file;
	struct gfarm_path_info to_pi;
	int i, nsection, *ncopy;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info **copies;

	e = gfarm_url_make_path_for_creation(to_url, &gfarm_file);
	if (e != NULL)
		return (e);
	
	e = gfarm_path_info_get(gfarm_file, &to_pi);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
		goto finish_free_gfarm_file;
	if (e == NULL) { /* "to" side file or directory exists */
		if (GFARM_S_ISDIR(to_pi.status.st_mode)) {
			e = GFARM_ERR_IS_A_DIRECTORY;
			gfarm_path_info_free(&to_pi);
			goto finish_free_gfarm_file;
		}
		gfarm_path_info_free(&to_pi);
		e = gfs_unlink(to_url);
		if (e != NULL)
			goto finish_free_gfarm_file;
	}

	/* set "to" path info and section info */
	to_pi = *from_pi;
	to_pi.pathname = gfarm_file;
	e = gfarm_path_info_set(to_pi.pathname, &to_pi);
	if (e != NULL)
		goto finish_free_gfarm_file;
	
	e = gfarm_file_section_info_get_all_by_file(from_pi->pathname,
						&nsection, &sections);
	if (e != NULL)
		goto finish_free_gfarm_file;

	for (i = 0; i < nsection; i++) {
		struct gfarm_file_section_info to_section;

		to_section = sections[i];
		to_section.pathname = to_pi.pathname;
		e = gfarm_file_section_info_set(to_section.pathname,
						to_section.section,
						&to_section);
		if (e != NULL) {
			while (i--) {
				e2 = gfarm_file_section_info_remove(
						to_section.pathname,
						sections[i].section);
				if (e2 != NULL)
					gflog_warning("gfs_rename_file"
					"gfarm_file_section_info_remove()", e);
			}
			e2 = gfarm_path_info_remove(to_pi.pathname);
			if (e2 != NULL)
				gflog_warning("gfs_rename_file:"
					      "gfarm_path_info_remove()", e);
			goto finish_free_section_info;
		}
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
			while (i--)
				gfarm_file_section_copy_info_free_all(
							ncopy[i], copies[i]);
			goto finish_free_copies;
		}
	}

	/* change spool file name and set section copy info */
	e = gfs_rename_file_spool(from_pi->pathname, to_pi.pathname,
				  nsection, sections, ncopy, copies);
	if (e != NULL) { 
		e2 = gfarm_file_section_copy_info_remove_all_by_file(
							to_pi.pathname);
		for (i = 0; i < nsection; i++) {
			e2 = gfarm_file_section_info_remove(to_pi.pathname,
							sections[i].section);
			if (e2 != NULL)
				gflog_warning("gfs_rename_file:"
				       "gfarm_file_section_info_remove()", e2);
		}
		e2 = gfarm_path_info_remove(to_pi.pathname);
		if (e2 != NULL)
			gflog_warning("gfs_rename_file:"
				      "gfarm_path_info_remove()", e2);
	} else {		
		e = gfarm_file_section_copy_info_remove_all_by_file(
							from_pi->pathname);
		for (i = 0; i < nsection; i++) {
			e2 = gfarm_file_section_info_remove(
							sections[i].pathname,
							sections[i].section);
			if (e2 != NULL)
				gflog_warning("gfs_rename_file:"
			      	  "gfarm_file_section_info_remove()", e2);
			if (e == NULL && e2 != NULL)
				e = e2;
		}
		e2 = gfarm_path_info_remove(from_pi->pathname);
		if (e2 != NULL)
			gflog_warning("gfs_rename_file:"
			      "gfarm_file_path_info_remove()", e2);
		if (e == NULL && e2 != NULL)
			e = e2;
	}
	for (i = 0; i < nsection; i++)
		gfarm_file_section_copy_info_free_all(ncopy[i], copies[i]);

finish_free_copies:
	free(copies);
finish_free_ncopy:
	free(ncopy);
finish_free_section_info:
	gfarm_file_section_info_free_all(nsection, sections);
finish_free_gfarm_file:
	free(gfarm_file);
	return (e);
}

static char *
gfs_rename_dir(struct gfarm_path_info *from_pi, const char *to_url)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

char *
gfs_rename(const char *from_url, const char *to_url)
{
	char *e, *gfarm_file;
	struct gfarm_path_info from_pi;

	e = gfarm_url_make_path_for_creation(from_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &from_pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);

	if (GFARM_S_ISREG(from_pi.status.st_mode))
		e = gfs_rename_file(&from_pi, to_url);
	else if (GFARM_S_ISDIR(from_pi.status.st_mode))
		e = gfs_rename_dir(&from_pi, to_url);
	else
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;

	gfarm_path_info_free(&from_pi);
	return (e);
}

/*
 *
 */

static char *
digest_calculate(char *filename,
		 char **digest_type, char *digest_string, size_t *md_len_p,
		 file_offset_t *filesizep)
{
	int fd, i, rv;
	EVP_MD_CTX md_ctx;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	char buffer[GFS_FILE_BUFSIZE];

	if ((fd = open(filename, O_RDONLY)) == -1)
		return (gfarm_errno_to_error(errno));
	EVP_DigestInit(&md_ctx, GFS_DEFAULT_DIGEST_MODE);
	rv = gfs_digest_calculate_local(
		fd, buffer, GFS_FILE_BUFSIZE,
		GFS_DEFAULT_DIGEST_MODE,
		&md_ctx, md_len_p, md_value, filesizep);
	close(fd);
	if (rv != 0)
		return (gfarm_errno_to_error(rv));

	for (i = 0; i < *md_len_p; i++)
		sprintf(&digest_string[i + i], "%02x",
			md_value[i]);

	*digest_type = GFS_DEFAULT_DIGEST_NAME;
	return (NULL);
}

/*
 * Register a gfarm fragment to a Meta DB.  This function is intended
 * to be used with legacy applications to register a new file.
 */

char *
gfs_pio_set_fragment_info_local(char *filename,
	char *gfarm_file, char *section)
{
	char *digest_type;
	char digest_value_string[EVP_MAX_MD_SIZE * 2 + 1];
	size_t digest_len;
	file_offset_t filesize;
	char *e = NULL;
	struct gfarm_file_section_info fi;
	struct gfarm_file_section_copy_info fci;

	/* Calculate checksum. */
	e = digest_calculate(filename, &digest_type, digest_value_string,
			     &digest_len, &filesize);
	if (e != NULL)
		return (e);

	/* Update the filesystem metadata. */
	e = gfarm_file_section_info_get(gfarm_file, section, &fi);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		fi.filesize = filesize;
		fi.checksum_type = digest_type;
		fi.checksum = digest_value_string;

		e = gfarm_file_section_info_set(gfarm_file, section, &fi);
		if (e != NULL)
			return (e);
	}
	else if (e == NULL) {
		if (fi.filesize != filesize)
			return "file size mismatch";
		if (strcasecmp(fi.checksum_type, digest_type) != 0)
			return "checksum type mismatch";
		if (strcasecmp(fi.checksum,digest_value_string) != 0)
			return "check sum mismatch";

		gfarm_file_section_info_free(&fi);
	}
	else
		return (e);

	e = gfarm_host_get_canonical_self_name(&fci.hostname);
	if (e == NULL) {
		e = gfarm_file_section_copy_info_set(
			gfarm_file, section, fci.hostname, &fci);
	}

	return (e);
}

static char *
gfarm_file_section_replicate_from_to_by_gfrepbe(
	char *gfarm_file, char *section, char *srchost, char *dsthost)
{
	char *e;
	char *e2;
	gfarm_stringlist gfarm_file_list;
	gfarm_stringlist section_list;

	e = gfarm_stringlist_init(&gfarm_file_list);
	if (e != NULL)
		return (e);
	e = gfarm_stringlist_init(&section_list);
	if (e != NULL) {
		gfarm_stringlist_free(&gfarm_file_list);
		return (e);
	}
	e = gfarm_stringlist_add(&gfarm_file_list, gfarm_file);
	e2 = gfarm_stringlist_add(&section_list, section);
	if (e == NULL && e2 == NULL) {
		e = gfarm_file_section_replicate_multiple(
		    &gfarm_file_list, &section_list, srchost, dsthost, &e2);
	}
	gfarm_stringlist_free(&gfarm_file_list);
	gfarm_stringlist_free(&section_list);
	return (e != NULL ? e : e2);
}

/*
 * 0: use gfrepbe_client/gfrepbe_server
 * 1: use gfs_client_bootstrap_replicate_file()
 */
#if 0 /* XXX for now */
static int gfarm_replication_method = GFARM_REPLICATION_NORMAL_METHOD;
#else
static int gfarm_replication_method = GFARM_REPLICATION_BOOTSTRAP_METHOD;
#endif

int
gfarm_replication_get_method(void)
{
	return (gfarm_replication_method);
}

void
gfarm_replication_set_method(int method)
{
	gfarm_replication_method = method;
}

/*
 * `srchost' must already reflect "address_use" directive.
 */
static char *
gfarm_file_section_replicate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *path_section;
	struct gfarm_file_section_copy_info ci;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	e = gfarm_host_get_canonical_name(dsthost, &ci.hostname);
	if (e != NULL)
		goto finish;
	if (gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    ci.hostname)) /* already exists, don't have to replicate */
		goto finish_hostname;

	if (gfarm_replication_method != GFARM_REPLICATION_BOOTSTRAP_METHOD) {
		e = gfarm_file_section_replicate_from_to_by_gfrepbe(
		    gfarm_file, section,
		    src_canonical_hostname, ci.hostname);
		goto finish_hostname;
	}

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		goto finish_hostname;

	e = gfarm_host_address_get(dsthost, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		goto finish_path_section;

	e = gfs_client_connect(ci.hostname, &peer_addr, &gfs_server);
	if (e != NULL)
		goto finish_path_section;
	e = gfs_client_bootstrap_replicate_file(gfs_server,
	    path_section, mode, file_size,
	    src_canonical_hostname, src_if_hostname);
	/* FT - the parent directory of the destination may be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		if (gfs_pio_remote_mkdir_parent_canonical_path(
			    gfs_server, gfarm_file) == NULL)
			e = gfs_client_bootstrap_replicate_file(
				gfs_server, path_section, mode, file_size,
				src_canonical_hostname, src_if_hostname);
	}
#if 0 /* XXX - not implemented yet */
	/* FT - source file should be missing */
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* XXX - need to check explicitly */
		if (gfs_client_exist() == GFARM_ERR_NO_SUCH_OBJECT)
			/* Delete the section copy info */
			if (gfarm_file_section_copy_info_remove(gfarm_file,
				section, src_canonical_hostname) == NULL)
				e = GFARM_ERR_INCONSISTENT_RECOVERABLE;
	}
#endif
	gfs_client_disconnect(gfs_server);
	if (e != NULL)
		goto finish_path_section;
	e = gfarm_file_section_copy_info_set(gfarm_file, section,
	    ci.hostname, &ci);

finish_path_section:
	free(path_section);
finish_hostname:
	free(ci.hostname);
finish:
	return (e);
}

static char *
gfarm_file_section_replicate_to_internal(char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost)
{
	char *e, *srchost, *if_hostname;
	struct sockaddr peer_addr;

	e = gfarm_file_section_host_schedule(gfarm_file, section, &srchost);
	if (e != NULL)
		goto finish;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_srchost;

	e = gfarm_file_section_replicate_from_to_internal(
	    gfarm_file, section, mode & GFARM_S_ALLPERM, file_size,
	    srchost, if_hostname, dsthost);

	free(if_hostname);
finish_srchost:
	free(srchost);
finish:
	return (e);
}

char *
gfarm_url_section_replicate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	char *e, *gfarm_file, *canonical_hostname, *if_hostname;
	struct sockaddr peer_addr;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode_allowed = 0, mode_mask = GFARM_S_ALLPERM;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;

	e = gfarm_host_get_canonical_name(srchost, &canonical_hostname);
	if (e != NULL)
		goto finish_canonical_hostname;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_section_info;
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL)
			goto finish_path_info;
		mode_allowed = 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	e = gfarm_file_section_replicate_from_to_internal(
	    gfarm_file, section,
	    (pi.status.st_mode | mode_allowed) & mode_mask, si.filesize,
	    canonical_hostname, if_hostname, dsthost);

	free(if_hostname);
finish_canonical_hostname:
	free(canonical_hostname);
finish_section_info:
	gfarm_file_section_info_free(&si);
finish_path_info:
	gfarm_path_info_free(&pi);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e);
}

char *
gfarm_url_section_replicate_to(
	const char *gfarm_url, char *section, char *dsthost)
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode_allowed = 0, mode_mask = GFARM_S_ALLPERM;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL)
			goto finish_path_info;
		mode_allowed = 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	e = gfarm_file_section_replicate_to_internal(
	    gfarm_file, section,
	    (pi.status.st_mode | mode_allowed) & mode_mask, si.filesize,
	    dsthost);

	gfarm_file_section_info_free(&si);
finish_path_info:
	gfarm_path_info_free(&pi);
finish_gfarm_file:
	free(gfarm_file);
finish:
	return (e);
}

char *
gfarm_url_program_register(
	const char *gfarm_url, char *architecture,
	char *filename, int nreplicas)
{
	char *e, *e_save = NULL, *if_hostname, *gfarm_file;
	int nhosts, fd, i;
	struct gfarm_host_info *hosts;
	struct sockaddr peer_addr;
	size_t rv;
	int length; /* XXX - should be size_t */
	GFS_File gf;
	struct stat s;
	char buffer[GFS_FILE_BUFSIZE];
	char *self_name;
	char **hostnames;

	if (stat(filename, &s) == -1)
		return (gfarm_errno_to_error(errno));
	if (!GFARM_S_IS_PROGRAM(s.st_mode))
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	/* XXX - use better strategy for the replication */

	e = gfarm_host_info_get_allhost_by_architecture(architecture,
	    &nhosts, &hosts);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return ("gfarm_url_program_register(): no such architecture");
	if (e != NULL)
		goto finish;


	hostnames = malloc(sizeof(char *) * nhosts);
	if (hostnames == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_host_info;
	}
	for (i = 0; i < nhosts; i++)
		hostnames[i] = hosts[i].hostname;
	if (nhosts < nreplicas)
		nreplicas = nhosts;

	if (gfarm_host_get_canonical_self_name(&self_name) != NULL) {
		e = gfarm_schedule_search_idle_hosts(nhosts, hostnames,
						     nreplicas, hostnames);
	} else {
		/* give the top priority to self host */
		char * tmp;
		for (i = 0; i < nhosts; i++)
			if (strcmp(hostnames[i], self_name) == 0)
				break;
		if (i != 0 && i < nhosts) {
			tmp = hostnames[0];
			hostnames[0] = hostnames[i];
			hostnames[i] = tmp;
		}	
		if (nreplicas > 1)
			e = gfarm_schedule_search_idle_hosts(nhosts - 1,
							     hostnames + 1,
							     nreplicas - 1,
							     hostnames + 1);
	}
	if (e != NULL)
		goto finish_hostnames;

	/* reflect "address_use" directive in the `hostnames[0]' */
	e = gfarm_host_address_get(hostnames[0], gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_hostnames;

	/*
	 * register the program
	 */
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		e = "gfarm_url_program_register(): can't open program";
		goto finish_if_hostname;
	}
	/* XXX - overwrite case */
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY,
	    s.st_mode & GFARM_S_ALLPERM, &gf);
	if (e != NULL) {
		close(fd);
		goto finish_if_hostname;
	}
	/* XXX - better strategy to select replica */
	e = gfs_pio_set_view_section(gf, architecture, if_hostname, 0);
	if (e != NULL) {
		/* XXX - take care of the case where node is down */
		gfs_pio_close(gf);
		close(fd);
		goto finish_if_hostname;
	}
	for (;;) {
		rv = read(fd, buffer, sizeof(buffer));
		if (rv <= 0)
			break;
		/* XXX - partial write case ? */
		e = gfs_pio_write(gf, buffer, rv, &length);
		if (e != NULL)
			break;
	}
	e_save = e;
	e = gfs_pio_close(gf);
	close(fd);
	if (e_save != NULL)
		e = e_save;
	if (e != NULL)
		goto finish_if_hostname;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish_if_hostname;

	/*
	 * replicate the program
	 */
	for (i = 1; i < nreplicas; i++) {
		/* XXX - better strategy to select replica */
		e = gfarm_file_section_replicate_from_to_internal(
		    gfarm_file, architecture,
		    s.st_mode & GFARM_S_ALLPERM, s.st_size,
		    hostnames[0], if_hostname, hostnames[i]);
		if (e != NULL)
			e_save = e;
	}
	e = NULL; /* there is at least on copy available. */
	/* XXX - partial error case? */

	free(gfarm_file);
finish_if_hostname:
	free(if_hostname);
finish_hostnames:
	free(hostnames);
finish_host_info:
	gfarm_host_info_free_all(nhosts, hosts);
finish:
	return (e);
}

char *
gfarm_url_program_deliver(const char *gfarm_url, int nhosts, char **hosts,
			  char ***delivered_paths)
{
	char *e, **dp, *gfarm_file, *root, *arch, **canonical_hostnames;
	gfarm_mode_t mode, mode_mask = GFARM_S_ALLPERM;
	int i;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
	mode = pi.status.st_mode;
	if (!GFARM_S_IS_PROGRAM(mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("gfarm_url_program_deliver(): not a program");
	}
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	/*
	 * XXX FIXME
	 * This may be called with GFARM_REPLICATION_BOOTSTRAP_METHOD
	 * to deliver gfrepbe_client/gfrepbe_server.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_X_OK);
		if (e != NULL) {
			gfarm_path_info_free(&pi);
			free(gfarm_file);
			return (e);
		}
		mode |= 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	gfarm_path_info_free(&pi);
	dp = malloc(sizeof(char *) * (nhosts + 1));
	if (dp == NULL) {
		free(gfarm_file);
		return (GFARM_ERR_NO_MEMORY);
	}
	dp[nhosts] = NULL;

	e = gfarm_host_get_canonical_names(nhosts, hosts,
	    &canonical_hostnames);
	if (e != NULL) {
		free(dp);
		free(gfarm_file);
		return (e);
	}

	/* XXX - this is too slow */
	for (i = 0; i < nhosts; i++) {
		struct sockaddr peer_addr;
		struct gfs_connection *gfs_server;
		struct gfarm_file_section_info si;

		dp[i] = NULL; /* for error handling */
		arch = gfarm_host_info_get_architecture_by_host(
		    canonical_hostnames[i]);
		if (arch == NULL) {
			/* architecture of the hosts[i] is not registered */
			e = "cannot deliver program to an unregistered host";
			goto error;
		}

		/* XXX - which to use? `hosts[i]' vs `copies[j].hostname' */
		e = gfarm_host_address_get(hosts[i],
		    gfarm_spool_server_port, &peer_addr, NULL);
		if (e != NULL) {
			free(arch);
			goto error;
		}

		e = gfs_client_connection(canonical_hostnames[i], &peer_addr,
		    &gfs_server);
		if (e != NULL) {
			free(arch);
			goto error;
		}
		e = gfs_client_get_spool_root(gfs_server, &root);
		if (e != NULL) {
			free(arch);
			goto error;
		}
		e = gfarm_full_path_file_section(root, gfarm_file,
		    arch, &dp[i]);
		free(root);
		if (e != NULL) {
			free(arch);
			goto error;
		}
		e = gfarm_file_section_info_get(gfarm_file, arch, &si);
		if (e != NULL) {
			free(arch);
			goto error;
		}

		/*
		 * replicate the program
		 */
		e = gfarm_file_section_replicate_to_internal(gfarm_file, arch,
		    mode & mode_mask, si.filesize, hosts[i]);
		gfarm_file_section_info_free(&si);
		free(arch);
		if (e != NULL)
			goto error;
	}

	gfarm_strings_free_deeply(nhosts, canonical_hostnames);
	free(gfarm_file);
	*delivered_paths = dp;
	return (NULL);

error:
	gfarm_strings_free_deeply(nhosts, canonical_hostnames);
	free(gfarm_file);
	gfarm_strings_free_deeply(i + 1, dp);
	*delivered_paths = NULL;
	return (e);
}

char *
gfarm_url_fragments_replicate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	char *e, *gfarm_file, **srchosts, **edsthosts;
	int nsrchosts;
	gfarm_mode_t mode, mode_mask = GFARM_S_ALLPERM;
	int i, pid, *pids;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;

	mode = pi.status.st_mode;
	if (!GFARM_S_IS_FRAGMENTED_FILE(mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto finish_gfarm_file;
	}
	/*
	 * XXX - if the owner of a file is not the same, permit a
	 * group/other write access - This should be fixed in the next
	 * major release.
	 */
	if (strcmp(pi.status.st_user, gfarm_get_global_username()) != 0) {
		e = gfarm_path_info_access(&pi, GFS_R_OK);
		if (e != NULL) {
			gfarm_path_info_free(&pi);
			free(gfarm_file);
			return (e);
		}
		mode |= 022;
		mode_mask = 0777; /* don't allow setuid/setgid */
	}
	gfarm_path_info_free(&pi);
	e = gfarm_url_hosts_schedule(gfarm_url, "", &nsrchosts, &srchosts);
	if (e != NULL)
		goto finish_gfarm_file;

	edsthosts = malloc(sizeof(*edsthosts) * nsrchosts);
	if (edsthosts == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_srchosts;
	}
	gfarm_strings_expand_cyclic(ndsthosts, dsthosts, nsrchosts, edsthosts);

	pids = malloc(sizeof(int) * nsrchosts);
	if (pids == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_edsthosts;
	}
	/*
	 * To use different connection for each metadb access.
	 *
	 * XXX: FIXME layering violation
	 */
	e = gfarm_metadb_terminate();
	if (e != NULL)
		goto finish_pids;

	for (i = 0; i < nsrchosts; i++) {
		struct sockaddr peer_addr;
		char *if_hostname;
		char section_string[GFARM_INT32STRLEN + 1];
		struct gfarm_file_section_info si;

		pid = fork();
		if (pid < 0)
			break;
		if (pid) {
			/* parent */
			pids[i] = pid;
			continue;
		}
		/* child */

		/*
		 * use different connection for each metadb access.
		 *
		 * XXX: FIXME layering violation
		 */
		e = gfarm_metadb_initialize();
		if (e != NULL)
			_exit(1);

		/* reflect "address_use" directive in the `srchosts[i]' */
		e = gfarm_host_address_get(srchosts[i],
		    gfarm_spool_server_port, &peer_addr, &if_hostname);
		if (e != NULL)
			_exit(2);

		sprintf(section_string, "%d", i);
		e = gfarm_file_section_info_get(gfarm_file, section_string,
		    &si);
		if (e != NULL)
			_exit(3);

		e = gfarm_file_section_replicate_from_to_internal(
		    gfarm_file, section_string,
		    mode & mode_mask, si.filesize,
		    srchosts[i], if_hostname, edsthosts[i]);
		if (e != NULL)
			_exit(1);
		_exit(0);
	}
	while (--i >= 0) {
		int rv, s;

		while ((rv = waitpid(pids[i], &s, 0)) == -1 && errno == EINTR)
			;
		if (rv == -1) {
			if (e == NULL)
				e = gfarm_errno_to_error(errno);
		} else if (WIFEXITED(s) && WEXITSTATUS(s) != 0) {
			e = "error happens on replication";
		}
	}
	/*
	 * recover temporary closed metadb connection
	 *
	 * XXX: FIXME layering violation
	 */
	if (e != NULL) {
		gfarm_metadb_initialize();
	} else {
		e = gfarm_metadb_initialize();
	}
 finish_pids:
	free(pids);
 finish_edsthosts:
	free(edsthosts);
 finish_srchosts:
	gfarm_strings_free_deeply(nsrchosts, srchosts);
 finish_gfarm_file:
	free(gfarm_file);
	return (e);
}

char *
gfarm_url_fragments_replicate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	char *e, **dsthosts;
	int nfrags;

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);

	dsthosts = malloc(nfrags * sizeof(char *));
	if (dsthosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_schedule_search_idle_by_domainname(domainname,
		nfrags, dsthosts);
	if (e != NULL)
		goto free_dsthosts;

	e = gfarm_url_fragments_replicate(gfarm_url, nfrags, dsthosts);

	while (--nfrags >= 0)
		free(dsthosts[nfrags]);
 free_dsthosts:
	free(dsthosts);

	return (e);
}

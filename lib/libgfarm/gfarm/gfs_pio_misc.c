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
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>

#include <gfarm/gfarm.h>

#include "timer.h"
#include "gfutil.h"

#include "host.h"
#include "config.h"
#include "gfs_proto.h" /* for gfs_digest_calculate_local() */
#include "gfs_client.h"
#include "gfs_pio.h"
#include "gfs_lock.h"
#include "gfs_misc.h"
#include "schedule.h"

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
		e = gfs_check_section_busy_by_finfo(&sections[i]);
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
	long ino;
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
		return (GFARM_ERR_NO_FRAGMENT_INFORMATION);
	}
	return (e);
}

double gfs_stat_time;

char *
gfs_stat(const char *path, struct gfs_stat *s)
{
	char *e, *p;
	gfarm_timerval_t t1, t2;
	long ino;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	t1 = 0;
#endif
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

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto free_gfarm_file;

	e = gfarm_path_info_access(&pi, mode);
	gfarm_path_info_free(&pi);

free_gfarm_file:
	free(gfarm_file);
	return (e);
}


char *
gfs_utimes(const char *gfarm_url, const struct gfarm_timespec *tsp)
{
	char *e, *gfarm_file, *user;
	struct gfarm_path_info pi;
	struct timeval now;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(gfarm_file, &pi);
	free(gfarm_file);
	if (e != NULL)
		return (e);
	user = gfarm_get_global_username();
	if (user == NULL)
		return ("gfs_utimes(): programming error, "
			"gfarm library isn't properly initialized");
	if (strcmp(pi.status.st_user, user) != 0)
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

/*
 * gfs_rename
 */
char *
gfs_clean_spool(
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
rename_file_spool(
	char *from_pathname,
	char *to_pathname,
	int nsection,
	struct gfarm_file_section_info *sections,
	int *ncopy,
	struct gfarm_file_section_copy_info **copies)
{
	int i, j;
	char *e;
	char *from_path_section, *to_path_section;
	struct gfarm_file_section_copy_info new_copy;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	/*
	 * 1 - succeed in linking at least 1 spool file for all sections
	 * 0 - otherwise
	 */
	int ok;

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
				gflog_warning(
					"rename_file_spool: host_address_get: "
					"%s: %s", copies[i][j].hostname, e);
				continue;
			}

			e = gfs_client_connect(copies[i][j].hostname,
					       &peer_addr, &gfs_server);
			if (e != NULL) {
				gflog_warning("rename_file_spool: "
					      "gfs_client_connect: %s: %s",
					      copies[i][j].hostname, e);
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
				gflog_warning("rename_file_spool: "
				    "gfs_client_link(%s, %s): %s",
				    from_path_section, to_path_section, e);
				continue;
			}

			new_copy = copies[i][j];
			new_copy.pathname = to_pathname;
			e = gfarm_file_section_copy_info_set(new_copy.pathname,
				new_copy.section, new_copy.hostname, &new_copy);
			if (e != NULL) {
				gflog_warning("rename_file_spool: "
				    "file_section_copy_info_set: "
				    "%s (%s) on %s: %s", new_copy.pathname,
				    new_copy.section, new_copy.hostname, e);
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
	if (ok) {
		e = NULL;
	} else {  /* unlink new spool file */
		/* ignore error code here */
		gfs_clean_spool(to_pathname, nsection, sections,
		    ncopy, copies);
	}
	return (e);
}

static char *
remove_infos_all(char *pathname)
{
	char *e, *e2;

	e = gfarm_file_section_copy_info_remove_all_by_file(pathname);
	e2 = gfarm_file_section_info_remove_all_by_file(pathname);
	if (e2 != NULL)
		gflog_warning("remove_infos_all: "
			      "file_section_info_remove_all_by_file: %s: %s",
			      pathname, e2);
	if (e == NULL)
		e = e2;
	e2 = gfarm_path_info_remove(pathname);
	if (e2 != NULL)
		gflog_warning("remove_infos_all:"
			      "path_info_remove: %s: %s", pathname, e2);
	return (e == NULL ? e2 : e);
}

static char *
link_a_file(struct gfarm_path_info *from_pip, char *newpath,
	    int *nsection, struct gfarm_file_section_info **sections,
	    int **ncopy, struct gfarm_file_section_copy_info ***copies)
{
	char *e, *e2;
	int i;
	struct gfarm_path_info to_pi;
	struct timeval now;

	to_pi = *from_pip;
	to_pi.pathname = newpath;
	gettimeofday(&now, NULL);
	to_pi.status.st_ctimespec.tv_sec = now.tv_sec;
	to_pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;

	e = gfarm_path_info_set(to_pi.pathname, &to_pi);
	if (e != NULL)
		return (e);

	e = gfarm_file_section_info_get_all_by_file(from_pip->pathname,
						    nsection, sections);
	if (e != NULL) {
		e2 = gfarm_path_info_remove(to_pi.pathname);
		if (e2 != NULL)
			gflog_warning("link_a_file: "
				"path_info_remove: %s: %s", to_pi.pathname, e2);
		return (e);
	}

	for (i = 0; i < *nsection; i++) {
		struct gfarm_file_section_info to_section;

		to_section = (*sections)[i];
		to_section.pathname = to_pi.pathname;
		e = gfarm_file_section_info_set(
			to_section.pathname, to_section.section, &to_section);
		if (e != NULL) {
			while (--i >= 0) {
				e2 = gfarm_file_section_info_remove(
					to_section.pathname,
					(*sections)[i].section);
				if (e2 != NULL)
					gflog_warning("link_a_file: "
					    "file_section_info_remove: "
					    "%s (%s): %s", to_section.pathname,
					    (*sections)[i].section, e2);
			}
			e2 = gfarm_path_info_remove(to_pi.pathname);
			if (e2 != NULL)
				gflog_warning("link_a_file: "
					"path_info_remove: %s: %s",
					to_pi.pathname, e2);
			goto finish_free_section_info;
		}
	}

	*ncopy = malloc(*nsection * sizeof(**ncopy));
	if (*ncopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_free_section_info;
	}
	*copies = malloc(*nsection * sizeof(**copies));
	if (*copies == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_free_ncopy;
	}

	for (i = 0; i < *nsection; i++) {
		e = gfarm_file_section_copy_info_get_all_by_section(
			(*sections)[i].pathname, (*sections)[i].section,
			&(*ncopy)[i], &(*copies)[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free_all(
						(*ncopy)[i], (*copies)[i]);
			goto finish_free_copies;
		}
	}

	/* change spool file name and set section copy info */
	e = rename_file_spool(from_pip->pathname, to_pi.pathname,
				  *nsection, *sections, *ncopy, *copies);
	if (e == NULL)
		return (NULL);

	e2 = remove_infos_all(to_pi.pathname);

	for (i = 0; i < *nsection; i++)
		gfarm_file_section_copy_info_free_all((*ncopy)[i],
						      (*copies)[i]);
finish_free_copies:
	free(*copies);
finish_free_ncopy:
	free(*ncopy);
finish_free_section_info:
	gfarm_file_section_info_free_all(*nsection, *sections);
	return (e);
}

static char *
rename_single_file(struct gfarm_path_info *from_pi, char *newpath)
{
	char *e, *e2;
	int i, nsection, *ncopy;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info **copies;

#ifdef __GNUC__
	/* workaround gcc warning: 'copies' may be used uninitialized */
	copies = NULL;
	/* workaround gcc warning: 'ncopy' may be used uninitialized */
	ncopy = NULL;
#endif
	e = link_a_file(from_pi, newpath,
			&nsection, &sections, &ncopy, &copies);
	if (e == NULL) {
		e2 = gfs_clean_spool(from_pi->pathname,
					nsection, sections,
					ncopy, copies);
		if (e2 != NULL)
			gflog_warning("rename_single_file: "
				      "gfs_clean_spool: %s: %s",
				      from_pi->pathname, e2);

		e2 = remove_infos_all(from_pi->pathname);
		if (e2 != NULL)
			gflog_warning("rename_single_file: "
				      "rename_infos_all: %s: %s",
				      from_pi->pathname, e2);

		for (i = 0; i < nsection; i++)
			gfarm_file_section_copy_info_free_all(ncopy[i],
							      copies[i]);
		free(copies);
		free(ncopy);
		gfarm_file_section_info_free_all(nsection, sections);
	}
	return (e);
}

static char *
add_cwd_to_relative_path(char *cwd, const char *path)
{
	char *p;

	p = malloc(strlen(cwd) + strlen(path) + 2);
	if (p == NULL)
		return (NULL);
	sprintf(p, "%s/%s", cwd, path);
	return (p);
}

static char *
traverse_file_tree(char *cwd, char *path,
		   gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char *e;
	struct gfs_stat gs;
	GFS_Dir dir;
	struct gfs_dirent *entry;
	gfarm_mode_t mode;
	char *dpath;

	e = gfs_stat(path, &gs);
	if (e != NULL)
		return (e);
	mode = gs.st_mode;
	gfs_stat_free(&gs);
	dpath = add_cwd_to_relative_path(cwd, path);
	if (dpath == NULL)
		return (GFARM_ERR_NO_MEMORY);
	if (GFARM_S_ISREG(mode)) {
		e = gfarm_stringlist_add(file_list, dpath);
	} else if (GFARM_S_ISDIR(mode)) {
		e = gfarm_stringlist_add(dir_list, dpath);
		if (e != NULL)
			return (e);
		e = gfs_chdir(path);
		if (e != NULL)
			return (e);
		e = gfs_opendir(".", &dir);
		if (e != NULL)
			return (e);
		while ((e = gfs_readdir(dir, &entry)) == NULL &&
				entry != NULL) {
			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			e = traverse_file_tree(dpath, entry->d_name,
					       dir_list, file_list);
			if (e != NULL)
				return (e);
		}
		if (e != NULL)
			return (e);
		gfs_closedir(dir);
		e = gfs_chdir("..");
	}
	return (e);
}

static void
rename_spool_node_dir(char *hostname,
		      char *from, char *to,
		      int nfile, int *nsection, int **ncopy,
		      struct gfarm_file_section_copy_info ***copies,
		      unsigned char ***exist)
{
	char *e;
	struct sockaddr peer_addr;
	struct gfs_connection *gfs_server;
	int i, j, k;

	e = gfarm_host_address_get(hostname,
		gfarm_spool_server_port, &peer_addr, NULL);
	if (e != NULL) {
		gflog_warning(
			"rename_spool_node_dir: host_address_get: %s: %s",
			hostname, e);
		return;
	}
	e = gfs_client_connect(hostname, &peer_addr, &gfs_server);
	if (e != NULL) {
		gflog_warning(
			"rename_spool_node_dir: gfs_client_connect: %s: %s",
			hostname, e);
		return;
	}
	e = gfs_client_rename(gfs_server, from, to);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_warning(
			"rename_spool_node_dir: gfs_client_rename(%s, %s): %s",
			from, to, e);
		return;
	}
	for (i = 0; i < nfile; i++) {
		for (j = 0; j < nsection[i]; j++) {
			for (k = 0; k < ncopy[i][j]; k++) {
				if (strcmp(
				      copies[i][j][k].hostname, hostname) == 0)
					exist[i][j][k] = 1;
			}
		}
	}
	gfs_client_disconnect(gfs_server);
}

static char *
get_lists(const char *from_url,
		       gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char *e, cwdbf[PATH_MAX * 2];
	GFS_Dir dir;
	struct gfs_dirent *entry;

	e = gfs_getcwd(cwdbf, sizeof(cwdbf));
	if (e != NULL)
		return (e);
	e = gfs_chdir(from_url);
	if (e != NULL)
		return (e);
	e = gfs_opendir(".", &dir);
	if (e != NULL)
		return (e);
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		if (strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strcmp(entry->d_name, ".") == 0) {
			gfarm_stringlist_add(dir_list, "");
			continue;
		}
		e = traverse_file_tree("", entry->d_name, dir_list, file_list);
		if (e != NULL)
			return (e);
	}
	if (e != NULL)
		return (e);
	gfs_closedir(dir);
	e = gfs_chdir(cwdbf);
	return (e);
}

static char *get_infos_by_file(char *pathname,
		int *nsection, struct gfarm_file_section_info **sections,
		int **ncopy, struct gfarm_file_section_copy_info ***copies)
{
	char *e;
	int i;

	e = gfarm_file_section_info_get_all_by_file(pathname,
						    nsection, sections);
	if (e != NULL)
		return (e);
	*ncopy = malloc(*nsection * sizeof(**ncopy));
	if (*ncopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_free_section_info;
	}
	*copies = malloc(*nsection * sizeof(**copies));
	if (*copies == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_free_ncopy;
	}
	for (i = 0; i < *nsection; i++) {
		e = gfarm_file_section_copy_info_get_all_by_section(
			(*sections)[i].pathname, (*sections)[i].section,
			&(*ncopy)[i], &(*copies)[i]);
		if (e != NULL) {
			while (--i >= 0)
				gfarm_file_section_copy_info_free_all(
						(*ncopy)[i], (*copies)[i]);
			goto finish_free_copies;
		}
	}
	return (NULL);

finish_free_copies:
	free(*copies);
finish_free_ncopy:
	free(*ncopy);
finish_free_section_info:
	gfarm_file_section_info_free_all(*nsection, *sections);
	return (e);
}

static void
free_infos(int nsection, struct gfarm_file_section_info *sections,
	   int *ncopy, struct gfarm_file_section_copy_info **copies)
{
	int i;

	for (i = 0; i < nsection; i++) {
		gfarm_file_section_copy_info_free_all(ncopy[i], copies[i]);
	}
	gfarm_file_section_info_free_all(nsection, sections);
}

static char *
get_path_infos(char *from_canonic_path,
	       gfarm_stringlist *list, struct gfarm_path_info *path_infos)
{
	int i;
	char *e = NULL;

	for (i = 0; i < gfarm_stringlist_length(list); i++) {
		char *p, *elem;

		elem = gfarm_stringlist_elem(list, i);
		p = malloc(strlen(from_canonic_path) + strlen(elem) + 1);
		if (p == NULL)
			return (GFARM_ERR_NO_MEMORY);
		sprintf(p, "%s%s", from_canonic_path, elem);
		e = gfarm_path_info_get(p, &path_infos[i]);
		free(p);
		if (e != NULL)
			break;
	}
	if (e != NULL) {
		while (--i >= 0)
			 gfarm_path_info_free(&path_infos[i]);
	}
	return (e);
}

static void
free_path_infos(int n, struct gfarm_path_info *path_infos)
{
	int i;

	for (i = 0; i < n; i++)
		gfarm_path_info_free(&path_infos[i]);
}

static char *
get_meta_data(const char *from_url,
	      gfarm_stringlist *dir_list,
	      gfarm_stringlist *file_list,
	      struct gfarm_path_info *dir_path_infos,
	      struct gfarm_path_info *file_path_infos,
	      int *nsection, struct gfarm_file_section_info **sections,
	      int **ncopy, struct gfarm_file_section_copy_info ***copies)
{
	char *e, *from_canonic_path;
	int i;

	e = gfarm_url_make_path_for_creation(from_url, &from_canonic_path);
	if (e != NULL)
		return (e);
	e = get_path_infos(from_canonic_path, dir_list, dir_path_infos);
	if (e != NULL)
		goto free_from_canonic_path;
	e = get_path_infos(from_canonic_path, file_list, file_path_infos);
	if (e != NULL)
		goto free_dir_path_infos;
	for (i = 0; i < gfarm_stringlist_length(file_list); i++) {
		e = get_infos_by_file(
			file_path_infos[i].pathname,
			&nsection[i], &sections[i], &ncopy[i], &copies[i]);
		if (e != NULL) {
			while (--i >= 0) {
				free_infos(nsection[i], sections[i],
					   ncopy[i], copies[i]);
			}
			goto free_file_path_infos;
		}
	}
	return (NULL);

free_file_path_infos:
	free_path_infos(gfarm_stringlist_length(file_list), file_path_infos);
free_dir_path_infos:
	free_path_infos(gfarm_stringlist_length(dir_list), dir_path_infos);
free_from_canonic_path:
	free(from_canonic_path);
	return(e);
}

static char *
set_a_path_info(char *from_dir_canonical_path,
		char *to_dir_canonical_path,
		struct gfarm_path_info *from_path_info,
		char **to_path)
{
	char *p;
	struct gfarm_path_info to_pi;
	struct timeval now;

	p = malloc(strlen(from_path_info->pathname) -
			 strlen(from_dir_canonical_path) +
			 strlen(to_dir_canonical_path) + 1);
	if (p == NULL) {
		return (GFARM_ERR_NO_MEMORY);
	}
	sprintf(p, "%s%s", to_dir_canonical_path,
	    from_path_info->pathname + strlen(from_dir_canonical_path));
	to_pi = *from_path_info;
	to_pi.pathname = p;
	*to_path = p;
	gettimeofday(&now, NULL);
	to_pi.status.st_ctimespec.tv_sec = now.tv_sec;
	to_pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;

	return (gfarm_path_info_set(to_pi.pathname, &to_pi));
}

static char *
set_meta_data(int ndir, int nfile,
	      char *from_canonical_path, char *to_canonical_path,
	      struct gfarm_path_info *dir_path_infos,
	      struct gfarm_path_info *file_path_infos,
	      int *nsection, struct gfarm_file_section_info **sections,
	      int **ncopy, struct gfarm_file_section_copy_info ***copies,
	      unsigned char ***exist)
{
	int i, j, k;
	char *e, *to_path;

	e = NULL;
	for (i = 0; i < ndir; i++) {
		e = set_a_path_info(from_canonical_path,
			to_canonical_path, &dir_path_infos[i], &to_path);
		if (e != NULL)
			return (e);
	}
	for (i = 0; i < nfile; i++) {
		e = set_a_path_info(from_canonical_path,
			to_canonical_path, &file_path_infos[i], &to_path);
		if (e != NULL)
			return (e);

		for (j = 0; j < nsection[i]; j++) {
			struct gfarm_file_section_info to_si;
			to_si = sections[i][j];
			to_si.pathname = to_path;
			e = gfarm_file_section_info_set(to_si.pathname,
						to_si.section, &to_si);
			if (e != NULL)
				return (e);
			for (k = 0; k < ncopy[i][j]; k++) {
				struct gfarm_file_section_copy_info
					to_ci;
					if (exist[i][j][k] == 0)
					continue;
				to_ci = copies[i][j][k];
				to_ci.pathname = to_path;
				e = gfarm_file_section_copy_info_set(
					to_ci.pathname, to_ci.section,
					to_ci.hostname,  &to_ci);
				if (e != NULL)
					return (e);
			}
		}
	}
	return (e);
}

static void
free_meta_data(int ndir, struct gfarm_path_info *dir_path_infos,
	       int nfile, struct gfarm_path_info *file_path_infos,
	       int *nsection, struct gfarm_file_section_info **sections,
	       int **ncopy, struct gfarm_file_section_copy_info ***copies)
{
	int i;

	for (i = 0; i < nfile; i++) {
		free_infos(nsection[i], sections[i], ncopy[i], copies[i]);
	}
	free_path_infos(nfile, file_path_infos);
	free_path_infos(ndir, dir_path_infos);
}

static int
check_existence_spool_file(int nfile, int *nsection, int **ncopy,
			   unsigned char ***exist)
{
	int i, j, k;

	for (i = 0; i < nfile; i++) {
		if (nsection[i] == 0)
			continue;
		for (j = 0; j < nsection[i]; j++) {
			if (ncopy[i][j] == 0)
				continue;
			for (k = 0; k < ncopy[i][j]; k++) {
				if (exist[i][j][k] == 1)
					break;
			}
			if (k >= ncopy[i][j])
				return (0);
		}
	}
	return (1);
}

static char *
rename_dir(const char *from_url, const char *to_url,
	   char *from_canonical_path, char *to_canonical_path)
{
	int i, j, nhosts;
	char *e;
	struct gfarm_host_info *hosts;
	gfarm_stringlist dir_list, file_list;
	int ndir, nfile;
	int *nsection, **ncopy;
	struct gfarm_path_info *dir_path_infos, *file_path_infos;
	struct gfarm_file_section_info **sections;
	struct gfarm_file_section_copy_info ***copies;
	unsigned char ***exist; /* whether spool file exists at the host */

	e = gfarm_stringlist_init(&dir_list);
	if (e != NULL)
		return (e);
	e = gfarm_stringlist_init(&file_list);
	if (e != NULL)
		goto free_dir_list;
	e = get_lists(from_url, &dir_list, &file_list);
	if (e != NULL)
		goto free_file_list;
	ndir = gfarm_stringlist_length(&dir_list);
	nfile = gfarm_stringlist_length(&file_list);

	dir_path_infos = malloc(ndir * sizeof(*dir_path_infos));
	if (dir_path_infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_file_list;
	}
	file_path_infos = malloc(nfile * sizeof(*file_path_infos));
	if (file_path_infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_dir_path_infos;
	}
	nsection = malloc(nfile * sizeof(*nsection));
	if (nsection == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_file_path_infos;
	}
	sections = malloc(nfile * sizeof(*sections));
	if (sections == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_nsection;
	}
	ncopy = malloc(nfile * sizeof(*ncopy));
	if (ncopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_sections;
	}
	copies = malloc(nfile * sizeof(*copies));
	if (copies == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_ncopy;
	}
	e = get_meta_data(from_url, &dir_list, &file_list,
			  dir_path_infos, file_path_infos,
			  nsection, sections, ncopy, copies);
	if (e != NULL)
		goto free_copies;
	/*
	 * allocate table to check spool files existence
	 */
	exist = malloc(nfile * sizeof(*exist));
	if (exist == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_meta_data;
	}
	for (i = 0; i < nfile; i++) {
		exist[i] = malloc(nsection[i] * sizeof(**exist));
		if (exist[i] == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			while (--i >= 0)
				free(exist[i]);
			goto free_meta_data;
		}
		for (j = 0; j < nsection[i]; j++) {
			int size = ncopy[i][j] * sizeof(***exist);

			exist[i][j] = malloc(size);
			if (exist[i][j] == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				while (--j >= 0)
					free(exist[i][j]);
				while (--i >= 0) {
					for (j = 0; nsection[i]; j++)
						free(exist[i][j]);
					free(exist[i]);
				}
				goto free_meta_data;
			}
			memset(exist[i][j], 0, size);
		}
	}

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL)
		goto free_exist;
	for (i = 0; i < nhosts; i++) {
		rename_spool_node_dir(hosts[i].hostname,
				      from_canonical_path,
				      to_canonical_path,
				      nfile, nsection, ncopy, copies, exist);
	}

	/*
	 * check existence of section copy in spool node
	 */
	if (!check_existence_spool_file(nfile,
				       nsection, ncopy, exist)) {
		/* undo rename spool directory */
		for (i = 0; i < nhosts; i++) {
			rename_spool_node_dir(hosts[i].hostname,
					      to_canonical_path,
					      from_canonical_path,
					      nfile, nsection, ncopy,
					      copies, exist);
		}
		goto free_hosts;
	}
	e = set_meta_data(ndir, nfile, from_canonical_path, to_canonical_path,
			  dir_path_infos, file_path_infos,
			  nsection, sections, ncopy, copies, exist);
	if (e != NULL) {
		for (i = 0; i < nhosts; i++) {
			rename_spool_node_dir(hosts[i].hostname,
					      to_canonical_path,
					      from_canonical_path,
					      nfile, nsection, ncopy,
					      copies, exist);
		}
		goto free_hosts;
	}
	for (i = 0; i < nfile; i++) {
		e = remove_infos_all(file_path_infos[i].pathname);
	}
	i = ndir;
	while (--i >= 0) {
		e = gfarm_path_info_remove(dir_path_infos[i].pathname);
	}

free_hosts:
	gfarm_host_info_free_all(nhosts, hosts);
free_exist:
	for (i = 0; i < nfile; i++) {
		for (j = 0; j < nsection[i]; j++) {
			free(exist[i][j]);
		}
		free(exist[i]);
	}
free_meta_data:
	free_meta_data(ndir, dir_path_infos, nfile, file_path_infos,
		       nsection, sections, ncopy, copies);
free_copies:
	free(copies);
free_ncopy:
	free(ncopy);
free_sections:
	free(sections);
free_nsection:
	free(nsection);
free_file_path_infos:
	free(file_path_infos);
free_dir_path_infos:
	free(dir_path_infos);
free_file_list:
	gfarm_stringlist_free(&file_list);
free_dir_list:
	gfarm_stringlist_free(&dir_list);
	return (e);
}

char *
gfs_rename(const char *from_url, const char *to_url)
{
	char *e, *from_canonical_path, *to_canonical_path;
	struct gfarm_path_info from_pi, to_pi;

	e = gfarm_url_make_path(from_url, &from_canonical_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_info_get(from_canonical_path, &from_pi);
	if (e != NULL)
		goto free_from_canonical_path;
	e = gfarm_url_make_path_for_creation(to_url, &to_canonical_path);
	if (e != NULL)
		goto free_from_pi;
	if (strcmp(from_canonical_path, to_canonical_path) == 0)
		goto free_to_canonical_path;
	e = gfarm_path_info_get(to_canonical_path, &to_pi);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		goto free_to_canonical_path;
	}

	if (GFARM_S_ISREG(from_pi.status.st_mode)) {
		if (e == NULL) {
			if (GFARM_S_ISDIR(to_pi.status.st_mode)) {
				e = GFARM_ERR_IS_A_DIRECTORY;
				gfarm_path_info_free(&to_pi);
				goto free_to_canonical_path;
			}
			gfarm_path_info_free(&to_pi);
			e = gfs_unlink(to_url);
			/* FT - allows no physical file case */
			if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT)
				goto free_to_canonical_path;
		}
		e = rename_single_file(&from_pi, to_canonical_path);
	} else if (GFARM_S_ISDIR(from_pi.status.st_mode)) {
		if (e == NULL) {
			if (!GFARM_S_ISDIR(to_pi.status.st_mode)) {
				e = GFARM_ERR_NOT_A_DIRECTORY;
				gfarm_path_info_free(&to_pi);
				goto free_to_canonical_path;
			}
			gfarm_path_info_free(&to_pi);
			if (strstr(to_canonical_path, from_canonical_path) ==
				to_canonical_path &&
			    to_canonical_path[strlen(from_canonical_path)] ==
									'/') {
				e = GFARM_ERR_INVALID_ARGUMENT;
				goto free_to_canonical_path;
			}
			e = gfs_rmdir(to_url);
			if (e != NULL)
				goto free_to_canonical_path;
		} else { /* to_canonical_path does not exist */
			if (strstr(to_canonical_path, from_canonical_path) ==
				to_canonical_path &&
			    to_canonical_path[strlen(from_canonical_path)] ==
									'/') {
				e = GFARM_ERR_INVALID_ARGUMENT;
				goto free_to_canonical_path;
			}
		}
		e = rename_dir(from_url, to_url,
			       from_canonical_path, to_canonical_path);
	} else {
		if (e == NULL)
			gfarm_path_info_free(&to_pi);
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	}

free_to_canonical_path:
	free(to_canonical_path);
free_from_pi:
	gfarm_path_info_free(&from_pi);
free_from_canonical_path:
	free(from_canonical_path);
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

#ifdef __GNUC__ /* workaround gcc warning: 'digest_type' may be used uninitialized */
	digest_type = NULL;
#endif
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
	}
	else if (e == NULL) {
		if (gfs_check_section_checksum_unknown_by_finfo(&fi)) {
			struct gfarm_file_section_info fi1;

			fi1.filesize = filesize;
			fi1.checksum_type = GFS_DEFAULT_DIGEST_NAME;
			fi1.checksum = digest_value_string;

			e = gfarm_file_section_info_replace(
				gfarm_file, section, &fi1);
		}
		else {
			if (fi.filesize != filesize)
				e = "file size mismatch";
			if (strcasecmp(fi.checksum_type, digest_type) != 0)
				e = "checksum type mismatch";
			if (strcasecmp(fi.checksum, digest_value_string) != 0)
				e = "check sum mismatch";
		}
		gfarm_file_section_info_free(&fi);
	}
	if (e != NULL)
		return (e);

	e = gfarm_host_get_canonical_self_name(&fci.hostname);
	if (e == NULL) {
		e = gfarm_file_section_copy_info_set(
			gfarm_file, section, fci.hostname, &fci);
	}
	return (e);
}

static int
gfarm_file_missing_replica(char *gfarm_file, char *section,
	char *canonical_hostname)
{
	char *e, *path_section;
	struct sockaddr peer_addr;
	struct gfs_connection *peer_conn;
	int peer_fd, missing;

	e = gfarm_host_address_get(canonical_hostname, gfarm_spool_server_port,
	    &peer_addr, NULL);
	if (e != NULL)
		return (0);

	e = gfs_client_connection(canonical_hostname, &peer_addr, &peer_conn);
	if (e != NULL)
		return (0);

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		return (0);

	e = gfs_client_open(peer_conn, path_section, GFARM_FILE_RDONLY, 0,
	    &peer_fd);
	missing = e == GFARM_ERR_NO_SUCH_OBJECT;
	if (e == NULL)
		gfs_client_close(peer_conn, peer_fd);

	free(path_section);
	return (missing);
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
 * NOTE: gfarm_file_section_replicate_without_busy_check() assumes
 *	that the caller of this function already checked the section
 *	by gfs_check_section_busy(gfarm_file, section)
 */
static char *
gfarm_file_section_replicate_without_busy_check(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dst_canonical_hostname)
{
	char *e, *path_section;
	struct gfarm_file_section_copy_info ci;
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;

	if (gfarm_replication_method != GFARM_REPLICATION_BOOTSTRAP_METHOD)
		return (gfarm_file_section_replicate_from_to_by_gfrepbe(
		    gfarm_file, section,
		    src_canonical_hostname, dst_canonical_hostname));

	e = gfarm_path_section(gfarm_file, section, &path_section);
	if (e != NULL)
		return (e);

	e = gfarm_host_address_get(dst_canonical_hostname,
	    gfarm_spool_server_port, &peer_addr, NULL);
	if (e != NULL)
		goto finish;

	e = gfs_client_connect(dst_canonical_hostname, &peer_addr,
	    &gfs_server);
	if (e != NULL)
		goto finish;
	e = gfs_client_bootstrap_replicate_file(gfs_server,
	    path_section, mode, file_size,
	    src_canonical_hostname, src_if_hostname);
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		/* FT - the parent directory may be missing */
		if (gfarm_file_missing_replica(gfarm_file, section,
		    src_canonical_hostname)) {
			/* Delete the section copy info */
			(void)gfarm_file_section_copy_info_remove(gfarm_file,
			    section, src_canonical_hostname);
			e = GFARM_ERR_INCONSISTENT_RECOVERABLE;
			goto disconnect;
		}
		/* FT - the parent directory of the destination may be missing */
		(void)gfs_pio_remote_mkdir_parent_canonical_path(
			gfs_server, gfarm_file);
		e = gfs_client_bootstrap_replicate_file(
			gfs_server, path_section, mode, file_size,
			src_canonical_hostname, src_if_hostname);
	}
	if (e == NULL)
		e = gfarm_file_section_copy_info_set(gfarm_file, section,
		    dst_canonical_hostname, &ci);

disconnect:
	gfs_client_disconnect(gfs_server);

finish:
	free(path_section);
	return (e);
}

static char *
gfarm_file_section_replicate(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dst_canonical_hostname)
{
	char *e = gfs_check_section_busy(gfarm_file, section);

	if (e != NULL)
		return (e);
	return (gfarm_file_section_replicate_without_busy_check(
	    gfarm_file, section, mode, file_size,
	    src_canonical_hostname, src_if_hostname,
	    dst_canonical_hostname));
}

static char *
gfarm_file_section_replicate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *dst_canonical_hostname;

	e = gfarm_host_get_canonical_name(dsthost, &dst_canonical_hostname);
	if (e != NULL)
		return (e);
	/* already exists? don't have to replicate in that case */
	if (!gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    dst_canonical_hostname)) {
		e = gfarm_file_section_replicate(
		    gfarm_file, section, mode, file_size,
		    src_canonical_hostname, src_if_hostname,
		    dst_canonical_hostname);
	}
	free(dst_canonical_hostname);
	return (e);
}

static char *
gfarm_file_section_migrate_from_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *src_canonical_hostname, char *src_if_hostname,
	char *dsthost)
{
	char *e, *dst_canonical_hostname;

	e = gfarm_host_get_canonical_name(dsthost, &dst_canonical_hostname);
	if (e != NULL)
		return (e);
	/* already exists? don't have to replicate in that case */
	if (!gfarm_file_section_copy_info_does_exist(gfarm_file, section,
	    dst_canonical_hostname)) {
		e = gfarm_file_section_replicate(
		    gfarm_file, section, mode, file_size,
		    src_canonical_hostname, src_if_hostname,
		    dst_canonical_hostname);
		if (e == NULL) {
			e = gfs_unlink_replica_internal(
			    gfarm_file, section, src_canonical_hostname);
		}
	}
	free(dst_canonical_hostname);
	return (e);
}

static char *
gfarm_file_section_transfer_to_internal(char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *srchost, *if_hostname;
	struct sockaddr peer_addr;

	do {
		e = gfarm_file_section_host_schedule(gfarm_file, section,
		    &srchost);
		if (e != NULL)
			break;

		/* reflect "address_use" directive in the `srchost' */
		e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
		    &peer_addr, &if_hostname);
		if (e != NULL) {
			free(srchost);
			break; /* XXX should try next candidate */
		}

		e = (*transfer_from_to_internal)(gfarm_file, section,
		    mode & GFARM_S_ALLPERM, file_size,
		    srchost, if_hostname, dsthost);

		free(if_hostname);
		free(srchost);

	} while (e == GFARM_ERR_INCONSISTENT_RECOVERABLE);
	return (e);
}

static char *
gfarm_file_section_replicate_to_internal(
	char *gfarm_file, char *section,
	gfarm_mode_t mode, file_offset_t file_size,
	char *dsthost)
{
	return (gfarm_file_section_transfer_to_internal(gfarm_file, section,
	    mode, file_size, dsthost,
	    gfarm_file_section_replicate_from_to_internal));
}

/*
 * XXX FIXME
 * if the owner of a file is not the same, permit a group/other write access.
 *	- This should be fixed in the release of gfarm version 2.
 */
char *
gfarm_fabricate_mode_for_replication(struct gfs_stat *gst, gfarm_mode_t *modep)
{
	char *e;

	if (strcmp(gst->st_user, gfarm_get_global_username()) == 0) {
		*modep = gst->st_mode & GFARM_S_ALLPERM;
	} else {
		e = gfs_stat_access(gst, R_OK);
		if (e != NULL)
			return (e);
		/* don't allow setuid/setgid */
		*modep = (gst->st_mode | 022) & 0777;
	}
	return (NULL);
}

static char *
gfarm_url_section_transfer_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file, *canonical_hostname, *if_hostname;
	struct sockaddr peer_addr;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode;

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
		goto finish_section_info;

	/* reflect "address_use" directive in the `srchost' */
	e = gfarm_host_address_get(srchost, gfarm_spool_server_port,
	    &peer_addr, &if_hostname);
	if (e != NULL)
		goto finish_canonical_hostname;
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	if (e != NULL)
		goto finish_if_hostname;
	e = (*transfer_from_to_internal)(gfarm_file, section,
	    mode, si.filesize,
	    canonical_hostname, if_hostname, dsthost);
finish_if_hostname:
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
gfarm_url_section_replicate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	return (gfarm_url_section_transfer_from_to(gfarm_url, section,
	    srchost, dsthost, gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_section_migrate_from_to(const char *gfarm_url, char *section,
	char *srchost, char *dsthost)
{
	return (gfarm_url_section_transfer_from_to(gfarm_url, section,
	    srchost, dsthost, gfarm_file_section_migrate_from_to_internal));
}

static char *
gfarm_url_section_transfer_to(
	const char *gfarm_url, char *section, char *dsthost,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file;
	struct gfarm_path_info pi;
	struct gfarm_file_section_info si;
	gfarm_mode_t mode;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		goto finish;
	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;
	e = gfarm_file_section_info_get(gfarm_file, section, &si);
	if (e != NULL)
		goto finish_path_info;
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	if (e != NULL)
		goto finish_section_info;
	e = gfarm_file_section_transfer_to_internal(gfarm_file, section,
	    mode, si.filesize,
	    dsthost, transfer_from_to_internal);
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
	return (gfarm_url_section_transfer_to(gfarm_url, section, dsthost,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_section_migrate_to(
	const char *gfarm_url, char *section, char *dsthost)
{
	return (gfarm_url_section_transfer_to(gfarm_url, section, dsthost,
	    gfarm_file_section_migrate_from_to_internal));
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
		e = gfarm_schedule_search_idle_hosts_to_write(
		    nhosts, hostnames, nreplicas, hostnames);
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
			e = gfarm_schedule_search_idle_hosts_to_write(
			    nhosts - 1, hostnames + 1,
			    nreplicas - 1, hostnames + 1);
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
	e = gfs_pio_create(gfarm_url, GFARM_FILE_WRONLY|GFARM_FILE_TRUNC,
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
	gfarm_mode_t mode;
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
	if (!GFARM_S_IS_PROGRAM(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		free(gfarm_file);
		return ("gfarm_url_program_deliver(): not a program");
	}
	/*
	 * XXX FIXME
	 * This may be called with GFARM_REPLICATION_BOOTSTRAP_METHOD
	 * to deliver gfrepbe_client/gfrepbe_server.
	 */
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	gfarm_path_info_free(&pi);
	if (e != NULL) {
		free(gfarm_file);
		return (e);
	}
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
		    mode, si.filesize, hosts[i]);
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

static char *
gfarm_url_fragments_transfer(
	const char *gfarm_url, int ndsthosts, char **dsthosts,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, *gfarm_file, **srchosts, **edsthosts;
	int nsrchosts;
	gfarm_mode_t mode;
	int i, pid, *pids;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(gfarm_url, &gfarm_file);
	if (e != NULL)
		return (e);

	e = gfarm_path_info_get(gfarm_file, &pi);
	if (e != NULL)
		goto finish_gfarm_file;

	if (!GFARM_S_IS_FRAGMENTED_FILE(pi.status.st_mode)) {
		gfarm_path_info_free(&pi);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto finish_gfarm_file;
	}
	e = gfarm_fabricate_mode_for_replication(&pi.status, &mode);
	gfarm_path_info_free(&pi);
	if (e != NULL)
		goto finish_gfarm_file;
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

		e = (*transfer_from_to_internal)(gfarm_file, section_string,
		    mode, si.filesize,
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
gfarm_url_fragments_replicate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	return (gfarm_url_fragments_transfer(gfarm_url, ndsthosts, dsthosts,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_fragments_migrate(
	const char *gfarm_url, int ndsthosts, char **dsthosts)
{
	return (gfarm_url_fragments_transfer(gfarm_url, ndsthosts, dsthosts,
	    gfarm_file_section_migrate_from_to_internal));
}

char *
gfarm_url_fragments_transfer_to_domainname(
	const char *gfarm_url, const char *domainname,
	char *(*transfer_from_to_internal)(char *, char *,
	    gfarm_mode_t, file_offset_t, char *, char *, char *))
{
	char *e, **dsthosts;
	int nfrags;

	e = gfarm_url_fragment_number(gfarm_url, &nfrags);
	if (e != NULL)
		return (e);

	dsthosts = malloc(nfrags * sizeof(char *));
	if (dsthosts == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = gfarm_schedule_search_idle_by_domainname_to_write(domainname,
		nfrags, dsthosts);
	if (e != NULL)
		goto free_dsthosts;

	e = gfarm_url_fragments_transfer(gfarm_url, nfrags, dsthosts,
	    transfer_from_to_internal);

	while (--nfrags >= 0)
		free(dsthosts[nfrags]);
 free_dsthosts:
	free(dsthosts);

	return (e);
}

char *
gfarm_url_fragments_replicate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	return (gfarm_url_fragments_transfer_to_domainname(
	    gfarm_url, domainname,
	    gfarm_file_section_replicate_from_to_internal));
}

char *
gfarm_url_fragments_migrate_to_domainname(
	const char *gfarm_url, const char *domainname)
{
	return (gfarm_url_fragments_transfer_to_domainname(
	    gfarm_url, domainname,
	    gfarm_file_section_migrate_from_to_internal));
}

/*
 * internal functions which are used from gfs_pio_section.c and gfs_exec.c
 */

static char *
lock_local_file_section(struct gfarm_file_section_info *sinfo,
	char *canonical_self_hostname,
	char **localpathp, int *replication_neededp)
{
	char *e, *localpath;
	struct stat st;
	int metadata_exist, localfile_exist;

	e = gfs_check_section_busy_by_finfo(sinfo);
	if (e != NULL)
		return (e);

	e = gfarm_path_localize_file_section(sinfo->pathname, sinfo->section,
	    &localpath);
	if (e != NULL)
		return (e);

	/* critical section starts */
	gfs_lock_local_path_section(localpath);

	/* FT - check existence of the local file and its metadata */
	metadata_exist = gfarm_file_section_copy_info_does_exist(
	    sinfo->pathname, sinfo->section, canonical_self_hostname);
	localfile_exist = stat(localpath, &st) == 0 && S_ISREG(st.st_mode);

	if (metadata_exist &&
	    localfile_exist && st.st_size == sinfo->filesize) {
		/* already exist */
		/* XXX - need integrity check by checksum */
		*replication_neededp = 0;
	} else {
		if (localfile_exist) /* FT - unknown local file.  delete it */
			unlink(localpath);
		if (metadata_exist)  /* FT - delete dangling metadata */
			gfarm_file_section_copy_info_remove(
			    sinfo->pathname, sinfo->section,
			    canonical_self_hostname);
		*replication_neededp = 1;
	}
	*localpathp = localpath;
	return (NULL);
}

char *
gfarm_file_section_replicate_from_to_local_with_locking(
	struct gfarm_file_section_info *sinfo, gfarm_mode_t mode,
	char *src_canonical_hostname, char *src_if_hostname,
	char **localpathp)
{
	char *e, *canonical_self_hostname, *localpath;
	int replication_needed;

	e = gfarm_host_get_canonical_self_name(&canonical_self_hostname);
	if (e != NULL)
		return (e);

	/* critical section starts */
	e = lock_local_file_section(sinfo, canonical_self_hostname,
	    &localpath, &replication_needed);
	if (e != NULL)
		return (e);

	if (replication_needed)
		e = gfarm_file_section_replicate_without_busy_check(
		    sinfo->pathname, sinfo->section, mode, sinfo->filesize,
		    src_canonical_hostname, src_if_hostname,
		    canonical_self_hostname);

	gfs_unlock_local_path_section(localpath);
	/* critical section ends */

	if (e == NULL && localpathp != NULL)
		*localpathp = localpath;
	else
		free(localpath);
	return (e);
}

char *
gfarm_file_section_replicate_to_local_with_locking(
	struct gfarm_file_section_info *sinfo, gfarm_mode_t mode,
	char **localpathp)
{
	char *e, *canonical_self_hostname, *localpath;
	int replication_needed;

	e = gfarm_host_get_canonical_self_name(&canonical_self_hostname);
	if (e != NULL)
		return (e);

	/* critical section starts */
	e = lock_local_file_section(sinfo, canonical_self_hostname,
	    &localpath, &replication_needed);
	if (e != NULL)
		return (e);

	if (replication_needed)
		e = gfarm_file_section_transfer_to_internal(
		    sinfo->pathname, sinfo->section, mode, sinfo->filesize,
		    canonical_self_hostname,
		    gfarm_file_section_replicate_without_busy_check);

	gfs_unlock_local_path_section(localpath);
	/* critical section ends */

	if (e == NULL && localpathp != NULL)
		*localpathp = localpath;
	else
		free(localpath);
	return (e);
}

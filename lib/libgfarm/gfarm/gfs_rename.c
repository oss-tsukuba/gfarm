/*
 * $Id$
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "gfs_client.h"
#include "gfs_misc.h"

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
add_cwd_to_relative_path(char *cwd, const char *path)
{
	char *p;

	GFARM_MALLOC_ARRAY(p, strlen(cwd) + strlen(path) + 2);
	if (p == NULL)
		return (NULL);
	sprintf(p, "%s/%s", cwd, path);
	return (p);
}

static char *
traverse_file_tree(char *cwd, char *path,
		   gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char *e, *e_save = NULL;
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
			goto free_dpath;
		e = gfs_chdir(path);
		if (e != NULL)
			goto free_dpath;
		e = gfs_opendir(".", &dir);
		if (e != NULL)
			goto pop_dir;
		while ((e = gfs_readdir(dir, &entry)) == NULL &&
				entry != NULL) {
			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			e = traverse_file_tree(dpath, entry->d_name,
					       dir_list, file_list);
			if (e != NULL)
				goto close_dir;
		}
 close_dir:
		gfs_closedir(dir);
 pop_dir:
		e_save = gfs_chdir("..");
	}
	if (e == NULL)
		e = e_save;
 free_dpath:
	if (e != NULL)
		free(dpath);
	return (e);
}

static void
rename_spool_node_dir(char *hostname,
		      char *from, char *to,
		      int nfile, int *nsection, int **ncopy,
		      struct gfarm_file_section_copy_info ***copies,
		      unsigned char ***exist)
{
	char *e, *e2;
	struct gfs_connection *gfs_server;
	int i, j, k;

	e = gfs_client_rename_with_reconnection(hostname, from, to,
	    &gfs_server, &e2);
	if (e != NULL) {
		gflog_warning(
			"rename_spool_node_dir: gfs_client_connection: %s: %s",
			hostname, e);
		return;
	}
	if (e2 == GFARM_ERR_NO_SUCH_OBJECT &&
	    gfs_client_exist(gfs_server, from) == NULL) {
		if (gfs_client_mk_parent_dir(gfs_server, to) == NULL)
			e2 = gfs_client_rename(gfs_server, from, to);
	}
	gfs_client_connection_free(gfs_server);
	if (e2 != NULL && e2 != GFARM_ERR_NO_SUCH_OBJECT) {
		gflog_warning(
			"rename_spool_node_dir: gfs_client_rename(%s, %s): %s",
			from, to, e2);
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
}

static char *
get_lists(const char *from_url,
		       gfarm_stringlist *dir_list, gfarm_stringlist *file_list)
{
	char *e, *e_save, *s, cwdbf[PATH_MAX * 2];
	GFS_Dir dir;
	struct gfs_dirent *entry;

	e = gfs_getcwd(cwdbf, sizeof(cwdbf));
	if (e != NULL)
		return (e);
	e = gfs_chdir(from_url);
	if (e != NULL)
		return (e);
	e = gfs_opendir(".", &dir);
	if (e != NULL) {
		gfs_chdir(cwdbf);
		return (e);
	}
	while ((e = gfs_readdir(dir, &entry)) == NULL && entry != NULL) {
		if (strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (strcmp(entry->d_name, ".") == 0) {
			s = strdup("");
			if (s == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				break;
			}
			gfarm_stringlist_add(dir_list, s);
			continue;
		}
		e = traverse_file_tree("", entry->d_name, dir_list, file_list);
		if (e != NULL)
			break;
	}
	gfs_closedir(dir);
	e_save = gfs_chdir(cwdbf);
	return (e != NULL ? e : e_save);
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
	GFARM_MALLOC_ARRAY(*ncopy, *nsection);
	if (*ncopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto finish_free_section_info;
	}
	GFARM_MALLOC_ARRAY(*copies, *nsection);
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
	free(copies);
	free(ncopy);
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
		GFARM_MALLOC_ARRAY(p,
			strlen(from_canonic_path) + strlen(elem) + 1);
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
	free(from_canonic_path);
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
	char *e, *p;
	struct gfarm_path_info to_pi;
	struct timeval now;

	GFARM_MALLOC_ARRAY(p, strlen(from_path_info->pathname) -
			 strlen(from_dir_canonical_path) +
			 strlen(to_dir_canonical_path) + 1);
	if (p == NULL) {
		return (GFARM_ERR_NO_MEMORY);
	}
	sprintf(p, "%s%s", to_dir_canonical_path,
	    from_path_info->pathname + strlen(from_dir_canonical_path));
	to_pi = *from_path_info;
	to_pi.pathname = p;
	gettimeofday(&now, NULL);
	to_pi.status.st_ctimespec.tv_sec = now.tv_sec;
	to_pi.status.st_ctimespec.tv_nsec = now.tv_usec * 1000;

	e = gfarm_path_info_set(to_pi.pathname, &to_pi);
	if (e != NULL)
		free(p);
	else
		*to_path = p;

	return (e);
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
		free(to_path);
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
			if (e != NULL) {
				free(to_path);
				return (e);
			}
			for (k = 0; k < ncopy[i][j]; k++) {
				struct gfarm_file_section_copy_info to_ci;

				if (exist[i][j][k] == 0)
					continue;
				to_ci = copies[i][j][k];
				to_ci.pathname = to_path;
				e = gfarm_file_section_copy_info_set(
					to_ci.pathname, to_ci.section,
					to_ci.hostname,  &to_ci);
				if (e != NULL) {
					free(to_path);
					return (e);
				}
			}
		}
		free(to_path);
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
rename_dir(const char *from_url,
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

	GFARM_MALLOC_ARRAY(dir_path_infos, ndir);
	if (dir_path_infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_file_list;
	}
	GFARM_MALLOC_ARRAY(file_path_infos, nfile);
	if (file_path_infos == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_dir_path_infos;
	}
	GFARM_MALLOC_ARRAY(nsection, nfile);
	if (nsection == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_file_path_infos;
	}
	GFARM_MALLOC_ARRAY(sections, nfile);
	if (sections == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_nsection;
	}
	GFARM_MALLOC_ARRAY(ncopy, nfile);
	if (ncopy == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_sections;
	}
	GFARM_MALLOC_ARRAY(copies, nfile);
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
	GFARM_MALLOC_ARRAY(exist, nfile);
	if (exist == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto free_meta_data;
	}
	for (i = 0; i < nfile; i++) {
		GFARM_MALLOC_ARRAY(exist[i], nsection[i]);
		if (exist[i] == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			while (--i >= 0)
				free(exist[i]);
			goto free_exist;
		}
		for (j = 0; j < nsection[i]; j++) {
			GFARM_MALLOC_ARRAY(exist[i][j], ncopy[i][j]);
			if (exist[i][j] == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				while (--j >= 0)
					free(exist[i][j]);
				while (--i >= 0) {
					for (j = 0; j < nsection[i]; j++)
						free(exist[i][j]);
					free(exist[i]);
				}
				goto free_exist;
			}
			memset(exist[i][j], 0, ncopy[i][j] * sizeof(***exist));
		}
	}

	e = gfarm_host_info_get_all(&nhosts, &hosts);
	if (e != NULL)
		goto free_exist_sections;
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
		e = GFARM_ERR_INPUT_OUTPUT;
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
free_exist_sections:
	for (i = 0; i < nfile; i++) {
		for (j = 0; j < nsection[i]; j++)
			free(exist[i][j]);
		free(exist[i]);
	}
free_exist:
	free(exist);
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
	gfarm_stringlist_free_deeply(&file_list);
free_dir_list:
	gfarm_stringlist_free_deeply(&dir_list);
	return (e);
}

static char *
gfs_rename_dir_internal(const char *from_url, const char *to_url,
	const struct gfarm_path_info *from_pi, char *to_canonical_path)
{
	char *e;
	struct gfarm_path_info to_pi;

	assert(GFARM_S_ISDIR(from_pi->status.st_mode));

	e = gfarm_path_info_get(to_canonical_path, &to_pi);
	if (e != NULL && e != GFARM_ERR_NO_SUCH_OBJECT) {
		return (e);
	}

	if (e == NULL) {
		if (!GFARM_S_ISDIR(to_pi.status.st_mode)) {
			gfarm_path_info_free(&to_pi);
			return (GFARM_ERR_NOT_A_DIRECTORY);
		}
		gfarm_path_info_free(&to_pi);
		if (strstr(to_canonical_path, from_pi->pathname) ==
			to_canonical_path &&
		    to_canonical_path[strlen(from_pi->pathname)] == '/') {
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		e = gfs_rmdir(to_url);
		if (e != NULL)
			return (e);
	} else { /* to_canonical_path does not exist */
		if (strstr(to_canonical_path, from_pi->pathname) ==
			to_canonical_path &&
		    to_canonical_path[strlen(from_pi->pathname)] == '/') {
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
	}
	e = rename_dir(from_url, from_pi->pathname, to_canonical_path);

	return (e);
}

struct gfs_rename_args {
	struct gfarm_path_info *pi;
	char *path, *n_path;
};

static char *
rename_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	char *path = info->pathname, *section = info->section;
	char *host = info->hostname;
	struct gfs_rename_args *a = arg;
	char *new_path, *old_path, *e, *e2;

	e = gfarm_path_section(path, section, &old_path);
	if (e != NULL)
		return (e);
	e = gfarm_path_section(a->n_path, section, &new_path);
	if (e == NULL) {
		e = gfs_client_link_faulttolerant(host, old_path, new_path,
		    NULL, &e2);
		if (e == NULL)
			e = e2;
		free(new_path);
	}
	free(old_path);

	return (e != NULL ? e : gfarm_file_section_copy_info_set(
			a->n_path, section, host, NULL));
}

static char *
rename_section(struct gfarm_file_section_info *info, void *arg)
{
	struct gfs_rename_args *a = arg;
	int nsuccess;
	char *e;

	/* add new section info */
	e = gfarm_file_section_info_set(a->n_path, info->section, info);
	if (e != NULL)
		return (e);

	e = gfarm_foreach_copy(
		rename_copy, info->pathname, info->section, arg, &nsuccess);
	if (nsuccess > 0)
		e = NULL;
	return (e);
}

static char *
gfs_rename_file_internal(struct gfarm_path_info *pi, char *newpath)
{
	struct gfs_rename_args a;
	struct gfarm_path_info n_pi;
	struct timeval now;
	char *e;

	a.pi = pi;
	a.path = pi->pathname;
	a.n_path = newpath;

	e = gfarm_path_info_get(a.n_path, &n_pi);
	if (e == NULL) {
		if (GFARM_S_ISDIR(n_pi.status.st_mode))
			e = GFARM_ERR_IS_A_DIRECTORY;
		else
			/* XXX - should keep the file in case of failure */
			e = gfs_unlink_internal(a.n_path);
		gfarm_path_info_free(&n_pi);
		if (e != NULL)
			return (e);
	}

	/* add new path info */
	gettimeofday(&now, NULL);
	a.pi->status.st_ctimespec.tv_sec = now.tv_sec;
	a.pi->status.st_ctimespec.tv_nsec = now.tv_usec * 1000;
	e = gfarm_path_info_set(a.n_path, a.pi);
	/* XXX - should continue */
	if (e != NULL)
		return (e);

	e = gfarm_foreach_section(rename_section, a.path, &a, NULL);
	if (e == NULL)
		gfs_unlink_internal(a.path);
	else
		gfs_unlink_internal(a.n_path);
		
	return (e);
}

char *
gfs_rename(const char *path, const char *newpath)
{
	char *c_path, *e;
	struct gfarm_path_info pi;
	gfarm_mode_t mode;

	e = gfarm_url_make_path(path, &c_path);
	if (e == NULL) {
		e = gfarm_path_info_get(c_path, &pi);
		if (e == NULL)
			e = gfs_unlink_check_perm(c_path);
		free(c_path);
	}
	if (e != NULL)
		return (e);

	e = gfarm_url_make_path_for_creation(newpath, &c_path);
	if (e != NULL)
		goto free_pi;
	e = gfs_unlink_check_perm(c_path);
	if (e != NULL)
		goto free_c_path;

	/* the same path */
	if (strcmp(pi.pathname, c_path) == 0)
		goto free_c_path;

	mode = pi.status.st_mode;
	if (GFARM_S_ISDIR(mode))
		e = gfs_rename_dir_internal(path, newpath, &pi, c_path);
	else if (!GFARM_S_ISREG(mode))
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	else
		e = gfs_rename_file_internal(&pi, c_path);
free_c_path:
	free(c_path);
free_pi:
	gfarm_path_info_free(&pi);
	return (e);
}

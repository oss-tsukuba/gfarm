/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "gfs_pio.h"
#include "gfs_client.h"

enum data_mode {
	NOT_CHANGED,
	TO_EXECUTABLE,
	TO_NONEXECUTABLE
};

#define IS_EXECUTABLE(mode)	((mode) & 0111 ? 1 : 0)

static enum data_mode
check_data_mode(gfarm_mode_t old, gfarm_mode_t new)
{
	int old_data_mode = IS_EXECUTABLE(old);
	int new_data_mode = IS_EXECUTABLE(new);

	switch (old_data_mode - new_data_mode) {
	case 1:
		return (TO_NONEXECUTABLE);
	case -1:
		return (TO_EXECUTABLE);
	default:
		return (NOT_CHANGED);
	}
}

static char *
get_new_section_name(gfarm_mode_t mode)
{
	char *arch;

	if (IS_EXECUTABLE(mode)) {
		/* if architecture of this node is known, use it */
		if (gfarm_host_get_self_architecture(&arch) == NULL)
			return (strdup(arch));

		return (strdup("noarch"));
	}
	else
		return (strdup("0"));
}

/* change mode of 'pi' */
static char *
gfs_chmod_path_info(struct gfarm_path_info *pi, gfarm_mode_t mode)
{
	gfarm_mode_t o_mode = pi->status.st_mode;

	pi->status.st_mode &= ~GFARM_S_ALLPERM;
	pi->status.st_mode |= (mode & GFARM_S_ALLPERM);

	if (GFARM_S_ISREG(o_mode)) {
		switch (check_data_mode(o_mode, mode)) {
		case TO_EXECUTABLE:
			pi->status.st_nsections = 0;
			break;
		case TO_NONEXECUTABLE:
			pi->status.st_nsections = 1;
			break;
		default:
			break;
		}
	}
	return (gfarm_path_info_replace(pi->pathname, pi));
}

struct gfs_chmod_args {
	char *path;
	gfarm_mode_t o_mode, mode;
	char *o_sec, *n_sec;
	int tol;
};

static char *
client_chmod(struct gfs_connection *gfs_server, void *args)
{
	struct gfs_chmod_args *a = args;
	char *e;

	e = gfs_client_chmod(gfs_server, a->path, a->mode);
	if (a->tol && e == GFARM_ERR_NO_SUCH_OBJECT)
		e = NULL;
	return (e);
}

static char *
gfs_chmod_dir(struct gfarm_path_info *pi, gfarm_mode_t mode)
{
	struct gfs_chmod_args a;
	int nhosts_succeed;
	char *e;

	e = gfs_chmod_path_info(pi, mode);
	if (e != NULL)
		return (e);

	/* try to change for all filesystem nodes */
	a.path = pi->pathname;
	a.mode = mode;
	a.tol = 1;
	return (gfs_client_apply_all_hosts(client_chmod,
			&a, "gfs_chmod", 1, &nhosts_succeed));
}

static char *
chmod_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;
	char *path = info->pathname, *section = info->section;
	char *host = info->hostname;
	struct gfs_chmod_args *a = arg;
	char *path_section, *e;

	e = gfarm_host_address_get(host, gfarm_spool_server_port,
		&peer_addr, NULL);
	if (e != NULL)
		return (e);

	e = gfs_client_connection(host, &peer_addr, &gfs_server);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(path, section, &path_section);
	if (e != NULL)
		return (e);

	e = gfs_client_chmod(gfs_server, path_section, a->mode);
	free(path_section);

	return (e);
}

static char *
chmod_section(struct gfarm_file_section_info *info, void *arg)
{
	int nsuccess;
	char *e;

	e = foreach_copy(
		chmod_copy, info->pathname, info->section, arg, &nsuccess);
	if (nsuccess > 0)
		e = NULL;
	return (e);
}

static char *
undo_chmod_section(struct gfarm_file_section_info *info, void *arg)
{
	struct gfs_chmod_args *a = arg;

	a->mode = a->o_mode;
	foreach_copy(chmod_copy, info->pathname, info->section, a, NULL);
	return (NULL);
}

static char *
change_section_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;
	char *path = info->pathname, *o_section = info->section;
	char *host = info->hostname;
	struct gfs_chmod_args *a = arg;
	char *new_path, *old_path, *e;

	e = gfarm_host_address_get(host, gfarm_spool_server_port,
		&peer_addr, NULL);
	if (e != NULL)
		return (e);

	e = gfs_client_connection(host, &peer_addr, &gfs_server);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(path, o_section, &old_path);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(path, a->n_sec, &new_path);
	if (e != NULL)
		goto free_old_path;

	e = gfs_client_link(gfs_server, old_path, new_path);
	if (e != NULL)
		goto free_new_path;

	e = gfs_client_chmod(gfs_server, new_path, a->mode);
	if (e != NULL)
		gfs_client_unlink(gfs_server, new_path);

free_new_path:
	free(new_path);
free_old_path:
	free(old_path);

	return (e != NULL ? e : gfarm_file_section_copy_info_set(
			path, a->n_sec, host, NULL));
}

char *
gfs_chmod_change_section_internal(
	struct gfs_chmod_args *a, int *nsuccess, char **n_sec)
{
	struct gfarm_file_section_info *sections;
	int nsection;
	char *e;

	*nsuccess = 0;

	e = gfarm_file_section_info_get_all_by_file(
		a->path, &nsection, &sections);
	if (e != NULL)
		return (e);

	if (nsection > 1) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		goto section_info_free_all;
	}

	/* add new file section */
	a->o_sec = sections[0].section;
	a->n_sec = get_new_section_name(a->mode);
	sections[0].section = a->n_sec;
	e = gfarm_file_section_info_set(a->path, a->n_sec, &sections[0]);
	if (e != NULL)
		goto free_o_sec;

	e = foreach_copy(change_section_copy, a->path, a->o_sec, a, nsuccess);
	if (*nsuccess == 0) {
		gfarm_file_section_info_remove(a->path, a->n_sec);
		goto free_o_sec;
	}
	gfs_unlink_section_internal(a->path, a->o_sec);

	if (n_sec != NULL)
		*n_sec = strdup(a->n_sec);

free_o_sec:
	free(a->o_sec);
section_info_free_all:
	gfarm_file_section_info_free_all(nsection, sections);
	return (e);
}

char *
gfs_chmod_internal(struct gfarm_path_info *pi, gfarm_mode_t mode, char **n_sec)
{
	struct gfs_chmod_args a;
	int nsuccess;
	char *e;

	if (n_sec != NULL)
		*n_sec = NULL;

	if (strcmp(pi->status.st_user, gfarm_get_global_username()) != 0)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);

	a.path = pi->pathname;
	a.mode = mode;
	a.o_mode = pi->status.st_mode;
	
	if ((a.o_mode & GFARM_S_ALLPERM) == (mode & GFARM_S_ALLPERM))
		return (NULL); /* same mode, nothing to do */

	if (GFARM_S_ISDIR(a.o_mode))
		return (gfs_chmod_dir(pi, mode));
	if (!GFARM_S_ISREG(a.o_mode))
		return (GFARM_ERR_OPERATION_NOT_SUPPORTED);

	/* regular file */
	if (check_data_mode(a.o_mode, mode) != NOT_CHANGED) {
		e = gfs_chmod_change_section_internal(&a, &nsuccess, n_sec);
		if (nsuccess > 0)
			e = NULL;
	}
	else
		e = foreach_section(
			chmod_section, a.path, &a, undo_chmod_section);

	if (e == NULL)
		e = gfs_chmod_path_info(pi, mode);

	return (e);
}

char *
gfs_chmod(const char *path, gfarm_mode_t mode)
{
	char *canonical_path, *e;
	struct gfarm_path_info pi;

	e = gfarm_url_make_path(path, &canonical_path);
	if (e == NULL) {
		e = gfarm_path_info_get(canonical_path, &pi);
		free(canonical_path);
	}
	if (e != NULL)
		return (e);

	e = gfs_chmod_internal(&pi, mode, NULL);
	gfarm_path_info_free(&pi);
	return (e);
}

char *
gfs_fchmod(GFS_File gf, gfarm_mode_t mode)
{
	return (*gf->ops->view_chmod)(gf, mode);
}

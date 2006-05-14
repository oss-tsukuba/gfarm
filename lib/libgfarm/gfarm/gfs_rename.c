/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <openssl/evp.h>
#include <gfarm/gfarm.h>
#include "config.h"
#include "gfs_pio.h"
#include "gfs_client.h"
#include "gfs_misc.h"

struct gfs_rename_args {
	struct gfarm_path_info *pi;
	char *path, *n_path;
};

static char *
rename_copy(struct gfarm_file_section_copy_info *info, void *arg)
{
	struct gfs_connection *gfs_server;
	struct sockaddr peer_addr;
	char *path = info->pathname, *section = info->section;
	char *host = info->hostname;
	struct gfs_rename_args *a = arg;
	char *new_path, *old_path, *e;

	e = gfarm_host_address_get(host, gfarm_spool_server_port,
		&peer_addr, NULL);
	if (e != NULL)
		return (e);

	e = gfs_client_connection(host, &peer_addr, &gfs_server);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(path, section, &old_path);
	if (e != NULL)
		return (e);

	e = gfarm_path_section(a->n_path, section, &new_path);
	if (e != NULL)
		goto free_old_path;

	e = gfs_client_link(gfs_server, old_path, new_path);
	/* FT */
	if (e == GFARM_ERR_NO_SUCH_OBJECT) {
		(void)gfs_pio_remote_mkdir_parent_canonical_path(
			gfs_server, a->n_path);
		e = gfs_client_link(gfs_server, old_path, new_path);
	}		

	free(new_path);
free_old_path:
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
		e = gfs_rename_old(path, newpath); /* XXX */
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

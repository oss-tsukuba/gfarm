#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include <gfarm/host_info.h>
#include <gfarm/group_info.h>
#include <gfarm/user_info.h>
#include <gfarm/gfs.h>

#include "metadb_common.h"

/**********************************************************************/

void
gfarm_base_generic_info_free_all(
	int n,
	void *vinfos,
	const struct gfarm_base_generic_info_ops *ops)
{
	int i;
	char *infos = vinfos;

	for (i = 0; i < n; i++) {
		ops->free(infos);
		infos += ops->info_size;
	}
	free(vinfos);
}

/**********************************************************************/

static void gfarm_base_host_info_clear(void *info);
static int gfarm_base_host_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_host_info_ops = {
	sizeof(struct gfarm_host_info),
	(void (*)(void *))gfarm_host_info_free,
	gfarm_base_host_info_clear,
	gfarm_base_host_info_validate,
};

void
gfarm_host_info_free(
	struct gfarm_host_info *info)
{
	int i;

	if (info->hostname != NULL)
		free(info->hostname);
	if (info->hostaliases != NULL) {
		for (i = 0; i < info->nhostaliases; i++)
			free(info->hostaliases[i]);
		free(info->hostaliases);
	}
	if (info->architecture != NULL)
		free(info->architecture);
}

static void
gfarm_base_host_info_clear(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	memset(info, 0, sizeof(*info));
#if 0
	info->ncpu = GFARM_HOST_INFO_NCPU_NOT_SET;
#else
	info->ncpu = 1; /* assume 1 CPU by default */
#endif
}

static int
gfarm_base_host_info_validate(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	/* info->hostaliases may be NULL */
	return (
	    info->hostname != NULL &&
	    info->architecture != NULL &&
	    info->ncpu != GFARM_HOST_INFO_NCPU_NOT_SET
	);
}

void
gfarm_host_info_free_all(
	int n,
	struct gfarm_host_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_host_info_ops);
}

/**********************************************************************/

static void gfarm_base_user_info_clear(void *info);
static int gfarm_base_user_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_user_info_ops = {
	sizeof(struct gfarm_user_info),
	(void (*)(void *))gfarm_user_info_free,
	gfarm_base_user_info_clear,
	gfarm_base_user_info_validate,
};

void
gfarm_user_info_free(struct gfarm_user_info *info)
{
	if (info->username != NULL)
		free(info->username);
	if (info->realname != NULL)
		free(info->realname);
	if (info->homedir != NULL)
		free(info->homedir);
	if (info->gsi_dn != NULL)
		free(info->gsi_dn);
}

static void
gfarm_base_user_info_clear(void *vinfo)
{
	struct gfarm_user_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_user_info_validate(void *vinfo)
{
	struct gfarm_user_info *info = vinfo;

	return (
	    info->username != NULL &&
	    info->realname != NULL &&
	    info->homedir != NULL &&
	    info->gsi_dn != NULL
	);
}

void
gfarm_user_info_free_all(
	int n,
	struct gfarm_user_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_user_info_ops);
}

/**********************************************************************/

static void gfarm_base_group_info_clear(void *info);
static int gfarm_base_group_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_group_info_ops = {
	sizeof(struct gfarm_group_info),
	(void (*)(void *))gfarm_group_info_free,
	gfarm_base_group_info_clear,
	gfarm_base_group_info_validate,
};

void
gfarm_group_info_free(struct gfarm_group_info *info)
{
	int i;

	if (info->groupname != NULL)
		free(info->groupname);
	if (info->usernames != NULL) {
		for (i = 0; i < info->nusers; i++)
			free(info->usernames[i]);
		free(info->usernames);
	}
}

static void
gfarm_base_group_info_clear(void *vinfo)
{
	struct gfarm_group_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_group_info_validate(void *vinfo)
{
	struct gfarm_group_info *info = vinfo;

	return (
	    info->groupname != NULL
	);
}

void
gfarm_group_info_free_all(
	int n,
	struct gfarm_group_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_group_info_ops);
}

/**********************************************************************/

void
gfarm_group_names_free(struct gfarm_group_names *info)
{
	int i;

	for (i = 0; i < info->ngroups; i++)
		free(info->groupnames[i]);
}

/**********************************************************************/

static void gfarm_base_gfs_stat_clear(void *info);
static int gfarm_base_gfs_stat_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_gfs_stat_ops = {
	sizeof(struct gfs_stat),
	(void (*)(void *))gfs_stat_free,
	gfarm_base_gfs_stat_clear,
	gfarm_base_gfs_stat_validate,
};

void
gfs_stat_free(struct gfs_stat *s)
{
	if (s->st_user != NULL)
		free(s->st_user);
	if (s->st_group != NULL)
		free(s->st_group);
}

static void
gfarm_base_gfs_stat_clear(void *vinfo)
{
	struct gfs_stat *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_gfs_stat_validate(void *vinfo)
{
	struct gfs_stat *info = vinfo;

	/* XXX - should check all fields are filled */
	return (
	    info->st_user != NULL &&
	    info->st_group != NULL
	);
}

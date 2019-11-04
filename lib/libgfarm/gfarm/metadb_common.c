#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>

#include <gfarm/host_info.h>
#include <gfarm/group_info.h>
#include <gfarm/user_info.h>
#include <gfarm/gfs.h>

#include "metadb_common.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "metadb_server.h"

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

/*
 * see the comment in server/gfmd/host.c:host_enter()
 * to see why this interface is necessary only for gfmd.
 */
void
gfarm_host_info_free_except_hostname(
	struct gfarm_host_info *info)
{
	int i;

	if (info->hostaliases != NULL) {
		for (i = 0; i < info->nhostaliases; i++)
			free(info->hostaliases[i]);
		free(info->hostaliases);
	}
	if (info->architecture != NULL)
		free(info->architecture);
}

void
gfarm_host_info_free(
	struct gfarm_host_info *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
	gfarm_host_info_free_except_hostname(info);
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

/* extra utility function to do deep copy */
gfarm_error_t
gfs_stat_copy(struct gfs_stat *d, const struct gfs_stat *s)
{
	char *user, *group;

	GFARM_MALLOC_ARRAY(user, strlen(s->st_user) + 1);
	GFARM_MALLOC_ARRAY(group, strlen(s->st_group) + 1);
	if (user == NULL || group == NULL) {
		if (user != NULL)
			free(user);
		if (group != NULL)
			free(group);
		gflog_debug(GFARM_MSG_1001022,
			"allocation of user or group failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
		return (GFARM_ERR_NO_MEMORY);
	}
	*d = *s;
	strcpy(user, s->st_user);
	strcpy(group, s->st_group);
	d->st_user = user;
	d->st_group = group;
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static void
xattr_info_free(void *vinfo)
{
	struct xattr_info *info = vinfo;

	free(info->attrname);
	free(info->attrvalue);
}

void
gfarm_base_xattr_info_free_array(int n, void *vinfo)
{
	int i;
	struct xattr_info *info = vinfo;

	for (i = 0; i < n; i++)
		xattr_info_free(&info[i]);

	free(info);
}

static void
xattr_info_clear(void *vinfo)
{
	struct xattr_info *info = vinfo;

	info->inum = 0;
	info->attrname = NULL;
	info->namelen = 0;
	info->attrvalue = NULL;
	info->attrsize = 0;
}

static int
xattr_info_validate(void *vinfo)
{
	return (1);
}

const struct gfarm_base_generic_info_ops gfarm_base_xattr_info_ops = {
	sizeof(struct xattr_info),
	xattr_info_free,
	xattr_info_clear,
	xattr_info_validate
};

/**********************************************************************/

static void gfarm_base_quota_info_clear(void *info);
static int gfarm_base_quota_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_quota_info_ops = {
	sizeof(struct gfarm_quota_info),
	(void (*)(void *))gfarm_quota_info_free,
	gfarm_base_quota_info_clear,
	gfarm_base_quota_info_validate,
};

static void
gfarm_base_quota_info_clear(void *vinfo)
{
	struct gfarm_quota_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_quota_info_validate(void *vinfo)
{
	struct gfarm_quota_info *info = vinfo;

	return (info->name != NULL);
}

void
gfarm_quota_info_free_all(
	int n,
	struct gfarm_quota_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_quota_info_ops);
}

/**********************************************************************/

void
gfarm_metadb_server_free(struct gfarm_metadb_server *info)
{
	free(info->name);
	free(info->clustername);
}

static void
gfarm_base_metadb_server_clear(void *vinfo)
{
	struct gfarm_metadb_server *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_metadb_server_validate(void *vinfo)
{
	struct gfarm_metadb_server *info = vinfo;

	return (
	    info->name != NULL &&
	    info->clustername != NULL
	);
}

const struct gfarm_base_generic_info_ops gfarm_base_metadb_server_ops = {
	sizeof(struct gfarm_metadb_server),
	(void (*)(void *))gfarm_metadb_server_free,
	gfarm_base_metadb_server_clear,
	gfarm_base_metadb_server_validate,
};

void
gfarm_metadb_server_free_all(
	int n,
	struct gfarm_metadb_server *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_metadb_server_ops);
}

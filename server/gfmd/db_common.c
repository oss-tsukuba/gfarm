/*
 * $Id$
 */

#include <pthread.h>	/* db_access.h currently needs this */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "quota_info.h"
#include "metadb_common.h"
#include "metadb_server.h"

#include "quota.h"
#include "db_access.h"
#include "db_ops.h"
#include "db_common.h"

/**********************************************************************/

static void
db_user_auth_arg_free_internal(void *vinfo)
{
	struct db_user_auth_arg *info = vinfo;
	db_user_auth_arg_free(info);
}

void
db_user_auth_arg_free(struct db_user_auth_arg *info)
{
	free(info->username);
	free(info->auth_id_type);
	free(info->auth_user_id);
}

static void
db_user_auth_arg_clear(void *vinfo)
{
	struct db_user_auth_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_user_auth_arg_validate(void *vinfo)
{
	struct db_user_auth_arg *info = vinfo;

	return (
	    info->username != NULL &&
	    info->auth_id_type != NULL &&
	    info->auth_user_id != NULL
	);
}

void
db_user_auth_callback_trampoline(void *closure, void *vinfo)
{
	struct db_user_auth_trampoline_closure *c = closure;
	(*c->callback)(c->closure, vinfo);
}

const struct gfarm_base_generic_info_ops db_base_user_auth_arg_ops = {
	sizeof(struct db_user_auth_arg),
	db_user_auth_arg_free_internal,
	db_user_auth_arg_clear,
	db_user_auth_arg_validate,
};

/**********************************************************************/

static void
db_inode_cksum_arg_free(void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;

	if (info->type != NULL)
		free(info->type);
	if (info->sum != NULL)
		free(info->sum);
}

static void
db_inode_cksum_arg_clear(void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_inode_cksum_arg_validate(void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;

	return (
	    info->type != NULL &&
	    info->sum != NULL
	);
}

void
db_inode_cksum_callback_trampoline(void *closure, void *vinfo)
{
	struct db_inode_cksum_trampoline_closure *c = closure;
	struct db_inode_cksum_arg *info = vinfo;

	(*c->callback)(c->closure,
	    info->inum, info->type, info->len, info->sum);
}

const struct gfarm_base_generic_info_ops db_base_inode_cksum_arg_ops = {
	sizeof(struct db_inode_cksum_arg),
	db_inode_cksum_arg_free,
	db_inode_cksum_arg_clear,
	db_inode_cksum_arg_validate,
};

/**********************************************************************/

static void
db_filecopy_arg_free(void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;

	if (info->hostname != NULL)
		free(info->hostname);
}

static void
db_filecopy_arg_clear(void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_filecopy_arg_validate(void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;

	return (
	    info->hostname != NULL
	);
}

void
db_filecopy_callback_trampoline(void *closure, void *vinfo)
{
	struct db_filecopy_trampoline_closure *c = closure;
	struct db_filecopy_arg *info = vinfo;

	(*c->callback)(c->closure, info->inum, info->hostname);
}

const struct gfarm_base_generic_info_ops db_base_filecopy_arg_ops = {
	sizeof(struct db_filecopy_arg),
	db_filecopy_arg_free,
	db_filecopy_arg_clear,
	db_filecopy_arg_validate,
};

/**********************************************************************/

static void
db_deadfilecopy_arg_free(void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;

	if (info->hostname != NULL)
		free(info->hostname);
}

static void
db_deadfilecopy_arg_clear(void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_deadfilecopy_arg_validate(void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;

	return (
	    info->hostname != NULL
	);
}

void
db_deadfilecopy_callback_trampoline(void *closure, void *vinfo)
{
	struct db_deadfilecopy_trampoline_closure *c = closure;
	struct db_deadfilecopy_arg *info = vinfo;

	(*c->callback)(c->closure, info->inum, info->igen, info->hostname);
}

const struct gfarm_base_generic_info_ops db_base_deadfilecopy_arg_ops = {
	sizeof(struct db_deadfilecopy_arg),
	db_deadfilecopy_arg_free,
	db_deadfilecopy_arg_clear,
	db_deadfilecopy_arg_validate,
};

/**********************************************************************/

static void
db_direntry_arg_free(void *vinfo)
{
	struct db_direntry_arg *info = vinfo;

	if (info->entry_name != NULL)
		free(info->entry_name);
}

static void
db_direntry_arg_clear(void *vinfo)
{
	struct db_direntry_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_direntry_arg_validate(void *vinfo)
{
	struct db_direntry_arg *info = vinfo;

	return (
	    info->entry_name != NULL
	);
}

void
db_direntry_callback_trampoline(void *closure, void *vinfo)
{
	struct db_direntry_trampoline_closure *c = closure;
	struct db_direntry_arg *info = vinfo;

	(*c->callback)(c->closure, info->dir_inum,
	    info->entry_name, info->entry_len, info->entry_inum);
}

const struct gfarm_base_generic_info_ops db_base_direntry_arg_ops = {
	sizeof(struct db_direntry_arg),
	db_direntry_arg_free,
	db_direntry_arg_clear,
	db_direntry_arg_validate,
};

/**********************************************************************/

static void
db_symlink_arg_free(void *vinfo)
{
	struct db_symlink_arg *info = vinfo;

	if (info->source_path != NULL)
		free(info->source_path);
}

static void
db_symlink_arg_clear(void *vinfo)
{
	struct db_symlink_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_symlink_arg_validate(void *vinfo)
{
	struct db_symlink_arg *info = vinfo;

	return (
	    info->source_path != NULL
	);
}

void
db_symlink_callback_trampoline(void *closure, void *vinfo)
{
	struct db_symlink_trampoline_closure *c = closure;
	struct db_symlink_arg *info = vinfo;

	(*c->callback)(c->closure, info->inum, info->source_path);
}

const struct gfarm_base_generic_info_ops db_base_symlink_arg_ops = {
	sizeof(struct db_symlink_arg),
	db_symlink_arg_free,
	db_symlink_arg_clear,
	db_symlink_arg_validate,
};

/**********************************************************************/

static void
db_quota_dirset_arg_free(void *vinfo)
{
	struct db_quota_dirset_arg *info = vinfo;

	gfarm_dirset_info_free(&info->dirset);
}

static void
db_quota_dirset_arg_clear(void *vinfo)
{
	struct db_quota_dirset_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_quota_dirset_arg_validate(void *vinfo)
{
	struct db_quota_dirset_arg *info = vinfo;

	return (
	    info->dirset.username != NULL &&
	    info->dirset.dirsetname != NULL
	);
}

void
db_quota_dirset_callback_trampoline(void *closure, void *vinfo)
{
	struct db_quota_dirset_trampoline_closure *c = closure;
	struct db_quota_dirset_arg *info = vinfo;

	(*c->callback)(c->closure, &info->dirset, &info->q);
}

const struct gfarm_base_generic_info_ops db_base_quota_dirset_arg_ops = {
	sizeof(struct db_quota_dirset_arg),
	db_quota_dirset_arg_free,
	db_quota_dirset_arg_clear,
	db_quota_dirset_arg_validate,
};

/**********************************************************************/

static void
db_inode_dirset_arg_free(void *vinfo)
{
	struct db_inode_dirset_arg *info = vinfo;

	gfarm_dirset_info_free(&info->dirset);
}

static void
db_inode_dirset_arg_clear(void *vinfo)
{
	struct db_inode_dirset_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_inode_dirset_arg_validate(void *vinfo)
{
	struct db_inode_dirset_arg *info = vinfo;

	return (
	    info->dirset.username != NULL &&
	    info->dirset.dirsetname != NULL
	);
}

void
db_quota_dir_callback_trampoline(void *closure, void *vinfo)
{
	struct db_quota_dir_trampoline_closure *c = closure;
	struct db_inode_dirset_arg *info = vinfo;

	(*c->callback)(c->closure, info->inum, &info->dirset);
}

const struct gfarm_base_generic_info_ops db_base_quota_dir_arg_ops = {
	sizeof(struct db_inode_dirset_arg),
	db_inode_dirset_arg_free,
	db_inode_dirset_arg_clear,
	db_inode_dirset_arg_validate,
};


/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "db_access.h"
#include "db_ops.h"

/**********************************************************************/

gfarm_error_t
gfarm_none_initialize(void)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

gfarm_error_t
gfarm_none_terminate(void)
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_host_add(struct gfarm_host_info *info)
{
	free(info);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_host_modify(struct db_host_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_host_remove(char *hostname)
{
	free(hostname);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_host_load(void *closure,
	void (*callback)(void *, struct gfarm_host_info *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_user_add(struct gfarm_user_info *info)
{
	free(info);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_user_modify(struct db_user_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_user_remove(char *username)
{
	free(username);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_group_add(struct gfarm_group_info *info)
{
	free(info);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_group_modify(struct db_group_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_group_remove(char *groupname)
{
	free(groupname);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_inode_stat_free(struct gfs_stat *arg)
{
	gfs_stat_free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_int64_free(struct db_inode_uint64_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_int32_free(struct db_inode_uint32_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_string_free(struct db_inode_string_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_timespec_free(struct db_inode_timespec_modify_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_inode_cksum_free(struct db_inode_cksum_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_inum_free(struct db_inode_inum_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_inode_cksum_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_filecopy_free(struct db_filecopy_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_deadfilecopy_free(struct db_deadfilecopy_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_direntry_add(struct db_direntry_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_direntry_remove(struct db_direntry_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_symlink_free(struct db_symlink_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_xattr_add(struct db_xattr_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_xattr_modify(struct db_xattr_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_xattr_remove(struct db_xattr_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_xattr_get(struct db_xattr_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

gfarm_error_t
gfarm_none_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

static gfarm_error_t
gfarm_none_xmlattr_find(struct db_xmlattr_find_arg *arg)
{
	free(arg);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
}

/**********************************************************************/

const struct db_ops db_none_ops = {
	gfarm_none_initialize,
	gfarm_none_terminate,

	gfarm_none_host_add,
	gfarm_none_host_modify,
	gfarm_none_host_remove,
	gfarm_none_host_load,

	gfarm_none_user_add,
	gfarm_none_user_modify,
	gfarm_none_user_remove,
	gfarm_none_user_load,

	gfarm_none_group_add,
	gfarm_none_group_modify,
	gfarm_none_group_remove,
	gfarm_none_group_load,

	gfarm_none_inode_stat_free,
	gfarm_none_inode_stat_free,
	gfarm_none_inode_int64_free,
	gfarm_none_inode_int64_free,
	gfarm_none_inode_int32_free,
	gfarm_none_inode_string_free,
	gfarm_none_inode_string_free,
	gfarm_none_inode_timespec_free,
	gfarm_none_inode_timespec_free,
	gfarm_none_inode_timespec_free,
	/* inode_remove: never remove any inode to keep inode->i_gen */
	gfarm_none_inode_load,

	/* cksum */
	gfarm_none_inode_cksum_free,
	gfarm_none_inode_cksum_free,
	gfarm_none_inode_inum_free,
	gfarm_none_inode_cksum_load,

	gfarm_none_filecopy_free,
	gfarm_none_filecopy_free,
	gfarm_none_filecopy_load,

	gfarm_none_deadfilecopy_free,
	gfarm_none_deadfilecopy_free,
	gfarm_none_deadfilecopy_load,

	gfarm_none_direntry_add,
	gfarm_none_direntry_remove,
	gfarm_none_direntry_load,

	gfarm_none_symlink_free,
	gfarm_none_inode_inum_free,
	gfarm_none_symlink_load,

	gfarm_none_xattr_add,
	gfarm_none_xattr_modify,
	gfarm_none_xattr_remove,
	NULL, // gfarm_none_xattr_removeall not supported
	gfarm_none_xattr_get,
	gfarm_none_xattr_load,
	gfarm_none_xmlattr_find,
};

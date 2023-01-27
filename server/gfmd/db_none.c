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

#include <pthread.h>	/* db_access.h currently needs this */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "config.h"
#include "quota_info.h"
#include "metadb_server.h"

#include "quota.h"
#include "db_access.h"
#include "db_ops.h"

/**********************************************************************/

static gfarm_uint64_t db_none_seqnum = 1;

static void
modify_seqnum(gfarm_uint64_t seqnum)
{
	if (gfarm_get_metadb_replication_enabled() && seqnum > 0)
		db_none_seqnum = seqnum;
}

static void
free_arg(void *arg)
{
	/*
	 * - When metadb_replication_enabled() is true, we store objects to
	 *   the journal file via db_journal_ops.
	 *   'arg' is allocated in db_*_dup() and freed in db_journal_enter().
	 *   Functions of db_pgsql_ops are called from
	 *   db_journal_store_thread().
	 *   In db_journal_store_thread(), each object to be passed to
	 *   functions of db_pgsql_ops is allocated in db_journal_read_ops().
	 *   db_journal_read_ops() allocates each object as multiple chunks
	 *   different from db_*_dup() functions which allocate each object
	 *   as single chunk.
	 *   The objects are possibly reused for retrying to call functions
	 *   of db_pgsql_ops and freed in db_journal_ops_free() called from
	 *   db_journal_free_rec_list().
	 *
	 */
	if (!gfarm_get_metadb_replication_enabled())
		free(arg);
}

gfarm_error_t
gfarm_none_initialize(void)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_none_terminate(void)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_nop(gfarm_uint64_t seqnum, void *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_string_free(gfarm_uint64_t seqnum, char *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *info)
{
	modify_seqnum(seqnum);
	free_arg(info);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_host_modify(gfarm_uint64_t seqnum, struct db_host_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	modify_seqnum(seqnum);
	free_arg(hostname);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_fsngroup_modify(gfarm_uint64_t seqnum,
	struct db_fsngroup_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *info)
{
	modify_seqnum(seqnum);
	free_arg(info);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_modify(gfarm_uint64_t seqnum, struct db_user_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_remove(gfarm_uint64_t seqnum, char *username)
{
	modify_seqnum(seqnum);
	free_arg(username);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *info)
{
	modify_seqnum(seqnum);
	free_arg(info);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	modify_seqnum(seqnum);
	free_arg(groupname);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_inode_stat_free(gfarm_uint64_t seqnum, struct gfs_stat *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_int64_free(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_int32_free(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_string_free(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_timespec_free(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_inode_cksum_free(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_inum_free(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_inode_cksum_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_filecopy_free(gfarm_uint64_t seqnum, struct db_filecopy_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_deadfilecopy_free(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_direntry_add(gfarm_uint64_t seqnum, struct db_direntry_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_direntry_remove(gfarm_uint64_t seqnum, struct db_direntry_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_symlink_free(gfarm_uint64_t seqnum, struct db_symlink_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_xattr_free_arg(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_xattr_get(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	modify_seqnum(seqnum);
	free(arg);
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfarm_none_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_xmlattr_find(gfarm_uint64_t seqnum,
	struct db_xmlattr_find_arg *arg)
{
	modify_seqnum(seqnum);
	free(arg);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_quota_add(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_modify(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_load(void *closure, int is_group,
		void (*callback)(void *, struct gfarm_quota_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_quota_dirset_add(gfarm_uint64_t seqnum,
	struct db_quota_dirset_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_dirset_modify(gfarm_uint64_t seqnum,
	struct db_quota_dirset_arg *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_dirset_remove(gfarm_uint64_t seqnum,
	struct gfarm_dirset_info *arg)
{
	modify_seqnum(seqnum);
	free_arg(arg);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_dirset_load(void *closure,
	void (*callback)(void *,
	    struct gfarm_dirset_info *, struct quota_metadata *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_quota_dir_add(gfarm_uint64_t seqnum,
		struct db_inode_dirset_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_dir_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_quota_dir_load(void *closure,
	void (*callback)(void *, gfarm_ino_t, struct gfarm_dirset_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_seqnum_get(const char *name, gfarm_uint64_t *seqnump)
{
	*seqnump = db_none_seqnum;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_seqnum_add(struct db_seqnum_arg *arg)
{
	modify_seqnum(arg->value);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_seqnum_modify(struct db_seqnum_arg *arg)
{
	modify_seqnum(arg->value);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_seqnum_remove(char *name)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_seqnum_load(void *closure,
		void (*callback)(void *, struct db_seqnum_arg *))
{
	struct db_seqnum_arg a = { NULL, db_none_seqnum };

	(*callback)(closure, &a);
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_mdhost_add(gfarm_uint64_t seqnum, struct gfarm_metadb_server *ms)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_mdhost_modify(gfarm_uint64_t seqnum,
	struct db_mdhost_modify_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_mdhost_remove(gfarm_uint64_t seqnum, char *name)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_mdhost_load(void *closure,
	void (*callback)(void *, struct gfarm_metadb_server *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
gfarm_none_user_auth_add(gfarm_uint64_t seqnum, struct db_user_auth_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_auth_modify(gfarm_uint64_t seqnum,
	struct db_user_auth_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_auth_remove(gfarm_uint64_t seqnum,
	struct db_user_auth_remove_arg *arg)
{
	modify_seqnum(seqnum);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
gfarm_none_user_auth_load(void *closure,
	void (*callback)(void *, struct db_user_auth_arg *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

const struct db_ops db_none_ops = {
	gfarm_none_initialize,
	gfarm_none_terminate,

	gfarm_none_nop,
	gfarm_none_nop,

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
	gfarm_none_string_free,
	gfarm_none_filecopy_load,

	gfarm_none_deadfilecopy_free,
	gfarm_none_deadfilecopy_free,
	gfarm_none_string_free,
	gfarm_none_deadfilecopy_load,

	gfarm_none_direntry_add,
	gfarm_none_direntry_remove,
	gfarm_none_direntry_load,

	gfarm_none_symlink_free,
	gfarm_none_inode_inum_free,
	gfarm_none_symlink_load,

	gfarm_none_xattr_free_arg,
	gfarm_none_xattr_free_arg,
	gfarm_none_xattr_free_arg,
	gfarm_none_xattr_free_arg,
	gfarm_none_xattr_get,
	gfarm_none_xattr_load,
	gfarm_none_xmlattr_find,

	gfarm_none_quota_add,
	gfarm_none_quota_modify,
	gfarm_none_quota_remove,
	gfarm_none_quota_load,

	gfarm_none_quota_dirset_add,
	gfarm_none_quota_dirset_modify,
	gfarm_none_quota_dirset_remove,
	gfarm_none_quota_dirset_load,

	gfarm_none_quota_dir_add,
	gfarm_none_quota_dir_remove,
	gfarm_none_quota_dir_load,

	gfarm_none_seqnum_get,
	gfarm_none_seqnum_add,
	gfarm_none_seqnum_modify,
	gfarm_none_seqnum_remove,
	gfarm_none_seqnum_load,

	gfarm_none_mdhost_add,
	gfarm_none_mdhost_modify,
	gfarm_none_mdhost_remove,
	gfarm_none_mdhost_load,

	gfarm_none_fsngroup_modify,

	gfarm_none_user_auth_add,
	gfarm_none_user_auth_modify,
	gfarm_none_user_auth_remove,
	gfarm_none_user_auth_load,
};

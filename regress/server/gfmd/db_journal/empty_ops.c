/*
 * $Id$
 */

#include <pthread.h>	/* db_access.h currently needs this */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "gfp_xdr.h"
#include "config.h"
#include "metadb_server.h"

#include "quota.h"
#include "db_access.h"
#include "db_ops.h"

/**********************************************************************/

gfarm_error_t
empty_initialize(void)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
empty_terminate(void)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_nop(gfarm_uint64_t seqnum, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *info)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_host_modify(gfarm_uint64_t seqnum, struct db_host_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_host_load(void *closure,
	void (*callback)(void *, struct gfarm_internal_host_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *info)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_user_modify(gfarm_uint64_t seqnum, struct db_user_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_user_remove(gfarm_uint64_t seqnum, char *username)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_user_load(void *closure,
	void (*callback)(void *, struct gfarm_user_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *info)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_group_load(void *closure,
	void (*callback)(void *, struct gfarm_group_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_inode_stat(gfarm_uint64_t seqnum, struct gfs_stat *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_int64(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_int32(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_string(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_timespec(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_load(
	void *closure,
	void (*callback)(void *, struct gfs_stat *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_inode_cksum(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_inum(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_inode_cksum_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_filecopy(gfarm_uint64_t seqnum, struct db_filecopy_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_filecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_deadfilecopy(gfarm_uint64_t seqnum, struct db_deadfilecopy_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_deadfilecopy_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_direntry(gfarm_uint64_t seqnum, struct db_direntry_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_direntry_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_symlink(gfarm_uint64_t seqnum, struct db_symlink_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_symlink_load(
	void *closure,
	void (*callback)(void *, gfarm_ino_t, char *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_xattr(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
empty_xattr_load(void *closure,
		void (*callback)(void *, struct xattr_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_xmlattr_find(gfarm_uint64_t seqnum,
	struct db_xmlattr_find_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_quota(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_quota_load(void *closure, int is_group,
		void (*callback)(void *, struct gfarm_quota_info *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

static gfarm_error_t
empty_seqnum(struct db_seqnum_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_seqnum_remove(char *name)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_seqnum_load(void *closure,
		void (*callback)(void *, struct db_seqnum_arg *))
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_seqnum_get(const char *name, gfarm_uint64_t *seqnump)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_mdhost_add(gfarm_uint64_t seqnum, struct gfarm_metadb_server *info)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_mdhost_modify(gfarm_uint64_t seqnum, struct db_mdhost_modify_arg *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_mdhost_remove(gfarm_uint64_t seqnum, char *name)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
empty_mdhost_load(void *closure, void (*callback)(void *,
	struct gfarm_metadb_server *))
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************************/

const struct db_ops empty_ops = {
	empty_initialize,
	empty_terminate,

	empty_nop,
	empty_nop,

	empty_host_add,
	empty_host_modify,
	empty_host_remove,
	empty_host_load,

	empty_user_add,
	empty_user_modify,
	empty_user_remove,
	empty_user_load,

	empty_group_add,
	empty_group_modify,
	empty_group_remove,
	empty_group_load,

	empty_inode_stat,
	empty_inode_stat,
	empty_inode_int64,
	empty_inode_int64,
	empty_inode_int64,
	empty_inode_int32,
	empty_inode_string,
	empty_inode_string,
	empty_inode_timespec,
	empty_inode_timespec,
	empty_inode_timespec,
	empty_inode_load,

	empty_inode_cksum,
	empty_inode_cksum,
	empty_inode_inum,
	empty_inode_cksum_load,

	empty_filecopy,
	empty_filecopy,
	empty_filecopy_load,

	empty_deadfilecopy,
	empty_deadfilecopy,
	empty_deadfilecopy_load,

	empty_direntry,
	empty_direntry,
	empty_direntry_load,

	empty_symlink,
	empty_inode_inum,
	empty_symlink_load,

	empty_xattr,
	empty_xattr,
	empty_xattr,
	NULL,
	empty_xattr,
	empty_xattr_load,
	empty_xmlattr_find,

	empty_quota,
	empty_quota,
	empty_quota_remove,
	empty_quota_load,

	empty_seqnum_get,
	empty_seqnum,
	empty_seqnum,
	empty_seqnum_remove,
	empty_seqnum_load,

	empty_mdhost_add,
	empty_mdhost_modify,
	empty_mdhost_remove,
	empty_mdhost_load,
};


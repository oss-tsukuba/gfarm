/*
 * $Id$
 */

#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"

#include "gfp_xdr.h"
#include "config.h"
#include "metadb_server.h"

#include "host.h"
#include "user.h"
#include "group.h"
#include "inode.h"
#include "dir.h"
#include "quota.h"
#include "mdhost.h"
#include "journal_file.h"	/* for enum journal_operation */
#include "db_ops.h"
#include "db_journal.h"

/**********************************************************/
/* transaction */

static gfarm_error_t
db_journal_apply_begin(gfarm_uint64_t seqnum, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_apply_end(gfarm_uint64_t seqnum, void *arg)
{
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************/
/* host */

static gfarm_error_t
db_journal_apply_host_add(gfarm_uint64_t seqnum, struct gfarm_host_info *hi)
{
	gfarm_error_t e;

	if (host_lookup(hi->hostname)) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1003202,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum,
		    hi->hostname, gfarm_error_string(e));
	} else if ((e = host_enter(hi, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003203,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum,
		    hi->hostname, gfarm_error_string(e));
	} else
		memset(hi, 0, sizeof(*hi));
	return (e);
}

static gfarm_error_t
db_journal_apply_host_modify(gfarm_uint64_t seqnum,
	struct db_host_modify_arg *arg)
{
	gfarm_error_t e;
	struct host *h;
	struct gfarm_host_info *hi = &arg->hi;

	if ((h = host_lookup(hi->hostname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1003204,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum,
		    hi->hostname, gfarm_error_string(e));
	} else {
		host_modify(h, hi);
		e = GFARM_ERR_NO_ERROR;
	}
	return (e);
}

static gfarm_error_t
db_journal_apply_host_remove(gfarm_uint64_t seqnum, char *hostname)
{
	gfarm_error_t e;

	if ((e = host_remove_in_cache(hostname))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003205,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum,
		    hostname, gfarm_error_string(e));
	return (e);
}

/**********************************************************/
/* fsngroup */

static gfarm_error_t
db_journal_apply_fsngroup_modify(gfarm_uint64_t seqnum,
	struct db_fsngroup_modify_arg *arg)
{
	gfarm_error_t e;
	struct host *h;
	static const char diag[] = "db_journal_apply_fsngroup_modify";

	if ((h = host_lookup_including_invalid(arg->hostname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_UNFIXED,
			"seqnum=%llu hostname=%s : %s",
			(unsigned long long)seqnum,
			arg->hostname, gfarm_error_string(e));
	} else {
		e = host_fsngroup_modify(h, arg->fsngroupname, diag);
	}
	return (e);
}

/**********************************************************/
/* user */

static gfarm_error_t
db_journal_apply_user_add(gfarm_uint64_t seqnum, struct gfarm_user_info *ui)
{
	gfarm_error_t e;

	if (user_lookup(ui->username) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1003206,
		    "seqnum=%llu username=%s : %s",
		    (unsigned long long)seqnum,
		    ui->username, gfarm_error_string(e));
	} else if ((e = user_enter(ui, NULL)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003207,
		    "seqnum=%llu username=%s : %s",
		    (unsigned long long)seqnum,
		    ui->username, gfarm_error_string(e));
	else
		memset(ui, 0, sizeof(*ui));
	return (e);
}

static gfarm_error_t
db_journal_apply_user_modify(gfarm_uint64_t seqnum,
	struct db_user_modify_arg *m)
{
	gfarm_error_t e;
	struct user *u;
	struct gfarm_user_info *ui = &m->ui;

	if ((u = user_lookup(ui->username)) == NULL) {
		e = GFARM_ERR_NO_SUCH_USER;
		gflog_error(GFARM_MSG_1003208,
		    "seqnum=%llu username=%s : %s",
		    (unsigned long long)seqnum,
		    ui->username, gfarm_error_string(e));
	} else {
		e = user_modify(u, ui);
	}
	return (e);
}

static gfarm_error_t
db_journal_apply_user_remove(gfarm_uint64_t seqnum, char *username)
{
	gfarm_error_t e;

	if ((e = user_remove_in_cache(username)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003209,
		    "seqnum=%llu username=%s : %s",
		    (unsigned long long)seqnum,
		    username, gfarm_error_string(e));
	return (e);
}

/**********************************************************/
/* group */

static gfarm_error_t
db_journal_apply_group_add(gfarm_uint64_t seqnum, struct gfarm_group_info *gi)
{
	gfarm_error_t e;

	if (group_lookup(gi->groupname) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1003210,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    gi->groupname, gfarm_error_string(e));
	} else if ((e = group_user_check(gi, "db_journal_apply_group_add"))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003211,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    gi->groupname, gfarm_error_string(e));
	else if ((e = group_info_add(gi)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003212,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    gi->groupname, gfarm_error_string(e));
	else
		memset(gi, 0, sizeof(*gi));
	return (e);
}

static gfarm_error_t
db_journal_apply_group_modify(gfarm_uint64_t seqnum,
	struct db_group_modify_arg *arg)
{
	gfarm_error_t e;
	struct group *g;
	struct gfarm_group_info *gi = &arg->gi;
	const char *diag = "db_journal_apply_group_modify";

	if ((g = group_lookup(gi->groupname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_GROUP;
		gflog_error(GFARM_MSG_1003213,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    gi->groupname, gfarm_error_string(e));
	} else if ((e = group_user_check(gi, diag)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003214,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    gi->groupname, gfarm_error_string(e));
	else
		group_modify(g, gi, diag);
	return (e);
}

static gfarm_error_t
db_journal_apply_group_remove(gfarm_uint64_t seqnum, char *groupname)
{
	gfarm_error_t e;

	if ((e = group_remove_in_cache(groupname))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003215,
		    "seqnum=%llu groupname=%s : %s",
		    (unsigned long long)seqnum,
		    groupname, gfarm_error_string(e));
	return (e);
}

/**********************************************************/
/* inode */

static gfarm_error_t
db_journal_inode_lookup(gfarm_ino_t ino, struct inode **np,
	const char *diag)
{
	if ((*np = inode_lookup(ino)) != NULL)
		return (GFARM_ERR_NO_ERROR);
	*np = inode_alloc_num(ino);
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_apply_inode_add(gfarm_uint64_t seqnum, struct gfs_stat *st)
{
	gfarm_error_t e;

	if (inode_lookup(st->st_ino) != NULL) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1003216,
		    "seqnum=%llu inum=%llu : %s", (unsigned long long)seqnum,
		    (unsigned long long)st->st_ino, gfarm_error_string(e));
	} else if ((e = inode_add_or_modify_in_cache(st, NULL))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003217,
		    "seqnum=%llu inum=%llu : %s", (unsigned long long)seqnum,
		    (unsigned long long)st->st_ino, gfarm_error_string(e));
	else
		memset(st, 0, sizeof(*st));
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_modify(gfarm_uint64_t seqnum, struct gfs_stat *st)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = inode_add_or_modify_in_cache(st, &n)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003218,
		    "seqnum=%llu inum=%llu : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)st->st_ino,
		    gfarm_error_string(e));
	else
		memset(st, 0, sizeof(*st));
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_gen_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_gen_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003219,
		    "seqnum=%llu inum=%llu : %s", (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_gen_in_cache(n, arg->uint64);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_nlink_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_nlink_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003220,
		    "seqnum=%llu inum=%llu : %s", (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_nlink_in_cache(n, arg->uint64);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_size_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint64_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_size_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003221,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_size_in_cache(n, arg->uint64);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_mode_modify(gfarm_uint64_t seqnum,
	struct db_inode_uint32_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_mode_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003222,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_mode_in_cache(n, arg->uint32);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_user_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_user_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003223,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_user_by_name_in_cache(n, arg->string);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_group_modify(gfarm_uint64_t seqnum,
	struct db_inode_string_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_group_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003224,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_group_by_name_in_cache(n, arg->string);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_atime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_atime_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003225,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_atime_in_cache(n, &arg->time);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_mtime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_mtime_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003226,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_mtime_in_cache(n, &arg->time);
	return (e);
}

static gfarm_error_t
db_journal_apply_inode_ctime_modify(gfarm_uint64_t seqnum,
	struct db_inode_timespec_modify_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_inode_ctime_modify")) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003227,
		    "inum=%llu : %s",
		    (unsigned long long)arg->inum, gfarm_error_string(e));
	else
		inode_set_ctime_in_cache(n, &arg->time);
	return (e);
}

/**********************************************************/
/* inode_cksum */

static gfarm_error_t
db_journal_apply_inode_cksum_add(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
db_journal_apply_inode_cksum_modify(gfarm_uint64_t seqnum,
	struct db_inode_cksum_arg *arg)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static gfarm_error_t
db_journal_apply_inode_cksum_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	/* XXX - NOT IMPLEMENTED */
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************/
/* filecopy */

static gfarm_error_t
db_journal_apply_filecopy_add(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;
	struct host *host;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_filecopy_add")) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((host = host_lookup(arg->hostname)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1003228,
		    "inum=%llu hostname=%s : %s",
		    (unsigned long long)arg->inum, arg->hostname,
		    gfarm_error_string(e));
	} else if ((e = inode_add_file_copy_in_cache(n,
	    host)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003229,
		    "inum=%llu hostname=%s : %s",
		    (unsigned long long)arg->inum, arg->hostname,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
db_journal_apply_filecopy_remove(gfarm_uint64_t seqnum,
	struct db_filecopy_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;
	struct host *host;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_filecopy_remove")) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((host = host_lookup_including_invalid(arg->hostname))
	    == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1003230,
		    "inum=%llu hostname=%s : %s",
		    (unsigned long long)arg->inum, arg->hostname,
		    gfarm_error_string(e));
	} else if ((e = inode_remove_replica_in_cache(n,
	    host)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003231,
		    "inum=%llu hostname=%s : %s",
		    (unsigned long long)arg->inum, arg->hostname,
		    gfarm_error_string(e));
	}
	return (e);
}

/**********************************************************/
/* deadfilecopy */

static gfarm_error_t
db_journal_apply_deadfilecopy_add(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	/* nothing to do */
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
db_journal_apply_deadfilecopy_remove(gfarm_uint64_t seqnum,
	struct db_deadfilecopy_arg *arg)
{
	/* nothing to do */
	return (GFARM_ERR_NO_ERROR);
}

/**********************************************************/
/* direntry */

static gfarm_error_t
db_journal_apply_direntry_add(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	gfarm_error_t e;

	if ((e = dir_entry_add(arg->dir_inum, arg->entry_name, arg->entry_len,
	    arg->entry_inum)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003232,
		    "seqnum=%llu dir_inum=%llu entry_inum=%llu : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->dir_inum,
		    (unsigned long long)arg->entry_inum,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
db_journal_apply_direntry_remove(gfarm_uint64_t seqnum,
	struct db_direntry_arg *arg)
{
	gfarm_error_t e;
	Dir dir;
	struct inode *idir = inode_lookup(arg->dir_inum);

	if (idir == NULL || (dir = inode_get_dir(idir)) == NULL) {
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
		gflog_error(GFARM_MSG_1003233,
		    "seqnum=%llu dir_inum=%llu entry_inum=%llu : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->dir_inum,
		    (unsigned long long)arg->entry_inum,
		    gfarm_error_string(e));
	} else if (dir_lookup(dir, arg->entry_name, arg->entry_len) == NULL) {
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
		gflog_error(GFARM_MSG_1003234,
		    "seqnum=%llu dir_inum=%llu entry_inum=%llu : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->dir_inum,
		    (unsigned long long)arg->entry_inum,
		    gfarm_error_string(e));
	} else {
		(void)dir_remove_entry(dir, arg->entry_name, arg->entry_len);
		e = GFARM_ERR_NO_ERROR;
	}
	return (e);
}

/**********************************************************/
/* symlink */

static gfarm_error_t
db_journal_apply_symlink_add(gfarm_uint64_t seqnum,
	struct db_symlink_arg *arg)
{
	gfarm_error_t e;

	if ((e = symlink_add(arg->inum, arg->source_path))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003235,
		    "seqnum=%llu inum=%llu source_path=%s : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, arg->source_path,
		    gfarm_error_string(e));
	else
		arg->source_path = NULL;
	return (e);
}

static gfarm_error_t
db_journal_apply_symlink_remove(gfarm_uint64_t seqnum,
	struct db_inode_inum_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_symlink_remove")) == GFARM_ERR_NO_ERROR)
		inode_clear_symlink(n);
	return (e);
}

/**********************************************************/
/* xattr */

static gfarm_error_t
db_journal_apply_xattr_add(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_xattr_add")) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((e = inode_xattr_add(n, arg->xmlMode, arg->attrname,
	    arg->value, arg->size)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003236,
		    "seqnum=%llu inum=%llu attrname=%s : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, arg->attrname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
db_journal_apply_xattr_modify(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_xattr_modify")) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((e = inode_xattr_modify(n, arg->xmlMode, arg->attrname,
	    arg->value, arg->size)) != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003237,
		    "seqnum=%llu inum=%llu attrname=%s : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, arg->attrname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
db_journal_apply_xattr_remove(gfarm_uint64_t seqnum, struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_xattr_remove")) != GFARM_ERR_NO_ERROR) {
		/* nothing to do */
	} else if ((e = inode_xattr_remove(n, arg->xmlMode, arg->attrname))
	    != GFARM_ERR_NO_ERROR)
		gflog_error(GFARM_MSG_1003238,
		    "seqnum=%llu inum=%llu attrname=%s : %s",
		    (unsigned long long)seqnum,
		    (unsigned long long)arg->inum, arg->attrname,
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
db_journal_apply_xattr_removeall(gfarm_uint64_t seqnum,
	struct db_xattr_arg *arg)
{
	gfarm_error_t e;
	struct inode *n;

	if ((e = db_journal_inode_lookup(arg->inum, &n,
	    "db_journal_apply_xattr_removeall")) == GFARM_ERR_NO_ERROR)
		inode_xattrs_clear(n);
	return (e);
}

/**********************************************************/
/* quota */

static gfarm_error_t
db_journal_apply_quota_add(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	gfarm_error_t e;
	struct quota *q;

	if ((e = quota_lookup(arg->name, arg->is_group, &q,
	    "db_journal_apply_quota_add")) == GFARM_ERR_NO_ERROR)
		*q = arg->quota;
	return (e);
}

static gfarm_error_t
db_journal_apply_quota_modify(gfarm_uint64_t seqnum, struct db_quota_arg *arg)
{
	gfarm_error_t e;
	struct quota *q;

	if ((e = quota_lookup(arg->name, arg->is_group, &q,
	    "db_journal_apply_quota_modify")) == GFARM_ERR_NO_ERROR)
		*q = arg->quota;
	return (e);
}

static gfarm_error_t
db_journal_apply_quota_remove(gfarm_uint64_t seqnum,
	struct db_quota_remove_arg *arg)
{
	gfarm_error_t e;
	struct quota *q;

	if ((e = quota_lookup(arg->name, arg->is_group, &q,
	    "db_journal_apply_quota_remove")) == GFARM_ERR_NO_ERROR)
		q->on_db = 0;
	return (e);
}

/**********************************************************/
/* mdhost */

static gfarm_error_t
db_journal_apply_mdhost_add(gfarm_uint64_t seqnum,
	struct gfarm_metadb_server *ms)
{
	gfarm_error_t e;

	if (mdhost_lookup(ms->name)) {
		e = GFARM_ERR_ALREADY_EXISTS;
		gflog_error(GFARM_MSG_1003239,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum, ms->name,
		    gfarm_error_string(e));
	} else if ((e = mdhost_enter(ms, NULL)) != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003240,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum, ms->name,
		    gfarm_error_string(e));
	} else
		memset(ms, 0, sizeof(*ms));
	return (e);
}

static gfarm_error_t
db_journal_apply_mdhost_modify(gfarm_uint64_t seqnum,
	struct db_mdhost_modify_arg *arg)
{
	gfarm_error_t e;
	struct mdhost *mh;

	if ((mh = mdhost_lookup(arg->ms.name)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1003241,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum, arg->ms.name,
		    gfarm_error_string(e));
	} else if ((e = mdhost_modify_in_cache(mh, &arg->ms))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_1003242,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum, arg->ms.name,
		    gfarm_error_string(e));
	}
	return (e);
}

static gfarm_error_t
db_journal_apply_mdhost_remove(gfarm_uint64_t seqnum, char *name)
{
	gfarm_error_t e;

	if (mdhost_lookup(name) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		gflog_error(GFARM_MSG_1003243,
		    "seqnum=%llu hostname=%s : %s",
		    (unsigned long long)seqnum, name,
		    gfarm_error_string(e));
	} else
		e = mdhost_remove_in_cache(name);
	return (e);
}

/**********************************************************/

const struct db_ops db_journal_apply_ops = {
	NULL,
	NULL,

	db_journal_apply_begin,
	db_journal_apply_end,

	db_journal_apply_host_add,
	db_journal_apply_host_modify,
	db_journal_apply_host_remove,
	NULL,

	db_journal_apply_user_add,
	db_journal_apply_user_modify,
	db_journal_apply_user_remove,
	NULL,

	db_journal_apply_group_add,
	db_journal_apply_group_modify,
	db_journal_apply_group_remove,
	NULL,

	db_journal_apply_inode_add,
	db_journal_apply_inode_modify,
	db_journal_apply_inode_gen_modify,
	db_journal_apply_inode_nlink_modify,
	db_journal_apply_inode_size_modify,
	db_journal_apply_inode_mode_modify,
	db_journal_apply_inode_user_modify,
	db_journal_apply_inode_group_modify,
	db_journal_apply_inode_atime_modify,
	db_journal_apply_inode_mtime_modify,
	db_journal_apply_inode_ctime_modify,
	NULL,

	db_journal_apply_inode_cksum_add,
	db_journal_apply_inode_cksum_modify,
	db_journal_apply_inode_cksum_remove,
	NULL,

	db_journal_apply_filecopy_add,
	db_journal_apply_filecopy_remove,
	NULL,

	db_journal_apply_deadfilecopy_add,
	db_journal_apply_deadfilecopy_remove,
	NULL,

	db_journal_apply_direntry_add,
	db_journal_apply_direntry_remove,
	NULL,

	db_journal_apply_symlink_add,
	db_journal_apply_symlink_remove,
	NULL,

	db_journal_apply_xattr_add,
	db_journal_apply_xattr_modify,
	db_journal_apply_xattr_remove,
	db_journal_apply_xattr_removeall,
	NULL,
	NULL,
	NULL,

	db_journal_apply_quota_add,
	db_journal_apply_quota_modify,
	db_journal_apply_quota_remove,
	NULL,

	NULL,
	NULL,
	NULL,
	NULL,
	NULL,

	db_journal_apply_mdhost_add,
	db_journal_apply_mdhost_modify,
	db_journal_apply_mdhost_remove,
	NULL,

	db_journal_apply_fsngroup_modify,
};

void
db_journal_apply_init(void)
{
	db_journal_set_apply_ops(&db_journal_apply_ops);
}

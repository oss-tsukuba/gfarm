#include <assert.h>
#include <stdarg.h> /* for "gfp_xdr.h" */
#include <stdlib.h>
#include <string.h>

#define GFARM_INTERNAL_USE
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "gfm_proto.h"

#include "subr.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "process.h"
#include "peer.h"
#include "fs.h"

gfarm_error_t
gfm_server_compound_begin(struct peer *peer, int from_client, int skip,
	int level)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level > 1) /* We don't allow nesting */
		e = GFARM_ERR_INVALID_ARGUMENT;
	return (gfm_server_put_reply(peer, "compound_begin", e, ""));
}

gfarm_error_t
gfm_server_compound_end(struct peer *peer, int from_client, int skip,
	int level)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level < 1) /* nesting doesn't match */
		e = GFARM_ERR_INVALID_ARGUMENT;
	return (gfm_server_put_reply(peer, "compound_end", e, ""));
}

gfarm_error_t
gfm_server_compound_on_error(struct peer *peer, int from_client, int skip,
	int level, gfarm_error_t *on_errorp)
{
	gfarm_error_t e, on_error;

	e = gfm_server_get_request(peer, "compound_on_error", "i", &on_error);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level < 1) /* there isn't COMPOUND_BEGIN ... END block around */
		e = GFARM_ERR_INVALID_ARGUMENT;
	else 
		*on_errorp = on_error;
	return (gfm_server_put_reply(peer, "compound_on_error",
	    GFARM_ERR_NO_ERROR, ""));
}

gfarm_error_t
gfm_server_get_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;

	e = gfm_server_get_request(peer, "get_fd", "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = peer_fdpair_externalize_current(peer);
	giant_unlock();
	
	return (gfm_server_put_reply(peer, "get_fd", e, "i", fd));
}

gfarm_error_t
gfm_server_put_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;

	e = gfm_server_get_request(peer, "put_fd", "i", &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = process_verify_fd(process, fd)) != GFARM_ERR_NO_ERROR)
		;
	else {
		peer_fdpair_set_current(peer, fd);
		e = peer_fdpair_externalize_current(peer);
	}
	giant_unlock();
	
	return (gfm_server_put_reply(peer, "put_fd", e, ""));
}

gfarm_error_t
gfm_server_save_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;

	e = gfm_server_get_request(peer, "save_fd", "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = peer_fdpair_save(peer);
	giant_unlock();
	
	return (gfm_server_put_reply(peer, "save_fd", e, ""));
}

gfarm_error_t
gfm_server_restore_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;

	e = gfm_server_get_request(peer, "restore_fd", "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = peer_fdpair_restore(peer);
	giant_unlock();
	
	return (gfm_server_put_reply(peer, "restore_fd", e, ""));
}

/* this assumes that giant_lock is already acquired */
gfarm_error_t
gfm_server_open_common(struct peer *peer, int from_client,
	char *name, gfarm_int32_t flag, int to_create, gfarm_int32_t mode,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_int32_t *modep)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	int op;
	struct inode *base, *inode;
	int created;
	gfarm_int32_t cfd, fd = -1;

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((process = peer_get_process(peer)) == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if (process_get_user(process) == NULL)
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	if ((e = peer_fdpair_get_current(peer, &cfd)) != GFARM_ERR_NO_ERROR)
		return (e);
	if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR)
		return (e);

	if (flag & ~GFARM_FILE_USER_MODE)
		return (GFARM_ERR_INVALID_ARGUMENT);
	op = accmode_to_op(flag);

	if (to_create) {
		if (mode & ~GFARM_S_ALLPERM)
			return (GFARM_ERR_INVALID_ARGUMENT);
		e = inode_create_file(base, name, process, op, mode, &inode,
		    &created);
	} else {
		e = inode_lookup_by_name(base, name, process, op, &inode);
		created = 0;
	}
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	e = process_open_file(process, inode, flag, created, peer, spool_host,
	    &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (created && !from_client) {
		e = inode_add_replica(inode, spool_host, 1);
		if (e != GFARM_ERR_NO_ERROR) {
			process_close_file(process, peer, fd);
			inode_unlink(base, name, process);
			return (e);
		}
	}
	peer_fdpair_set_current(peer, fd);
	*inump = inode_get_number(inode);
	*genp = inode_get_gen(inode);
	*modep = inode_get_mode(inode);
	return(GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_create(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	gfarm_int32_t flag, perm;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0;

	e = gfm_server_get_request(peer, "create", "sii", &name, &flag, &perm);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	e = gfm_server_open_common(peer, from_client, name, flag, 1, perm,
	    &inum, &gen, &mode);

	if (debug_mode) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info("create(%s) -> error: %s",
			    name, gfarm_error_string(e));
		} else {
			gfarm_int32_t fd;
			peer_fdpair_get_current(peer, &fd);
			gflog_info("create(%s) -> %d, %" GFARM_PRId64
			    ":%" GFARM_PRId64 ", %3o",
			    name, fd, inum, gen, mode);
		}
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, "create", e, "lli",
	    inum, gen, mode));
}

gfarm_error_t
gfm_server_open(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	gfarm_uint32_t flag;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0;

	e = gfm_server_get_request(peer, "open", "si", &name, &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	e = gfm_server_open_common(peer, from_client, name, flag, 0, 0,
	    &inum, &gen, &mode);

	if (debug_mode) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info("open(%s) -> error: %s",
			    name, gfarm_error_string(e));
		} else {
			gfarm_int32_t fd;
			peer_fdpair_get_current(peer, &fd);
			gflog_info("open(%s) -> %d, %" GFARM_PRId64
			    ":%" GFARM_PRId64 ", %3o",
			    name, fd, inum, gen, mode);
		}
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, "open", e, "lli", inum, gen, mode));
}

gfarm_error_t
gfm_server_open_root(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	int op;
	struct inode *inode;
	gfarm_uint32_t flag;
	gfarm_int32_t fd = -1;

	e = gfm_server_get_request(peer, "open_root", "i", &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (flag & ~GFARM_FILE_USER_MODE)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if ((op = accmode_to_op(flag)) & GFS_W_OK)
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		if (debug_mode)
			gflog_info("open_root: from_client=%d, spool?:%d\n",
			    from_client, spool_host != NULL);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		if (debug_mode)
			gflog_info("get_process?:%d\n", process != NULL);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;

	} else if ((e = inode_lookup_root(process, op, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info("inode_lookup_root?:%s\n",
			    gfarm_error_string(e));
	} else if ((e = process_open_file(process, inode, flag, 0,
	    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR)
		;
	else
		peer_fdpair_set_current(peer, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, "open_root", e, ""));
}

gfarm_error_t
gfm_server_open_parent(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	int op;
	struct process *process;
	gfarm_uint32_t flag;
	gfarm_int32_t cfd, fd = -1;
	struct inode *base, *inode;

	e = gfm_server_get_request(peer, "open_parent", "i", &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (flag & ~GFARM_FILE_USER_MODE)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if ((op = accmode_to_op(flag)) & GFS_W_OK)
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, cfd, &base)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_lookup_parent(base, process, op, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_open_file(process, inode, flag, 0,
	    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR)
		;
	else
		peer_fdpair_set_current(peer, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, "open_parent", e, ""));
}

gfarm_error_t
gfm_server_close(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t fd = -1;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_close_file(process, peer, fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = peer_fdpair_close_current(peer);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close", e, ""));
}

gfarm_error_t
gfm_server_verify_type(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_uint32_t type;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("verify_type: not implemented");

	e = gfm_server_get_request(peer, "verify_type", "i", &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "verify_type",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_verify_type_not(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_uint32_t type;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("verify_type_not: not implemented");

	e = gfm_server_get_request(peer, "verify_type_not", "i", &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "verify_type_not",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_fstat(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	memset(&st, 0, sizeof(st));
#endif
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) ==
	    GFARM_ERR_NO_ERROR) {
		st.st_ino = inode_get_number(inode);
		st.st_gen = inode_get_gen(inode);
		st.st_mode = inode_get_mode(inode);
		st.st_nlink = inode_get_nlink(inode);
		st.st_user = user_name(inode_get_user(inode));
		st.st_group = group_name(inode_get_group(inode));
		st.st_size = inode_get_size(inode);
		if (inode_is_file(inode))
			st.st_ncopy = inode_get_ncopy(inode);
		st.st_atimespec = *inode_get_atime(inode);
		st.st_mtimespec = *inode_get_mtime(inode);
		st.st_ctimespec = *inode_get_ctime(inode);
	}

	giant_unlock();
	/*
	 * XXX assumes that user_name() and group_name() are not broken
	 * even without giant_lock.
	 */
	return (gfm_server_put_reply(peer, "fstat", e, "llilsslllilili",
	    st.st_ino, st.st_gen, st.st_mode, st.st_nlink,
	    st.st_user, st.st_group, st.st_size, st.st_ncopy,
	    st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec, 
	    st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec, 
	    st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec));
}

gfarm_error_t
gfm_server_futimes(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime, mtime;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user;
	struct inode *inode;

	e = gfm_server_get_request(peer, "futimes", "lili",
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if ((user = process_get_user(process)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (user != inode_get_user(inode) && !user_is_admin(user) &&
	    (e = process_get_file_writable(process, peer, fd)) !=
	    GFARM_ERR_NO_ERROR)
		e = GFARM_ERR_PERMISSION_DENIED;
	else {
		inode_set_atime(inode, &atime);
		inode_set_mtime(inode, &mtime);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, "futimes", e, ""));
}

gfarm_error_t
gfm_server_fchmod(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t mode;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user;
	struct inode *inode;

	e = gfm_server_get_request(peer, "fchmod", "i", &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if (user != inode_get_user(inode) && !user_is_admin(user))
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = inode_set_mode(inode, mode);

	giant_unlock();
	return (gfm_server_put_reply(peer, "fchmod", e, ""));
}

gfarm_error_t
gfm_server_fchown(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *groupname;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user, *new_user = NULL;
	struct group *new_group = NULL;
	struct inode *inode;

	e = gfm_server_get_request(peer, "fchown", "ss",
	    &username, &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((user = process_get_user(process)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if (*username != '\0' &&
	    (new_user = user_lookup(username)) == NULL)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if (*groupname != '\0' &&
	    (new_group = group_lookup(groupname)) == NULL)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if (new_user != NULL && !user_is_admin(user))
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (new_group != NULL && !user_is_admin(user) &&
	    (user != inode_get_user(inode) || !user_in_group(user, new_group)))
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = inode_set_owner(inode, new_user, new_group);

	free(username);
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, "fchown", e, ""));
}

gfarm_error_t
gfm_server_cksum_get(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	gfarm_int32_t flags = 0;
	size_t cksum_len = 0;
	struct host *spool_host = NULL;
	struct process *process;
	char *cksum_type = NULL, *cksumbuf = NULL, *cksum;
	int alloced = 0;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_cksum_get(process, peer, fd,
	    &cksum_type, &cksum_len, &cksum, &flags)) != GFARM_ERR_NO_ERROR) {
		/* We cannot access cksum_type and cksum outside of giant */
		if (cksum_type == NULL) {
			cksum_type = "";
			cksumbuf = "";
		} else {
			alloced = 1;
			cksum_type = strdup(cksum_type);
			GFARM_MALLOC_ARRAY(cksumbuf, cksum_len);
			memcpy(cksumbuf, cksum, cksum_len);
		}
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, "cksum_get", e, "sbi",
	    cksum_type, cksum_len, cksumbuf,  flags);
	if (e == GFARM_ERR_NO_ERROR && alloced) {
		free(cksum_type);
		free(cksumbuf);
	}
	return (e);
}

gfarm_error_t
gfm_server_cksum_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t cksum_len, flags;
	struct host *spool_host = NULL;
	struct process *process;
	char *cksum_type;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	struct gfarm_timespec mtime;

	e = gfm_server_get_request(peer, "cksum_set", "sbili",
	    &cksum_type, sizeof(cksum), &cksum_len, cksum, &flags,
	    &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(cksum_type);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = process_cksum_set(process, peer, fd,
		    cksum_type, cksum_len, cksum, flags, &mtime);

	free(cksum_type);
	giant_unlock();
	return (gfm_server_put_reply(peer, "cksum_set", e, ""));
}

gfarm_error_t
gfm_server_schedule_file(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *domain;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;

	e = gfm_server_get_request(peer, "schedule_file", "s", &domain);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (*domain != '\0')
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED; /* XXX FIXME */
	else if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if (!inode_is_file(inode))
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	else {
		/* XXX FIXME too long giant lock */
		e = inode_schedule_file_reply(inode, peer,
		    process_get_file_writable(process, peer, fd)
		    == GFARM_ERR_NO_ERROR,
		    inode_is_creating_file(inode), "schedule_file");

		free(domain);
		giant_unlock();
		return (e);
	}

	assert(e != GFARM_ERR_NO_ERROR);
	free(domain);
	giant_unlock();
	return (gfm_server_put_reply(peer, "schedule_file", e, ""));
}

gfarm_error_t
gfm_server_schedule_file_with_program(struct peer *peer, int from_client,
	int skip)
{
	gfarm_error_t e;
	char *domain;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("schedule_file_with_program: not implemented");

	e = gfm_server_get_request(
		peer, "schedule_file_with_program", "s", &domain);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}

	free(domain);
	e = gfm_server_put_reply(peer, "schedule_file_with_program",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *base;

	e = gfm_server_get_request(peer, "remove", "s", &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (process_get_user(process) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR)
		;
	else
		e = inode_unlink(base, name, process);

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, "remove", e, ""));
}

gfarm_error_t
gfm_server_rename(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *sname, *dname;
	struct process *process;
	gfarm_int32_t sfd, dfd;
	struct inode *sdir, *ddir;

	e = gfm_server_get_request(peer, "rename", "ss", &sname, &dname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(sname);
		free(dname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (process_get_user(process) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_saved(peer, &sfd)) != GFARM_ERR_NO_ERROR)
		;
	else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, sfd, &sdir))
	    != GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, dfd, &ddir))
	    != GFARM_ERR_NO_ERROR)
		;
	else
		e = inode_rename(sdir, sname, ddir, dname, process);

	free(sname);
	free(dname);
	giant_unlock();
	return (gfm_server_put_reply(peer, "rename", e, ""));
}

gfarm_error_t
gfm_server_flink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("flink: not implemented");

	e = gfm_server_get_request(peer, "flink", "s", &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	free(name);
	e = gfm_server_put_reply(peer, "flink",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_mkdir(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	gfarm_int32_t mode;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *base;

	e = gfm_server_get_request(peer, "mkdir", "si", &name, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if (process_get_user(process) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR)
		;
	else if (mode & ~GFARM_S_ALLPERM)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else
		e = inode_create_dir(base, name, process, mode);

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, "mkdir", e, ""));
}

gfarm_error_t
gfm_server_symlink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *target, *name;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("symlink: not implemented");

	e = gfm_server_get_request(peer, "symlink", "ss", &target, &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(target);
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	free(target);
	free(name);
	e = gfm_server_put_reply(peer, "symlink",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_readlink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("readlink: not implemented");

	e = gfm_server_put_reply(peer, "readlink",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_getdirpath(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e_rpc;

	struct process *process;
	gfarm_int32_t cfd;
	struct inode *dir;
	char *s;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, cfd, &dir)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = inode_getdirpath(dir, process, &s);

	giant_unlock();
	e_rpc = gfm_server_put_reply(peer, "getdirpath", e, "s", s);
	if (e == GFARM_ERR_NO_ERROR)
		free(s);
	return (e_rpc);
}

gfarm_error_t
gfm_server_getdirents(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e, e2;
	gfarm_int32_t fd, n, i;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode, *entry_inode;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	struct dir_result_rec {
		char *s;
		gfarm_ino_t inum;
		gfarm_int32_t type;
	} *p = NULL;
	gfarm_off_t dir_offset = 0;
	char *key, *name;
	int keylen, namelen, ok;

	e = gfm_server_get_request(peer, "getdirents", "i", &n);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (n > GFM_PROTO_MAX_DIRENT)
		n = GFM_PROTO_MAX_DIRENT;

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if ((dir = inode_get_dir(inode)) == NULL)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = process_get_dir_key(process, peer, fd,
		    &key, &keylen)) != GFARM_ERR_NO_ERROR)
		;
	else if (key == NULL &&
		 (e = process_get_dir_offset(process, peer, fd,
		    &dir_offset)) != GFARM_ERR_NO_ERROR)
		;
	else if (n <= 0)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if (GFARM_MALLOC_ARRAY(p,  n) == NULL)
		e = GFARM_ERR_NO_MEMORY;
	else {
		if (key != NULL)
			ok = dir_cursor_lookup(dir, key, strlen(key), &cursor);
		else
			ok = 0;
		if (!ok)
			ok = dir_cursor_set_pos(dir, dir_offset, &cursor);
		if (!ok)
			n = 0; /* end of directory? */
		for (i = 0; i < n; ) {
			entry = dir_cursor_get_entry(dir, &cursor);
			if (entry == NULL)
				break;
			name = dir_entry_get_name(entry, &namelen);
			GFARM_MALLOC_ARRAY(p[i].s, namelen + 1);
			if (p[i].s == NULL)
				break;
			memcpy(p[i].s, name, namelen);
			p[i].s[namelen] = '\0';
			entry_inode = dir_entry_get_inode(entry);
			p[i].inum = inode_get_number(entry_inode);
			p[i].type = inode_is_dir(entry_inode) ?
			    GFS_DT_DIR : GFS_DT_REG;

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		n = i;
		if (n > 0)
			inode_accessed(inode);

		/* remember current position */
		if (!ok ||
		    (entry = dir_cursor_get_entry(dir, &cursor)) == NULL) {
			process_clear_dir_key(process, peer, fd);
			dir_offset = dir_get_entry_count(dir);
		} else {
			name = dir_entry_get_name(entry, &namelen);
			process_set_dir_key(process, peer, fd,
			    name, namelen);
			dir_offset = dir_cursor_get_pos(dir, &cursor);
		}
		process_set_dir_offset(process, peer, fd, dir_offset);
	}
	
	giant_unlock();
	e2 = gfm_server_put_reply(peer, "getdirents", e, "i", n);
	if (e2 == GFARM_ERR_NO_ERROR && e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e2 = gfp_xdr_send(client, "sil",
			    p[i].s, p[i].type, p[i].inum);
			if (e2 != GFARM_ERR_NO_ERROR) {
				gflog_warning("%s@%s: getdirent: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e2));
				break;
			}
		}
	}
	if (p != NULL) {
		for (i = 0; i < n; i++)
			free(p[i].s);
		free(p);
	}
	return (e);
}

gfarm_error_t
gfm_server_seek(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, whence;
	gfarm_off_t offset, current, max;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	Dir dir;

	e = gfm_server_get_request(peer, "seek", "li", &offset, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else if ((dir = inode_get_dir(inode)) == NULL)
		e = GFARM_ERR_NOT_A_DIRECTORY;
	else if ((e = process_get_dir_offset(process, peer, fd,
	    &current)) != GFARM_ERR_NO_ERROR)
		;
	else if (whence < 0 || whence > 2)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else {
		max = dir_get_entry_count(dir);
		switch (whence) {
		case 0: break;
		case 1: offset += current; break;
		case 2: offset += max; break;
		default: assert(0);
		}
		if (offset != current) {
			if (offset > max)
				offset = max;
			process_clear_dir_key(process, peer, fd);
			process_set_dir_offset(process, peer, fd,
			    offset);
		}
	}
	
	giant_unlock();
	return (gfm_server_put_reply(peer, "seek", e, "l", offset));
}

gfarm_error_t
gfm_server_reopen(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct host *spool_host;
	struct process *process;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_int32_t mode, flags, to_create;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = process_reopen_file(process, peer, spool_host, fd,
		    &inum, &gen, &mode, &flags, &to_create);

	giant_unlock();
	return (gfm_server_put_reply(peer, "reopen", e, "lliii",
	    inum, gen, mode, flags, to_create));
}

gfarm_error_t
gfm_server_close_read(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "close_read", "li",
	    &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_close_file_read(process, peer, fd,
	    &atime)) != GFARM_ERR_NO_ERROR)
		;
	else
		e = peer_fdpair_close_current(peer);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close_read", e, ""));
}

gfarm_error_t
gfm_server_close_write(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "close_write", "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_close_file_write(process, peer,
	    fd, size, &atime, &mtime)) != GFARM_ERR_NO_ERROR)
		;
	else
		e = peer_fdpair_close_current(peer);

	giant_unlock();
	return (gfm_server_put_reply(peer, "close_write", e, ""));
}

gfarm_error_t
gfm_server_lock(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("lock: not implemented");

	e = gfm_server_get_request(peer, "lock", "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "lock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_trylock(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("trylock: not implemented");

	e = gfm_server_get_request(peer, "trylock", "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "trylock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_unlock(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("unlock: not implemented");

	e = gfm_server_get_request(peer, "unlock", "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "unlock",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_lock_info(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("lock_info: not implemented");

	e = gfm_server_get_request(peer, "lock_info", "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, "lock_info",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_glob(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("glob: not implemented");

	e = gfm_server_put_reply(peer, "glob",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_schedule(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("schedule: not implemented");

	e = gfm_server_put_reply(peer, "schedule",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_open(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_open: not implemented");

	e = gfm_server_put_reply(peer, "pio_open",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_set_paths(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_set_paths: not implemented");

	e = gfm_server_put_reply(peer, "pio_set_paths",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_close(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_close: not implemented");

	e = gfm_server_put_reply(peer, "pio_close",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_visit(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("pio_visit: not implemented");

	e = gfm_server_put_reply(peer, "pio_visit",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_list_by_name(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host;
	struct process *process;
	int fd, i;
	gfarm_int32_t n;
	struct inode *inode;
	char **hosts;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR)
		;
	else 
		e = inode_replica_list_by_name(inode, &n, &hosts);

	giant_unlock();
	e = gfm_server_put_reply(peer, "replica_list_by_name", e, "i", n);
	if (e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e = gfp_xdr_send(peer_get_conn(peer), "s", hosts[i]);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
		while (--i >= 0)
			free(hosts[i]);
		free(hosts);
	}
	return (e);
}

gfarm_error_t
gfm_server_replica_list_by_host(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_list_by_host: not implemented");

	e = gfm_server_get_request(peer, "replica_list_by_host", "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = gfm_server_put_reply(peer, "replica_list_by_host",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_remove_by_host(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;

	/* XXX - NOT IMPLEMENTED */
	gflog_error("replica_remove_by_host: not implemented");

	e = gfm_server_get_request(peer, "replica_remove_by_host", "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = gfm_server_put_reply(peer, "replica_remove_by_host",
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_adding(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_int64_t mtime_sec;
	gfarm_int32_t fd, mtime_nsec;
	struct host *spool_host;
	struct process *process;
	char *src_host;

	e = gfm_server_get_request(peer, "replica_adding", "s", &src_host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(src_host);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = process_replica_adding(process, peer, spool_host,
		    src_host, fd, &inum, &gen, &mtime_sec, &mtime_nsec);

	free(src_host);
	giant_unlock();
	return (gfm_server_put_reply(peer, "replica_adding", e, "llli",
	    inum, gen, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_server_replica_added(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, flags, mtime_nsec;
	gfarm_int64_t mtime_sec;
	struct host *spool_host;
	struct process *process;

	e = gfm_server_get_request(peer, "replica_added", "ili",
	    &flags, &mtime_sec, &mtime_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else
		e = process_replica_added(process, peer, spool_host, fd,
		    flags, mtime_sec, mtime_nsec);

	giant_unlock();
	return (gfm_server_put_reply(peer, "replica_added", e, ""));
}

gfarm_error_t
gfm_server_replica_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct host *spool_host;
	struct inode *inode;

	e = gfm_server_get_request(peer, "replica_remove", "ll", &inum, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((inode = inode_lookup(inum)) == NULL)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else if (inode_get_gen(inode) != gen)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = inode_remove_replica(inode, spool_host);
	giant_unlock();

	return (gfm_server_put_reply(peer, "replica_remove", e, ""));
}

gfarm_error_t
gfm_server_replica_add(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct host *spool_host;
	struct inode *inode;

	e = gfm_server_get_request(peer, "replica_add", "ll", &inum, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) /* from gfsd only */
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((spool_host = peer_get_host(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((inode = inode_lookup(inum)) == NULL)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else if (inode_get_gen(inode) != gen)
		e = GFARM_ERR_NO_SUCH_OBJECT;
	else
		e = inode_add_replica(inode, spool_host, 1);
	giant_unlock();

	return (gfm_server_put_reply(peer, "replica_add", e, ""));
}

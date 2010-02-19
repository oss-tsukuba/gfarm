#include <assert.h>
#include <stdarg.h> /* for "gfp_xdr.h" */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "gfp_xdr.h"
#include "auth.h"

#include "gfm_proto.h"

#include "subr.h"
#include "db_access.h"
#include "host.h"
#include "user.h"
#include "group.h"
#include "dir.h"
#include "inode.h"
#include "process.h"
#include "peer.h"
#include "back_channel.h"
#include "fs.h"

gfarm_error_t
gfm_server_compound_begin(struct peer *peer, int from_client, int skip,
	int level)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "GFM_PROTO_COMPOUND_BEGIN";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level > 0) /* We don't allow nesting */
		e = GFARM_ERR_INVALID_ARGUMENT;
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_compound_end(struct peer *peer, int from_client, int skip,
	int level)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	static const char diag[] = "GFM_PROTO_COMPOUND_END";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level < 1) /* nesting doesn't match */
		e = GFARM_ERR_INVALID_ARGUMENT;
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_compound_on_error(struct peer *peer, int from_client, int skip,
	int level, gfarm_error_t *on_errorp)
{
	gfarm_error_t e, on_error;
	static const char diag[] = "GFM_PROTO_COMPOUND_ON_ERROR";

	e = gfm_server_get_request(peer, diag, "i", &on_error);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"compound_on_error request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (level < 1) /* there isn't COMPOUND_BEGIN ... END block around */
		e = GFARM_ERR_INVALID_ARGUMENT;
	else 
		*on_errorp = on_error;
	return (gfm_server_put_reply(peer, diag, GFARM_ERR_NO_ERROR, ""));
}

gfarm_error_t
gfm_server_get_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	static const char diag[] = "GFM_PROTO_GET_FD";

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"get_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
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
	
	return (gfm_server_put_reply(peer, diag, e, "i", fd));
}

gfarm_error_t
gfm_server_put_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	static const char diag[] = "GFM_PROTO_GET_FD";

	e = gfm_server_get_request(peer, diag, "i", &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"put_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
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
	
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_save_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	static const char diag[] = "GFM_PROTO_SAVE_FD";

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"save_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = peer_fdpair_save(peer);
	giant_unlock();
	
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_restore_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	static const char diag[] = "GFM_PROTO_RESTORE_FD";

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "restore_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = peer_fdpair_restore(peer);
	giant_unlock();
	
	return (gfm_server_put_reply(peer, diag, e, ""));
}

/* this assumes that giant_lock is already acquired */
gfarm_error_t
gfm_server_open_common(const char *diag, struct peer *peer, int from_client,
	char *name, gfarm_int32_t flag, int to_create, gfarm_int32_t mode,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_int32_t *modep)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	int op;
	struct inode *base, *inode;
	int created, transaction = 0;;
	gfarm_int32_t cfd, fd = -1;

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: process_get_user() "
			"failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((e = peer_fdpair_get_current(peer, &cfd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "process_get_file_inode() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	if (flag & ~GFARM_FILE_USER_MODE) {
		gflog_debug(GFARM_MSG_UNFIXED, "argument 'flag' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	op = accmode_to_op(flag);

	if (to_create) {
		if (mode & ~GFARM_S_ALLPERM) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"argument 'mode' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		e = db_begin(diag);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		transaction = 1;
		e = inode_create_file(base, name, process, op, mode, &inode,
		    &created);
	} else {
		e = inode_lookup_by_name(base, name, process, op, &inode);
		created = 0;
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = process_open_file(process, inode, flag, created, peer,
		    spool_host, &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"error occurred during process: %s",
			gfarm_error_string(e));
		if (transaction)
			db_end(diag);
		return (e);
	}
	if (created && !from_client) {
		e = inode_add_replica(inode, spool_host, 1);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"inode_add_replica() failed: %s",
				gfarm_error_string(e));
			process_close_file(process, peer, fd);
			inode_unlink(base, name, process);
			if (transaction)
				db_end(diag);
			return (e);
		}
	}
	if (transaction)
		db_end(diag);
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
	static const char diag[] = "GFM_PROTO_CREATE";

	e = gfm_server_get_request(peer, diag, "sii", &name, &flag, &perm);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "create request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	e = gfm_server_open_common(diag, peer, from_client,
	    name, flag, 1, perm, &inum, &gen, &mode);

	if (debug_mode) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1000376,
			    "create(%s) -> error: %s",
			    name, gfarm_error_string(e));
		} else {
			gfarm_int32_t fd;
			peer_fdpair_get_current(peer, &fd);
			gflog_info(GFARM_MSG_1000377,
			    "create(%s) -> %d, %" GFARM_PRId64
			    ":%" GFARM_PRId64 ", %3o",
			    name, fd, inum, gen, mode);
		}
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "lli",
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
	static const char diag[] = "GFM_PROTO_OPEN";

	e = gfm_server_get_request(peer, "open", "si", &name, &flag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "open request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	e = gfm_server_open_common(diag, peer, from_client,
	    name, flag, 0, 0, &inum, &gen, &mode);

	if (debug_mode) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1000378, "open(%s) -> error: %s",
			    name, gfarm_error_string(e));
		} else {
			gfarm_int32_t fd;
			peer_fdpair_get_current(peer, &fd);
			gflog_info(GFARM_MSG_1000379,
			    "open(%s) -> %d, %" GFARM_PRId64
			    ":%" GFARM_PRId64 ", %3o",
			    name, fd, inum, gen, mode);
		}
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "lli", inum, gen, mode));
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
	static const char diag[] = "GFM_PROTO_OPEN_ROOT";

	e = gfm_server_get_request(peer, diag, "i", &flag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "open_root request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (flag & ~GFARM_FILE_USER_MODE)
		e = GFARM_ERR_INVALID_ARGUMENT;
	else if ((op = accmode_to_op(flag)) & GFS_W_OK)
		e = GFARM_ERR_IS_A_DIRECTORY;
	else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000380,
			    "open_root: from_client=%d, spool?:%d\n",
			    from_client, spool_host != NULL);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000381,
			    "get_process?:%d\n", process != NULL);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;

	} else if ((e = inode_lookup_root(process, op, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		if (debug_mode)
			gflog_info(GFARM_MSG_1000382,
			    "inode_lookup_root?:%s\n",
			    gfarm_error_string(e));
	} else if ((e = process_open_file(process, inode, flag, 0,
	    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_open_file() failed: %s",
			gfarm_error_string(e));
	} else
		peer_fdpair_set_current(peer, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_OPEN_PARENT";

	e = gfm_server_get_request(peer, diag, "i", &flag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"open_parent request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (flag & ~GFARM_FILE_USER_MODE) {
		gflog_debug(GFARM_MSG_UNFIXED, "argument 'flag' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((op = accmode_to_op(flag)) & GFS_W_OK) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode is a directory");
		e = GFARM_ERR_IS_A_DIRECTORY;
	} else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process()"
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &base)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = inode_lookup_parent(base, process, op, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_lookup_parent() failed"
			": %s", gfarm_error_string(e));
	} else if ((e = process_open_file(process, inode, flag, 0,
	    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_open_file() failed: "
			"%s", gfarm_error_string(e));
	} else
		peer_fdpair_set_current(peer, fd);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_close(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t fd = -1;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_CLOSE";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted : peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_close_file(process, peer, fd);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_verify_type(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_uint32_t type;
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000383, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "i", &type);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"verify_type request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_verify_type_not(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_uint32_t type;
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE_NOT";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000384, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "i", &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}


static gfarm_error_t
inode_get_stat(struct inode *inode, struct gfs_stat *st)
{
	st->st_ino = inode_get_number(inode);
	st->st_gen = inode_get_gen(inode);
	st->st_mode = inode_get_mode(inode);
	st->st_nlink = inode_get_nlink(inode);
	st->st_user = strdup(user_name(inode_get_user(inode)));
	st->st_group = strdup(group_name(inode_get_group(inode)));
	st->st_size = inode_get_size(inode);
	if (inode_is_file(inode))
		st->st_ncopy = inode_get_ncopy(inode);
	else
		st->st_ncopy = 1;
	st->st_atimespec = *inode_get_atime(inode);
	st->st_mtimespec = *inode_get_mtime(inode);
	st->st_ctimespec = *inode_get_ctime(inode);
	if (st->st_user == NULL || st->st_group == NULL) {
		if (st->st_user != NULL)
			free(st->st_user);
		if (st->st_group != NULL)
			free(st->st_group);
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'st_user' or 'st_group' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_fstat(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;
	static const char diag[] = "GFM_PROTO_FSTAT";

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	memset(&st, 0, sizeof(st));
#endif
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) ==
	    GFARM_ERR_NO_ERROR)
		e = inode_get_stat(inode, &st);

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "llilsslllilili",
	    st.st_ino, st.st_gen, st.st_mode, st.st_nlink,
	    st.st_user, st.st_group, st.st_size,
	    st.st_ncopy,
	    st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec, 
	    st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec, 
	    st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec);
	if (e == GFARM_ERR_NO_ERROR) {
		free(st.st_user);
		free(st.st_group);
	}
	return (e2);
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
	static const char diag[] = "GFM_PROTO_FUTIMES";

	e = gfm_server_get_request(peer, diag, "lili",
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "futimes request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (user != inode_get_user(inode) && !user_is_root(user) &&
	    (e = process_get_file_writable(process, peer, fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "permission denied");
		e = GFARM_ERR_PERMISSION_DENIED;
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		inode_set_atime(inode, &atime);
		inode_set_mtime(inode, &mtime);
		db_end(diag);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_FCHMOD";

	e = gfm_server_get_request(peer, diag, "i", &mode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "fchmod request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: process_get_user() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if (user != inode_get_user(inode) && !user_is_root(user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted for user");
	} else
		e = inode_set_mode(inode, mode);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_FCHOWN";

	e = gfm_server_get_request(peer, diag, "ss",
	    &username, &groupname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "fchown request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(username);
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: process_get_user() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if (*username != '\0' &&
	    ((new_user = user_lookup(username)) == NULL ||
	     user_is_invalidated(new_user))) {
		gflog_debug(GFARM_MSG_UNFIXED, "user is not found");
		e = GFARM_ERR_NO_SUCH_USER;
	} else if (*groupname != '\0' &&
	    ((new_group = group_lookup(groupname)) == NULL ||
	     group_is_invalidated(new_group))) {
		gflog_debug(GFARM_MSG_UNFIXED, "group is not found");
		e = GFARM_ERR_NO_SUCH_GROUP;
	} else if (new_user != NULL && !user_is_root(user)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted for user");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (new_group != NULL && !user_is_root(user) &&
	    (user != inode_get_user(inode) ||
	    !user_in_group(user, new_group))) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted for group");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_set_owner(inode, new_user, new_group);
		db_end(diag);
	}

	free(username);
	free(groupname);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_CKSUM_GET";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_cksum_get(process, peer, fd,
	    &cksum_type, &cksum_len, &cksum, &flags)) != GFARM_ERR_NO_ERROR) {
		/* We cannot access cksum_type and cksum outside of giant */
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_cksum_get() failed: %s",
			gfarm_error_string(e));
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
	e2 = gfm_server_put_reply(peer, diag, e, "sbi",
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
	static const char diag[] = "GFM_PROTO_CKSUM_SET";

	e = gfm_server_get_request(peer, diag, "sbili",
	    &cksum_type, sizeof(cksum), &cksum_len, cksum, &flags,
	    &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"cksum_set request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(cksum_type);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_host() failed");
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = process_cksum_set(process, peer, fd,
		    cksum_type, cksum_len, cksum, flags, &mtime);
		db_end(diag);
	}

	free(cksum_type);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_SCHEDULE_FILE";

	e = gfm_server_get_request(peer, diag, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"schedule_file request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (*domain != '\0') {
		gflog_debug(GFARM_MSG_UNFIXED,
			"function not implemented");
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED; /* XXX FIXME */
	} else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode is not file");
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
	} else {
		/* XXX FIXME too long giant lock */
		e = inode_schedule_file_reply(inode, peer,
		    process_get_file_writable(process, peer, fd)
		    == GFARM_ERR_NO_ERROR,
		    inode_is_creating_file(inode), diag);

		free(domain);
		giant_unlock();
		return (e);
	}

	assert(e != GFARM_ERR_NO_ERROR);
	free(domain);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_schedule_file_with_program(struct peer *peer, int from_client,
	int skip)
{
	gfarm_error_t e;
	char *domain;
	static const char diag[] = "GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000385,
	    "schedule_file_with_program: not implemented");

	e = gfm_server_get_request(
		peer, diag, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"schedule_file_with_program request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}

	free(domain);
	e = gfm_server_put_reply(peer, diag,
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
	static const char diag[] = "GFM_PROTO_REMOVE";

	e = gfm_server_get_request(peer, diag, "s", &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"remove request failed: %s", gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_unlink(base, name, process);
		db_end(diag);
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_rename(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *sname, *dname;
	struct process *process;
	gfarm_int32_t sfd, dfd;
	struct inode *sdir, *ddir;
	static const char diag[] = "GFM_PROTO_RENAME";

	e = gfm_server_get_request(peer, diag, "ss", &sname, &dname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "rename request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(sname);
		free(dname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_saved() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, sfd, &sdir))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, dfd, &ddir))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_rename(sdir, sname, ddir, dname, process);
		db_end(diag);
	}

	free(sname);
	free(dname);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_flink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t sfd, dfd;
	struct inode *src, *base;
	static const char diag[] = "GFM_PROTO_FLINK";

	e = gfm_server_get_request(peer, diag, "s", &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "flink request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_saved() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, sfd, &src))
	    != GFARM_ERR_NO_ERROR)  {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if (!inode_is_file(src)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode is not file");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, dfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
                e = inode_create_link(base, name, process, src);
		db_end(diag);
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	static const char diag[] = "GFM_PROTO_MKDIR";

	e = gfm_server_get_request(peer, diag, "si", &name, &mode);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "mkdir request failed :%s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if (mode & ~GFARM_S_ALLPERM) {
		gflog_debug(GFARM_MSG_UNFIXED, "argument 'mode' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_create_dir(base, name, process, mode);
		db_end(diag);
	}

	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_symlink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *source_path, *name;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *base;
	static const char diag[] = "GFM_PROTO_SYMLINK";

	e = gfm_server_get_request(peer, diag, "ss", &source_path, &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "symlink request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(source_path);
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_create_symlink(base, name, process, source_path);
		db_end(diag);
	}

	free(source_path);
	free(name);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_readlink(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	char *source_path = NULL;
	static const char diag[] = "GFM_PROTO_READLINK";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((source_path = inode_get_symlink(inode)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "invalid argument");
		e = GFARM_ERR_INVALID_ARGUMENT; /* not a symlink */
	} else if ((source_path = strdup(source_path)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocation of string failed");
		e = GFARM_ERR_NO_MEMORY;
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "s", source_path);
	if (e == GFARM_ERR_NO_ERROR)
		free(source_path);
	return (e2);
}

gfarm_error_t
gfm_server_getdirpath(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e_rpc;

	struct process *process;
	gfarm_int32_t cfd;
	struct inode *dir;
	char *s;
	static const char diag[] = "GFM_PROTO_GETDIRPATH";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &dir)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else {
		e = inode_getdirpath(dir, process, &s);
	}

	giant_unlock();
	e_rpc = gfm_server_put_reply(peer, diag, e, "s", s);
	if (e == GFARM_ERR_NO_ERROR)
		free(s);
	return (e_rpc);
}

static gfarm_error_t
fs_dir_get(struct peer *peer, int from_client,
	gfarm_int32_t *np, struct process **processp, gfarm_int32_t *fdp,
	struct inode **inodep, Dir *dirp, DirCursor *cursorp)
{
	gfarm_error_t e;
	gfarm_int32_t n = *np;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;
	Dir dir;
	char *key;
	int keylen, ok;
	gfarm_off_t dir_offset = 0;

	if (n > GFM_PROTO_MAX_DIRENT)
		n = GFM_PROTO_MAX_DIRENT;

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
		return (e);
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
		return (e);
	} else if ((dir = inode_get_dir(inode)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_get_dir() failed");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	} else if ((e = process_get_dir_key(process, peer, fd,
		    &key, &keylen)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_dir_key() failed: %s",
			gfarm_error_string(e));
		return (e);
	} else if (key == NULL &&
		 (e = process_get_dir_offset(process, peer, fd,
		    &dir_offset)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_dir_offset() failed: %s",
			gfarm_error_string(e));
		return (e);
	} else if (n <= 0) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"invalid argument");
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else {
		if (key != NULL)
			ok = dir_cursor_lookup(dir, key, strlen(key), cursorp);
		else
			ok = 0;
		if (!ok)
			ok = dir_cursor_set_pos(dir, dir_offset, cursorp);
		if (!ok)
			n = 0; /* end of directory? */
		*np = n;
		*processp = process;
		*fdp = fd;
		*inodep = inode;
		*dirp = dir;
		/* *cursorp = *cursorp; */
		return (GFARM_ERR_NO_ERROR);
	}
}

/* remember current position */
static void
fs_dir_remember_cursor(struct peer *peer, struct process *process,
	gfarm_int32_t fd, Dir dir, DirCursor *cursor, int eof)
{
	DirEntry entry;
	gfarm_off_t dir_offset;

	if (eof || (entry = dir_cursor_get_entry(dir, cursor)) == NULL) {
		process_clear_dir_key(process, peer, fd);
		dir_offset = dir_get_entry_count(dir);
	} else {
		int namelen;
		char *name = dir_entry_get_name(entry, &namelen);

		process_set_dir_key(process, peer, fd, name, namelen);
		dir_offset = dir_cursor_get_pos(dir, cursor);
	}
	process_set_dir_offset(process, peer, fd, dir_offset);
}

gfarm_error_t
gfm_server_getdirents(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e, e2;
	gfarm_int32_t fd, n, i;
	struct process *process;
	struct inode *inode, *entry_inode;
	Dir dir;
	DirCursor cursor;
	struct dir_result_rec {
		char *name;
		gfarm_ino_t inum;
		gfarm_int32_t type;
	} *p = NULL;
	static const char diag[] = "GFM_PROTO_GETDIRENTS";

	e = gfm_server_get_request(peer, diag, "i", &n);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "getdirents request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((e = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "fs_dir_get() failed: %s",
			gfarm_error_string(e));
	} else if (n > 0 && GFARM_MALLOC_ARRAY(p,  n) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocation of array failed");
		e = GFARM_ERR_NO_MEMORY;
	} else { /* note: (n == 0) means the end of the directory */
		for (i = 0; i < n; ) {
			if ((e = dir_cursor_get_name_and_inode(dir, &cursor,
			    &p[i].name, &entry_inode)) != GFARM_ERR_NO_ERROR ||
			    p[i].name == NULL)
				break;
			p[i].inum = inode_get_number(entry_inode);
			p[i].type =
			    gfs_mode_to_type(inode_get_mode(entry_inode));

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		if (e == GFARM_ERR_NO_ERROR) {
			fs_dir_remember_cursor(peer, process, fd, dir,
			    &cursor, n == 0);
			n = i;
			if (n > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
	}
	
	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	if (e2 == GFARM_ERR_NO_ERROR && e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e2 = gfp_xdr_send(client, "sil",
			    p[i].name, p[i].type, p[i].inum);
			if (e2 != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000386,
				    "%s@%s: getdirents: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e2));
				break;
			}
		}
	}
	if (p != NULL) {
		for (i = 0; i < n; i++)
			free(p[i].name);
		free(p);
	}
	return (e);
}

gfarm_error_t
gfm_server_getdirentsplus(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e, e2;
	gfarm_int32_t fd, n, i;
	struct process *process;
	struct inode *inode, *entry_inode;
	Dir dir;
	DirCursor cursor;
	struct dir_result_rec {
		char *name;
		struct gfs_stat st;
	} *p = NULL;
	static const char diag[] = "GFM_PROTO_GETDIRENTSPLUS";

	e = gfm_server_get_request(peer, diag, "i", &n);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"getdirentsplus request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((e = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor)) != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_UNFIXED, "fs_dir_get() failed: %s",
			gfarm_error_string(e));
	else if (n > 0 && GFARM_MALLOC_ARRAY(p,  n) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "allocation of array failed");
		e = GFARM_ERR_NO_MEMORY;
	} else { /* note: (n == 0) means the end of the directory */
		for (i = 0; i < n; ) {
			if ((e = dir_cursor_get_name_and_inode(dir, &cursor,
			    &p[i].name, &entry_inode)) != GFARM_ERR_NO_ERROR ||
			    p[i].name == NULL) {
				gflog_debug(GFARM_MSG_UNFIXED,
					"dir_cursor_get_name_and_inode() "
					"failed: %s",
					gfarm_error_string(e));
				break;
			}
			if ((e = inode_get_stat(entry_inode, &p[i].st)) !=
			    GFARM_ERR_NO_ERROR) {
				free(p[i].name);
				gflog_debug(GFARM_MSG_UNFIXED,
					"inode_get_stat() failed: %s",
					gfarm_error_string(e));
				break;
			}

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		if (e == GFARM_ERR_NO_ERROR) {
			fs_dir_remember_cursor(peer, process, fd, dir,
			    &cursor, n == 0);
			n = i;
			if (n > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
	}
	
	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	if (e2 == GFARM_ERR_NO_ERROR && e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			struct gfs_stat *st = &p[i].st;

			e2 = gfp_xdr_send(client, "sllilsslllilili",
			    p[i].name,
			    st->st_ino, st->st_gen, st->st_mode, st->st_nlink,
			    st->st_user, st->st_group, st->st_size,
			    st->st_ncopy,
			    st->st_atimespec.tv_sec, st->st_atimespec.tv_nsec, 
			    st->st_mtimespec.tv_sec, st->st_mtimespec.tv_nsec, 
			    st->st_ctimespec.tv_sec, st->st_ctimespec.tv_nsec);
			if (e2 != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000387,
				    "%s@%s: getdirentsplus: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e2));
				break;
			}
		}
	}
	if (p != NULL) {
		for (i = 0; i < n; i++) {
			free(p[i].name);
			gfs_stat_free(&p[i].st);
		}
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
	static const char diag[] = "GFM_PROTO_SEEK";

	e = gfm_server_get_request(peer, diag, "li", &offset, &whence);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "seek request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((dir = inode_get_dir(inode)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_get_dir() failed");
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if ((e = process_get_dir_offset(process, peer, fd,
	    &current)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_dir_offset() failed: %s",
			gfarm_error_string(e));
	} else if (whence < 0 || whence > 2) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"argument 'whence' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else {
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
	return (gfm_server_put_reply(peer, diag, e, "l", offset));
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
	static const char diag[] = "GFM_PROTO_REOPEN";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		e = process_reopen_file(process, peer, spool_host, fd,
		    &inum, &gen, &mode, &flags, &to_create);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "lliii",
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
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_CLOSE_READ";

	e = gfm_server_get_request(peer, diag, "li",
	    &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "close_read request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_close_file_read(process, peer, fd, &atime);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_close_write_common(const char *diag,
	struct peer *peer, int from_client,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	gfarm_int64_t *new_igenp, gfarm_int32_t *flagsp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	int transaction = 0;

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (peer_get_host(peer) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_close_file_write(process, peer, fd, size,
		    atime, mtime, new_igenp, flagsp);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
	}
	return (e);
}

gfarm_error_t
gfm_server_close_write(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	static const char diag[] = "GFM_PROTO_CLOSE_WRITE";

	e = gfm_server_get_request(peer, diag, "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	e = gfm_server_close_write_common(diag, peer, from_client, size,
	    &atime, &mtime, NULL, NULL);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_close_write_v2_4(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int64_t new_igen;
	gfarm_int32_t flags;
	static const char diag[] = "GFM_PROTO_CLOSE_WRITE_V2_4";

	e = gfm_server_get_request(peer, diag, "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	e = gfm_server_close_write_common(diag, peer, from_client, size,
	    &atime, &mtime, &new_igen, &flags);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "li", new_igen, flags));
}

gfarm_error_t
gfm_server_lock(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;
	static const char diag[] = "GFM_PROTO_LOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000388, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
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
	static const char diag[] = "GFM_PROTO_TRYLOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000389, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
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
	static const char diag[] = "GFM_PROTO_UNLOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000390, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
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
	static const char diag[] = "GFM_PROTO_LOCK_INFO";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000391, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "llii",
	    &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_glob(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_GLOB";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000392, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_schedule(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_SCHEDULE";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000393, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_open(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_PIO_OPEN";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000394, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_set_paths(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_PIO_SET_PATHS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000395, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_close(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_PIO_CLOSE";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000396, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_pio_visit(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_PIO_VISIT";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000397, "%s: not implemented", diag);

	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_list_by_name(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	struct host *spool_host;
	struct process *process;
	int fd, i;
	gfarm_int32_t n;
	struct inode *inode;
	char **hosts;
	static const char diag[] = "GFM_PROTO_REPLICA_LIST_BY_NAME";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else
		e = inode_replica_list_by_name(inode, &n, &hosts);

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	if (e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e = gfp_xdr_send(peer_get_conn(peer), "s", hosts[i]);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
		for (i = 0; i < n; ++i)
			free(hosts[i]);
		free(hosts);
	}
	return (e2);
}

gfarm_error_t
gfm_server_replica_list_by_host(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;
	static const char diag[] = "GFM_PROTO_REPLICA_LIST_BY_HOST";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000398, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = gfm_server_put_reply(peer, diag,
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
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE_BY_HOST";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000399, "%s: not implemented", diag);

	e = gfm_server_get_request(peer, diag, "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = gfm_server_put_reply(peer, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_replica_remove_by_file(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *hostname;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *inode;
	struct host *host, *spool_host;
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE_BY_FILE";

	e = gfm_server_get_request(peer, diag, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"replica_remove_by_file request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, cfd, &inode)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((host = host_lookup(hostname)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "host_lookup() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if ((spool_host = inode_writing_spool_host(inode)) != NULL &&
		spool_host == host) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode_writing_spool_host() failed");
		e = GFARM_ERR_TEXT_FILE_BUSY;
	} else {
		e = inode_remove_replica(inode, host, 1);
	}

	free(hostname);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_info_get(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	struct host *spool_host;
	struct process *process;
	int fd, i;
	gfarm_int32_t iflags, n;
	struct inode *inode;
	char **hosts;
	gfarm_int64_t *gens;
	gfarm_int32_t *oflags;
	static const char diag[] = "GFM_PROTO_REPLICA_INFO_GET";

	e = gfm_server_get_request(peer, diag, "i", &iflags);
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
	else 
		e = inode_replica_info_get(inode, iflags,
		    &n, &hosts, &gens, &oflags);

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	if (e == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e = gfp_xdr_send(peer_get_conn(peer), "sli",
			    hosts[i], gens[i], oflags[i]);
			if (e != GFARM_ERR_NO_ERROR)
				break;
		}
		for (i = 0; i < n; ++i)
			free(hosts[i]);
		free(hosts);
		free(gens);
		free(oflags);
	}
	return (e2);
}

gfarm_error_t
gfm_server_replicate_file_from_to(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *srchost;
	char *dsthost;
	gfarm_int32_t flags;
	struct process *process;
	gfarm_int32_t cfd;
	struct host *src, *dst;
	struct inode *inode;
	struct file_replicating *fr;
	int srcport;
	gfarm_ino_t ino;
	gfarm_int64_t gen;
	static const char diag[] = "GFM_PROTO_REPLICATE_FILE_FROM_TO";

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	dst = NULL;
	fr = NULL;
	srcport = 0;
	ino = 0;
	gen = 0;
#endif

	e = gfm_server_get_request(peer, diag, "ssi",
	    &srchost, &dsthost, &flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(srchost);
		free(dsthost);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((src = host_lookup(srchost)) == NULL)
		e = GFARM_ERR_UNKNOWN_HOST;
	else if ((dst = host_lookup(dsthost)) == NULL)
		e = GFARM_ERR_UNKNOWN_HOST;
	else if ((e = process_prepare_to_replicate(process, peer, src, dst,
	    cfd, flags, &inode)) != GFARM_ERR_NO_ERROR)
		;
	else if ((fr = file_replicating_new(inode, dst)) == NULL)
		e = GFARM_ERR_NO_MEMORY;
	else {
		srcport = host_port(src);
		ino = inode_get_number(inode);
		gen = inode_get_gen(inode);
	}

	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR) {
		e = async_back_channel_replication_request(srchost, srcport,
		    dst, ino, gen, fr);
		if (e != GFARM_ERR_NO_ERROR) {
			giant_lock();
			file_replicating_free(fr);
			giant_unlock();
		}
	}
	if (e != GFARM_ERR_NO_ERROR)
		free(srchost);
	free(dsthost);

	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_adding(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct gfarm_timespec *mtime;
	gfarm_int64_t mtime_sec;
	gfarm_int32_t fd, mtime_nsec;
	struct host *src, *spool_host;
	struct process *process;
	char *src_host;
	struct inode *inode;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDING";

	inum = 0;
	gen = 0;
	mtime_sec = 0;
	mtime_nsec = 0;

	e = gfm_server_get_request(peer, diag, "s", &src_host);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(src_host);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((src = host_lookup(src_host)) == NULL)
		e = GFARM_ERR_UNKNOWN_HOST;
	else if ((e = process_replica_adding(process, peer,
	    src, spool_host, fd, &inode)) != GFARM_ERR_NO_ERROR)
		;
	else {
		inum = inode_get_number(inode);
		gen = inode_get_gen(inode);
		mtime = inode_get_mtime(inode);
		mtime_sec = mtime->tv_sec;
		mtime_nsec = mtime->tv_nsec;
	}

	/* we don't maintain file_replicating in this case */

	free(src_host);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "llli",
	    inum, gen, mtime_sec, mtime_nsec));
}

/* assume giant lock is obtained */
gfarm_error_t
gfm_server_replica_added_common(const char *diag,
	struct peer *peer, int from_client,
	gfarm_int32_t flags, gfarm_int64_t mtime_sec,
	gfarm_int32_t mtime_nsec, gfarm_off_t size)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct host *spool_host;
	struct process *process;
	int transaction = 0;

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * the following internally calls process_close_file_read() and
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_replica_added(process, peer, spool_host, fd,
		    flags, mtime_sec, mtime_nsec, size);
		if (transaction)
			db_end(diag);
	}
	/* we don't maintain file_replicating in this case */
	return (e);
}

/* obsolete protocol */
gfarm_error_t
gfm_server_replica_added(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t flags, mtime_nsec;
	gfarm_int64_t mtime_sec;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED";

	e = gfm_server_get_request(peer, diag, "ili",
	    &flags, &mtime_sec, &mtime_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s request failed: %s",
			diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	e = gfm_server_replica_added_common(diag, peer, from_client,
		flags, mtime_sec, mtime_nsec, -1);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_added2(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t flags, mtime_nsec;
	gfarm_int64_t mtime_sec;
	gfarm_off_t size;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED2";

	e = gfm_server_get_request(peer, diag, "ilil",
	    &flags, &mtime_sec, &mtime_nsec, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s request failed: %s",
			diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	e = gfm_server_replica_added_common(diag, peer, from_client,
		flags, mtime_sec, mtime_nsec, size);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_remove(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct host *spool_host;
	struct inode *inode;
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE";

	e = gfm_server_get_request(peer, diag, "ll", &inum, &gen);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"replica_remove request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((inode = inode_lookup(inum)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_lookup() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (inode_get_gen(inode) != gen) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_get_gen() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		e = inode_remove_replica(inode, spool_host, 0);
	}
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_add(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_off_t size;
	struct host *spool_host;
	struct inode *inode;
	static const char diag[] = "GFM_PROTO_REPLICA_ADD";

	e = gfm_server_get_request(peer, diag, "lll",
		&inum, &gen, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"replica_add request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_UNFIXED,
			"not permitted : from_client");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((inode = inode_lookup(inum)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_lookup() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (inode_get_gen(inode) != gen) {
		gflog_debug(GFARM_MSG_UNFIXED, "inode_get_gen() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (inode_get_size(inode) != size) {
		gflog_debug(GFARM_MSG_UNFIXED, "invalid file replica");
		e = GFARM_ERR_INVALID_FILE_REPLICA;
	} else {
		e = inode_add_replica(inode, spool_host, 1);
	}
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
}

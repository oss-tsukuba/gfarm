#include <pthread.h>	/* db_access.h currently needs this */
#include <assert.h>
#include <stdarg.h> /* for "gfp_xdr.h" */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h> /* for sprintf(), snprintf() */
#include <sys/time.h> /* for gettimeofday() */
#include <limits.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"
#include "bool.h"

#include "context.h"
#include "patmatch.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"

#include "gfmd.h"
#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "host.h"
#include "mdhost.h"
#include "user.h"
#include "group.h"
#include "dead_file_copy.h"
#include "dir.h"
#include "inode.h"
#include "process.h"
#include "peer.h"
#include "back_channel.h"
#include "fs.h"
#include "acl.h"
#include "relay.h"
#include "config.h" /* for gfarm_host_get_self_name() */

static char dot[] = ".";
static char dotdot[] = "..";

gfarm_error_t
gfm_server_compound_begin(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int level)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_COMPOUND_BEGIN";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_COMPOUND_BEGIN, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		if (level > 0) /* We don't allow nesting */
			e = GFARM_ERR_INVALID_ARGUMENT;
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_compound_end(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int level)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_COMPOUND_END";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_COMPOUND_END, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		if (level < 1) /* nesting doesn't match */
			e = GFARM_ERR_INVALID_ARGUMENT;
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_compound_on_error(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int level, gfarm_error_t *on_errorp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR, on_error;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_COMPOUND_ON_ERROR";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_COMPOUND_ON_ERROR, "i", &on_error);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001784,
			"compound_on_error request failed: %s",
			gfarm_error_string(e));
		return (e);
	}

	/* XXX RELAY - needs special procedure */

	if (relay == NULL){
		if (level < 1) /* COMPOUND_BEGIN ... END block is not found */
			e = GFARM_ERR_INVALID_ARGUMENT;
		else
			*on_errorp = on_error;
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_get_fd(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	struct relayed_request *relay;
	int do_relay = 0;
	static const char diag[] = "GFM_PROTO_GET_FD";

	e = gfm_server_get_request(peer, sizep, diag, "");
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
	else if (mdhost_self_is_master() || FD_IS_SLAVE_ONLY(fd))
		e = peer_fdpair_externalize_current(peer);
	else
		do_relay = 1;
	giant_unlock();

	if (do_relay) {
		e = gfm_server_relay_put_request(peer, &relay, diag,
		    GFM_PROTO_GET_FD, "");
		if (e == GFARM_ERR_NO_ERROR)
			e = gfm_server_relay_get_reply(relay, diag, "i", &fd);
	}

	return (gfm_server_put_reply(peer, xid, sizep, diag,
	    e, "i", fd));
}

gfarm_error_t
gfm_server_put_fd(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	struct relayed_request *relay;
	int do_relay = 0;
	static const char diag[] = "GFM_PROTO_PUT_FD";

	e = gfm_server_get_request(peer, sizep, diag, "i", &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = process_verify_fd(process, fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else {
		peer_fdpair_set_current(peer, fd);
		e = peer_fdpair_externalize_current(peer);
		if (!mdhost_self_is_master() && !FD_IS_SLAVE_ONLY(fd))
			do_relay = 1;
	}
	giant_unlock();

	if (do_relay) {
		e = gfm_server_relay_put_request(peer, &relay, diag,
		    GFM_PROTO_PUT_FD, "i", fd);
		if (e == GFARM_ERR_NO_ERROR)
			e = gfm_server_relay_get_reply(relay, diag, "");
	}

	return (gfm_server_put_reply(peer, xid, sizep, diag,
	    e, ""));
}

gfarm_error_t
gfm_server_save_fd(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	struct relayed_request *relay;
	gfarm_int32_t fd;
	int do_relay = 0;
	static const char diag[] = "GFM_PROTO_SAVE_FD";

	e = gfm_server_get_request(peer, sizep, diag, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd))
	    != GFARM_ERR_NO_ERROR)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else {
		e = peer_fdpair_save(peer);
		/*
		 * XXX: FIXME
		 * more error check is needed, when COMPOUND starts to work
		 */
		if (!mdhost_self_is_master() && !FD_IS_SLAVE_ONLY(fd))
			do_relay = 1;
	}
	giant_unlock();

	if (do_relay) {
		e = gfm_server_relay_put_request(peer, &relay, diag,
		    GFM_PROTO_SAVE_FD, "");
		if (e == GFARM_ERR_NO_ERROR)
			e = gfm_server_relay_get_reply(relay, diag, "");
	}

	return (gfm_server_put_reply(peer, xid, sizep, diag,
	    e, ""));
}

gfarm_error_t
gfm_server_restore_fd(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	struct relayed_request *relay;
	gfarm_int32_t fd;
	int do_relay = 0;
	static const char diag[] = "GFM_PROTO_RESTORE_FD";

	e = gfm_server_get_request(peer, sizep, diag, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_saved(peer, &fd))
	    != GFARM_ERR_NO_ERROR)
		e = GFARM_ERR_BAD_FILE_DESCRIPTOR;
	else {
		e = peer_fdpair_restore(peer);
		/*
		 * XXX: FIXME
		 * more error check is needed, when COMPOUND starts to work
		 */
		if (!mdhost_self_is_master() && !FD_IS_SLAVE_ONLY(fd))
			do_relay = 1;
	}
	giant_unlock();

	if (do_relay) {
		e = gfm_server_relay_put_request(peer, &relay, diag,
		    GFM_PROTO_RESTORE_FD, "");
		if (e == GFARM_ERR_NO_ERROR)
			e = gfm_server_relay_get_reply(relay, diag, "");
	}

	return (gfm_server_put_reply(peer, xid, sizep, diag,
	    e, ""));
}

char *
trace_log_get_operation_name(int hlink_removed, gfarm_mode_t imode_removed)
{
	if (hlink_removed) {
		return ("DELLINK");
	} else if (GFARM_S_ISREG(imode_removed)) {
		return ("DELETE");
	} else if (GFARM_S_ISLNK(imode_removed)) {
		return ("DELSYMLINK");
	} else {
		return ("DELUNKNOWN");
	}
}

/* this assumes that giant_lock is already acquired */
/*
 * create_log and remove_log is malloc(3)ed string,
 * thus caller should free(3) the memory.
 */
gfarm_error_t
gfm_server_open_common(const char *diag, struct peer *peer, int from_client,
	char *name, gfarm_int32_t flag, int to_create, gfarm_int32_t mode,
	gfarm_ino_t *inump, gfarm_uint64_t *genp, gfarm_int32_t *modep,
	char **create_log, char **remove_log)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	int op;
	struct inode *base, *inode;
	int created, transaction = 0;;
	gfarm_int32_t cfd, fd = GFARM_DESCRIPTOR_INVALID;
	char *repattr;
	int desired_number;

	/* for gfarm_file_trace */
	int path_len = 0;
	int hlink_removed = 0;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;
#	define BUFSIZE_MAX 2048
	char tmp_str[BUFSIZE_MAX];
	char *parent_path, *child_path;
	int peer_port;
	struct inode_trace_log_info inodet;

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001789,
			"operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001790,
			"operation is not permitted: peer_get_process() "
			"failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001791,
			"operation is not permitted: process_get_user() "
			"failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((e = peer_fdpair_get_current(peer, &cfd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001792,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if ((e = process_get_file_inode(process, cfd, &base))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001793,
		    "process_get_file_inode() failed: %s",
		    gfarm_error_string(e));
		return (e);
	}

	if (flag & ~GFARM_FILE_USER_MODE) {
		gflog_debug(GFARM_MSG_1001794, "argument 'flag' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	op = accmode_to_op(flag);

	if (to_create) {
		if (mode & ~GFARM_S_ALLPERM) {
			gflog_debug(GFARM_MSG_1001795,
				"argument 'mode' is invalid");
			return (GFARM_ERR_INVALID_ARGUMENT);
		}
		e = db_begin(diag);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001796, "db_begin() failed: %s",
				gfarm_error_string(e));
			return (e);
		}
		transaction = 1;
		e = inode_create_file(base, name, process, op, mode,
		    flag & GFARM_FILE_EXCLUSIVE, &inode, &created);

		if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
			trace_seq_num = trace_log_get_sequence_number();
			gettimeofday(&tv, NULL);
			if ((e = process_get_path_for_trace_log(process,
			    cfd, &parent_path)) != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003286,
				    "process_get_path_for_trace_log() failed: %s",
				    gfarm_error_string(e));
			} else {
				peer_port = 0;
				peer_get_port(peer, &peer_port);
				snprintf(tmp_str, sizeof(tmp_str),
				    "%lld/%010ld.%06ld/%s/%s/%d/CREATE/%s/%d//%lld/%lld////\"%s/%s\"///",
				    (unsigned long long)trace_seq_num,
				    (long int)tv.tv_sec,
				    (long int)tv.tv_usec,
				    peer_get_username(peer),
				    peer_get_hostname(peer), peer_port,
				    gfarm_host_get_self_name(),
				    gfmd_port,
				    (unsigned long long)inode_get_number(inode),
				    (unsigned long long)inode_get_gen(inode),
				    parent_path, name);
				*create_log = strdup(tmp_str);
				free(parent_path);
			}
		}
	} else {
		flag &= ~GFARM_FILE_EXCLUSIVE;
		e = inode_lookup_by_name(base, name, process, op, &inode);
		created = 0;
	}
	if (e == GFARM_ERR_NO_ERROR)
		e = process_open_file(process, inode, flag, created, peer,
		    spool_host, &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001797,
			"error occurred during process: %s",
			gfarm_error_string(e));
		if (transaction)
			db_end(diag);
		return (e);
	}
	if (created && !from_client) {
		e = inode_add_replica(inode, spool_host, 1);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001798,
				"inode_add_replica() failed: %s",
				gfarm_error_string(e));
			process_close_file(process, peer, fd, NULL);
			if (inode_unlink(base, name, process,
				&inodet, &hlink_removed) == GFARM_ERR_NO_ERROR) {
				if (gfarm_ctxp->file_trace) {
					trace_seq_num =
					    trace_log_get_sequence_number();
					gettimeofday(&tv, NULL);
					if ((e = process_get_path_for_trace_log(
					    process, cfd, &parent_path))
					    != GFARM_ERR_NO_ERROR) {
						gflog_error(GFARM_MSG_1003287,
						    "process_get_path_for_trace_log() failed: %s",
						    gfarm_error_string(e));
					} else {
						peer_port = 0;
						peer_get_port(peer, &peer_port);
						snprintf(tmp_str,
						    sizeof(tmp_str),
						    "%lld/%010ld.%06ld/%s/%s/%d/%s/%s/%d//%lld/%lld////\"%s/%s\"///",
						    (unsigned long long)trace_log_get_sequence_number(),
						    (long int)tv.tv_sec,
						    (long int)tv.tv_usec,
						    peer_get_username(peer),
						    peer_get_hostname(peer),
						    peer_port,
						    trace_log_get_operation_name(hlink_removed, inodet.imode),
						    gfarm_host_get_self_name(), gfmd_port,
						    (unsigned long long)inodet.inum,
						    (unsigned long long)inodet.igen,
						    parent_path, name);
						*remove_log =
						    strdup(tmp_str);
						free(parent_path);
					}
				}
			}
			if (transaction)
				db_end(diag);
			return (e);
		}
	}
	if (transaction)
		db_end(diag);

	if ((created || (op & GFS_W_OK) != 0 ||
	     (flag & GFARM_FILE_REPLICA_SPEC) != 0) && inode_is_file(inode)) {
		if (inode_get_replica_spec(inode, &repattr, &desired_number) ||
		    inode_search_replica_spec(base,
		    &repattr, &desired_number))
			(void)process_record_replica_spec(
			    process, fd, desired_number, repattr);
	}

	/* set full path to file_opening */
	if (gfarm_ctxp->file_trace) {
		if (strcmp(name, dot) != 0 && strcmp(name, dotdot) != 0) {
			if ((e = process_get_path_for_trace_log(
			    process, cfd, &parent_path))
			    != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003288,
				    "process_get_path_for_trace_log() failed: "
				    "%s", gfarm_error_string(e));
			}
		} else { /* XXX FIXME */
			gflog_warning(GFARM_MSG_1003289,
				"handling for path including '.' or '..' "
				"haven't be implemented: %s",
				gfarm_error_string(e));
		}
	}

	peer_fdpair_set_current(peer, fd);
	*inump = inode_get_number(inode);
	*genp = inode_get_gen(inode);
	*modep = inode_get_mode(inode);

	if (gfarm_ctxp->file_trace) {
		if (strcmp(name, dot) != 0 && strcmp(name, dotdot) != 0) {
			if (e == GFARM_ERR_NO_ERROR) {
				path_len = strlen(parent_path) + 1 +
				    strlen(name) + 1;
				GFARM_MALLOC_ARRAY(child_path, path_len);
				if (child_path == NULL) {
					gflog_warning(GFARM_MSG_1003290,
					    "%s: no memory", diag);
					free(parent_path);
					return (GFARM_ERR_NO_MEMORY);
				}
				snprintf(child_path, path_len, "%s/%s",
				     parent_path, name);
				free(parent_path);
				if ((e = process_set_path_for_trace_log(
				    process, fd, child_path))
				    != GFARM_ERR_NO_ERROR) {
					gflog_error(GFARM_MSG_1003291,
					    "process_set_path_for_trace_log() failed: %s",
					    gfarm_error_string(e));
					free(child_path);
					return (e);
				}
			} else {
				return (e);
			}
		} else { /* XXX FIXME */
			gflog_warning(GFARM_MSG_1003292,
			    "handling for path including '.' or '..' haven't be implemented: %s",
			    gfarm_error_string(e));
		}
	}

	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_create(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	char *name;
	gfarm_int32_t flag, perm;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0;
	char *create_log = NULL, *remove_log = NULL;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CREATE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CREATE, "sii", &name, &flag, &perm);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(name);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();
		e = gfm_server_open_common(diag, peer, from_client,
		    name, flag, 1, perm, &inum, &gen, &mode,
		    &create_log, &remove_log);

		if (debug_mode) {
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_info(GFARM_MSG_1000376,
				    "create(%s) -> error: %s",
				    name, gfarm_error_string(e));
			} else {
				gfarm_int32_t fd;
				peer_fdpair_get_current(peer, &fd);
				gflog_info(GFARM_MSG_1000377,
				    "create(%s) -> %d, %lld:%lld, %3o",
				    name, fd, (unsigned long long)inum,
				    (unsigned long long)gen, mode);
			}
		}

		free(name);
		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "lli", &inum, &gen, &mode);

	if (gfarm_ctxp->file_trace && create_log != NULL) {
		gflog_trace(GFARM_MSG_1003293, "%s", create_log);
		free(create_log);
	}
	if (gfarm_ctxp->file_trace && remove_log != NULL) {
		gflog_trace(GFARM_MSG_1003294, "%s", remove_log);
		free(remove_log);
	}

	return (e2);
}

gfarm_error_t
gfm_server_open(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	gfarm_uint32_t flag;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_OPEN";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_OPEN, "si", &name, &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(name);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();

		e = gfm_server_open_common(diag, peer, from_client,
		    name, flag, 0, 0, &inum, &gen, &mode, NULL, NULL);

		if (debug_mode) {
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_info(GFARM_MSG_1000378,
				    "open(%s) -> error: %s",
				    name, gfarm_error_string(e));
			} else {
				gfarm_int32_t fd;
				peer_fdpair_get_current(peer, &fd);
				gflog_info(GFARM_MSG_1000379,
				    "open(%s) -> %d, %lld:%lld, %3o",
				    name, fd, (unsigned long long)inum,
				    (unsigned long long)gen, mode);
			}
		}

		free(name);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "lli", &inum, &gen, &mode));
}

gfarm_error_t
gfm_server_open_root(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	struct process *process;
	int op;
	struct inode *inode;
	gfarm_uint32_t flag;
	gfarm_int32_t fd = GFARM_DESCRIPTOR_INVALID;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_OPEN_ROOT";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_OPEN_ROOT, "i", &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (flag & ~GFARM_FILE_USER_MODE)
			e = GFARM_ERR_INVALID_ARGUMENT;
		else if ((op = accmode_to_op(flag)) & GFS_W_OK)
			e = GFARM_ERR_IS_A_DIRECTORY;
		else if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
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
			gflog_debug(GFARM_MSG_1001802,
			    "process_open_file() failed: %s",
			    gfarm_error_string(e));
		} else
			peer_fdpair_set_current(peer, fd);

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_open_parent(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct host *spool_host = NULL;
	int op;
	struct process *process;
	gfarm_uint32_t flag;
	gfarm_int32_t cfd, fd = GFARM_DESCRIPTOR_INVALID;
	struct inode *base, *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_OPEN_PARENT";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_OPEN_PARENT, "i", &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (flag & ~GFARM_FILE_USER_MODE) {
			gflog_debug(GFARM_MSG_1001804,
			    "argument 'flag' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((op = accmode_to_op(flag)) & GFS_W_OK) {
			gflog_debug(GFARM_MSG_1001805, "inode is a directory");
			e = GFARM_ERR_IS_A_DIRECTORY;
		} else if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001806,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001807,
			    "operation is not permitted: peer_get_process()"
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001808,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &base)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001809,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = inode_lookup_parent(base, process, op, &inode))
			    !=  GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001810,
			    "inode_lookup_parent() failed"
			    ": %s", gfarm_error_string(e));
		} else if ((e = process_open_file(process, inode, flag, 0,
		    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001811,
			    "process_open_file() failed: "
			    "%s", gfarm_error_string(e));
		} else
			peer_fdpair_set_current(peer, fd);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_fhopen(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct process *process;
	struct inode *inode;
	gfarm_int32_t fd;
	gfarm_ino_t inum;
	gfarm_uint64_t igen;
	gfarm_uint32_t flag;
	gfarm_int32_t mode = 0;
	static const char diag[] = "GFM_PROTO_FHOPEN";

	e = gfm_server_get_request(peer, sizep, diag,
	    "lli", &inum, &igen, &flag);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* do not relay RPC to master gfmd */
	giant_lock();

	if (peer_get_parent(peer) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: not from slave gfmd", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (!from_client) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: not from client", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no process", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: no process", diag);
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	}
	if (flag != GFARM_FILE_LOOKUP) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: not lookup", diag);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	
	if ((inode = inode_lookup(inum)) == NULL) {
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: inode %lld:%lld: not found",
		    diag, (long long)inum, (long long)igen);
	} else if (inode_get_gen(inode) != igen) {
		e = GFARM_ERR_NO_SUCH_FILE_OR_DIRECTORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: inode %lld:%lld: generation not found: %lld",
		    diag, (long long)inum, (long long)igen,
		    (long long)inode_get_gen(inode));
	} else if (!inode_is_dir(inode)) {
		e = GFARM_ERR_NOT_A_DIRECTORY;
		gflog_debug(GFARM_MSG_UNFIXED,
		    "%s: inode %lld:%lld: not dir: %d",
		    diag, (long long)inum, (long long)igen,
		    inode_get_mode(inode));
	} else if ((e = process_open_file(process, inode, flag, 0, peer, NULL,
	    &fd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: process_open_file(): %s",
		    diag, gfarm_error_string(e));
	} else {
		peer_fdpair_set_current(peer, fd);
		mode = inode_get_mode(inode);
#if 0		/* XXX: */
		if (gfarm_ctxp->file_trace) {
		}
#endif
	}

	giant_unlock();

	return (gfm_server_put_reply(peer, xid, sizep, diag, e, "i", mode));
}

gfarm_error_t
gfm_server_close(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t fd = GFARM_DESCRIPTOR_INVALID;
	int transaction = 0;
	char *trace_log = NULL;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CLOSE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CLOSE, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001812,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001813,
			    "operation is not permitted : peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001814,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			/*
			 * closing must be done regardless of the result
			 * of db_begin().
			 * because not closing may cause descriptor leak.
			 */
			e = process_close_file(process, peer, fd, &trace_log);
			if (transaction)
				db_end(diag);
			if (e == GFARM_ERR_NO_ERROR) /* permission ok */
				e = peer_fdpair_close_current(peer);
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, &e, "");
	if (gfarm_ctxp->file_trace && trace_log != NULL) {
		gflog_trace(GFARM_MSG_1003295, "%s", trace_log);
		free(trace_log);
	}
	return (e2);
}

static gfarm_error_t
gfm_server_verify_type_common(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip,
	int tf, const char *diag)
{
	gfarm_error_t e;
	struct process *process;
	gfarm_uint32_t type;
	gfarm_int32_t cfd;
	struct inode *inode;
	struct relayed_request *relay;
	gfarm_mode_t mode = 0;

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    tf ? GFM_PROTO_VERIFY_TYPE : GFM_PROTO_VERIFY_TYPE_NOT, "i", &type);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1002844,
			    "operation is not permitted : peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002845,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &inode)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002846,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else {
			mode = inode_get_mode(inode);
		}
		giant_unlock();

#define ERR_FOR_TYPE(m) \
	(GFARM_S_ISDIR(m) ? GFARM_ERR_IS_A_DIRECTORY : \
	GFARM_S_ISREG(m) ? GFARM_ERR_IS_A_REGULAR_FILE : \
	GFARM_S_ISLNK(m) ? GFARM_ERR_IS_A_SYMBOLIC_LINK : \
	GFARM_ERR_UNKNOWN)

		switch (type) {
		case GFS_DT_DIR:
			e = GFARM_S_ISDIR(mode) ?
			    (tf ? GFARM_ERR_NO_ERROR :
				GFARM_ERR_IS_A_DIRECTORY) :
			    (tf ? ERR_FOR_TYPE(mode) : GFARM_ERR_NO_ERROR);
			break;
		case GFS_DT_REG:
			e = GFARM_S_ISREG(mode) ?
			    (tf ? GFARM_ERR_NO_ERROR :
				GFARM_ERR_IS_A_REGULAR_FILE) :
			    (tf ? ERR_FOR_TYPE(mode) : GFARM_ERR_NO_ERROR);
			break;
		case GFS_DT_LNK:
			e = GFARM_S_ISLNK(mode) ?
			    (tf ? GFARM_ERR_NO_ERROR :
				GFARM_ERR_IS_A_SYMBOLIC_LINK) :
			    (tf ? ERR_FOR_TYPE(mode) : GFARM_ERR_NO_ERROR);
			break;
		default:
			e = GFARM_ERR_INVALID_ARGUMENT;
			break;
		}
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_verify_type(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE";
	return (gfm_server_verify_type_common(
	    peer, xid, sizep, from_client, skip, 1, diag));
}

gfarm_error_t
gfm_server_verify_type_not(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE_NOT";
	return (gfm_server_verify_type_common(
	    peer, xid, sizep, from_client, skip, 0, diag));
}

gfarm_error_t
gfm_server_revoke_gfsd_access(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct file_opening *fo = NULL;
	struct process *process = NULL;
	gfarm_pid_t pid_for_logging;
	gfarm_ino_t inum_for_logging;
	gfarm_uint64_t igen_for_logging;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REVOKE_GFSD_ACCESS";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REVOKE_GFSD_ACCESS, "i", &fd);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1002848,
			    "%s", gfarm_error_string(e));
		} else if ((process = peer_get_process(peer)) == NULL) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1002849,
			    "%s", gfarm_error_string(e));
		} else if ((e = process_get_file_opening(process, fd, &fo))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002850,
			    "%s", gfarm_error_string(e));
		} else if ((fo->flag & GFARM_FILE_ACCMODE) !=
		    GFARM_FILE_RDONLY) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1002851,
			    "%s", gfarm_error_string(e));
		} else if (fo->opener) {
			fo->u.f.spool_opener = NULL;
			fo->u.f.spool_host = NULL;
		}
		if (process != NULL)
			pid_for_logging = process_get_pid(process);
		else
			pid_for_logging = -1;
		if (fo != NULL) {
			inum_for_logging = inode_get_number(fo->inode);
			igen_for_logging = inode_get_gen(fo->inode);
		} else {
			inum_for_logging = -1;
			igen_for_logging = -1;
		}
		giant_unlock();

		/* connection problem in client side. worth logging. */
		gflog_info(GFARM_MSG_UNFIXED,
		    "%s:%s: pid:%lld fd:%d inode:%llu:%llu %s request: %s",
		    peer_get_username(peer), peer_get_hostname(peer),
		    (long long)pid_for_logging, (int)fd,
		    (long long)inum_for_logging, (long long)igen_for_logging,
		    diag, gfarm_error_string(e));
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

static gfarm_error_t
inode_get_stat(struct inode *inode, struct gfs_stat *st)
{
	static const char diag[] = "inode_get_stat";

	st->st_ino = inode_get_number(inode);
	st->st_gen = inode_get_gen(inode);
	st->st_mode = inode_get_mode(inode);
	st->st_nlink = inode_get_nlink(inode);
	st->st_user = strdup_log(user_name(inode_get_user(inode)), diag);
	st->st_group = strdup_log(group_name(inode_get_group(inode)), diag);
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
		gflog_debug(GFARM_MSG_1001816,
			"allocation of 'st_user' or 'st_group' failed");
		return (GFARM_ERR_NO_MEMORY);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_fstat(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FSTAT";

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	memset(&st, 0, sizeof(st));
#endif
	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FSTAT, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001817,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001818,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001819,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode)) ==
			   GFARM_ERR_NO_ERROR)
			e = inode_get_stat(inode, &st);

		giant_unlock();
	}

	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "llilsslllilili",
	    &st.st_ino, &st.st_gen, &st.st_mode, &st.st_nlink,
	    &st.st_user, &st.st_group, &st.st_size,
	    &st.st_ncopy,
	    &st.st_atimespec.tv_sec, &st.st_atimespec.tv_nsec,
	    &st.st_mtimespec.tv_sec, &st.st_mtimespec.tv_nsec,
	    &st.st_ctimespec.tv_sec, &st.st_ctimespec.tv_nsec);
	if (relay == NULL && e == GFARM_ERR_NO_ERROR) {
		free(st.st_user);
		free(st.st_group);
	}
	return (e2);
}

gfarm_error_t
gfm_server_recv_attrpatterns(struct peer *peer, size_t *sizep, int skip,
	gfarm_int32_t nattrpatterns, char ***attrpatternsp, const char *diag)
{
	gfarm_error_t e;
	struct gfp_xdr *client = peer_get_conn(peer);
	char **attrpatterns = NULL, *attrpattern;
	int i, j, eof;

	if (!skip) {
		GFARM_MALLOC_ARRAY(attrpatterns, nattrpatterns);
		/*
		 * NOTE: We won't return GFARM_ERR_NO_MEMORY,
		 * but returns GFARM_ERR_NO_ERROR with *attrpatternsp == NULL,
		 * to continue protocol processing even in no memory case.
		 */
	}
	for (i = 0; i < nattrpatterns; i++) {
		if (sizep == NULL)
			e = gfp_xdr_recv(client, 0, &eof, "s", &attrpattern);
		else
			e = gfp_xdr_recv_sized(client, 0, 1, sizep, &eof, "s",
			    &attrpattern);
		if (e != GFARM_ERR_NO_ERROR || eof) {
			gflog_debug(GFARM_MSG_1002496,
			    "%s: gfp_xdr_recv(xattrpattern) failed: %s",
			    diag, gfarm_error_string(e));
			if (e == GFARM_ERR_NO_ERROR) /* i.e. eof */
				e = GFARM_ERR_PROTOCOL;
			if (attrpatterns != NULL) {
				for (j = 0; j < i; j++) {
					if (attrpatterns[j] != NULL)
						free(attrpatterns[j]);
				}
				free(attrpatterns);
			}
			return (e);
		}
		if (attrpatterns == NULL) {
			free(attrpattern);
		} else {
			attrpatterns[i] = attrpattern;
		}
	}
	if (!skip)
		*attrpatternsp = attrpatterns;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_fgetattrplus(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
	gfarm_int32_t flags, nattrpatterns, fd;
	char **attrpatterns;
	int i, j, needs_free = 0;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;
	size_t nxattrs = 0;
	struct xattr_list *xattrs, *px;
	struct db_waitctx waitctx;
	static const char diag[] = "GFM_PROTO_FGETATTRPLUS";

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	memset(&st, 0, sizeof(st));
#endif

	e_ret = gfm_server_get_request(peer, sizep, diag, "ii",
	    &flags, &nattrpatterns);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);

	e_ret = gfm_server_recv_attrpatterns(peer, sizep, skip, nattrpatterns,
	    &attrpatterns, diag);
	/* don't have to free attrpatterns in the return case */
	if (e_ret != GFARM_ERR_NO_ERROR || skip)
		return (e_ret);

	/* NOTE: attrpatterns may be NULL here in case of memory shortage */

	if (attrpatterns == NULL) {
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else if ((e_rpc = wait_db_update_info(peer, DBUPDATE_XMLATTR, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
	}

	giant_lock();

	if (e_rpc != GFARM_ERR_NO_ERROR) {
		;
	} else if (!from_client &&
	    (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002497,
			"operation is not permitted");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002498,
			"operation is not permitted: peer_get_process() "
			"failed");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e_rpc = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002499,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e_rpc));
	} else if ((e_rpc = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		;
	} else if ((e_rpc = inode_get_stat(inode, &st)) !=
	    GFARM_ERR_NO_ERROR) {
		;
	} else if ((e_rpc = inode_xattr_list_get_cached_by_patterns(
	    st.st_ino, attrpatterns, nattrpatterns, &xattrs, &nxattrs))
	    != GFARM_ERR_NO_ERROR) {
		xattrs = NULL;
		nxattrs = 0;
		needs_free = 1;
	} else {
		needs_free = 1;
		for (j = 0; j < nxattrs; j++) {
			px = &xattrs[j];
			if (px->value == NULL) {
				/* not cached */
				db_waitctx_init(&waitctx);
				e_rpc = db_xattr_get(0, st.st_ino, px->name,
				    &px->value, &px->size, &waitctx);
				if (e_rpc == GFARM_ERR_NO_ERROR) {
					/*
					 * XXX this is slow, but we don't know
					 * the safe window size
					 */
					giant_unlock();
					e_rpc = dbq_waitret(&waitctx);
					giant_lock();
				}
				db_waitctx_fini(&waitctx);
				/* if error happens, px->value == NULL here */
				if (e_rpc != GFARM_ERR_NO_ERROR)
					break;
			}
			e_rpc = acl_convert_for_getxattr(inode, px->name,
			    &px->value, &px->size);
			if (e_rpc != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002852,
				    "acl_convert_for_getxattr() failed: %s",
				    gfarm_error_string(e_rpc));
				break;
			}
		}
	}

	giant_unlock();

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer,
	    xid, &size_pos, diag, e_rpc,
	    "llilsslllililii",
	    st.st_ino, st.st_gen, st.st_mode, st.st_nlink,
	    st.st_user, st.st_group, st.st_size,
	    st.st_ncopy,
	    st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec,
	    st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec,
	    st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec,
	    nxattrs);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (j = 0; j < nxattrs; j++) {
			px = &xattrs[j];
			e_ret = gfp_xdr_send(client, "sb",
			    px->name, px->size, px->value);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002501,
				    "%s@%s: %s returing xattr: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    diag, gfarm_error_string(e_ret));
				break;
			}
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	if (needs_free) {
		free(st.st_user);
		free(st.st_group);
		inode_xattr_list_free(xattrs, nxattrs);
	}
	if (attrpatterns != NULL) {
		for (i = 0; i < nattrpatterns; i++)
			free(attrpatterns[i]);
		free(attrpatterns);
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_futimes(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime, mtime;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FUTIMES";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FUTIMES, "lili",
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001820, "futimes request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001821,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001822,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001823,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001824,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((user = process_get_user(process)) == NULL) {
			gflog_debug(GFARM_MSG_1001825,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (user != inode_get_user(inode) &&
		    !user_is_root(inode, user) &&
			(e = process_get_file_writable(process, peer, fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001826, "permission denied");
			e = GFARM_ERR_PERMISSION_DENIED;
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001827, "db_begin() failed: %s",
			    gfarm_error_string(e));
		} else {
			if (atime.tv_nsec != GFARM_UTIME_OMIT)
				inode_set_atime(inode, &atime);
			if (mtime.tv_nsec != GFARM_UTIME_OMIT)
				inode_set_mtime(inode, &mtime);
			db_end(diag);
		}

		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_fchmod(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t mode;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FCHMOD";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FCHMOD, "i", &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001829,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001830,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((user = process_get_user(process)) == NULL) {
			gflog_debug(GFARM_MSG_1001831,
			    "operation is not permitted: process_get_user() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001832,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001833,
			    "process_get_file_inode() failed: %s",
			    gfarm_error_string(e));
		} else if (user != inode_get_user(inode) &&
			   !user_is_root(inode, user)) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1001834,
			    "operation is not permitted for user");
		} else {
			if (!user_is_root(inode, user)) {
				/* POSIX requirement for setgid-bit security */
				if ((mode & GFARM_S_ISGID) != 0 &&
				    !user_in_group(user,
				    inode_get_group(inode)))
					mode &= ~GFARM_S_ISGID;
				/* Solaris behavior. BSDs return EFTYPE */
				if ((mode & GFARM_S_ISTXT) != 0 &&
				    !inode_is_dir(inode))
					mode &= ~GFARM_S_ISTXT;
			}
			e = inode_set_mode(inode, mode);
		}

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_fchown(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *username, *groupname;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct user *user, *new_user = NULL;
	struct group *new_group = NULL;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FCHOWN";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FCHOWN, "ss", &username, &groupname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(username);
		free(groupname);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(username);
		free(groupname);

	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001836,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001837,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((user = process_get_user(process)) == NULL) {
			gflog_debug(GFARM_MSG_1001838,
			    "operation is not permitted: process_get_user() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001839,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001840,
			    "process_get_file_inode() failed: %s",
			    gfarm_error_string(e));
		} else if (*username != '\0' &&
			   (new_user = user_lookup(username)) == NULL) {
			gflog_debug(GFARM_MSG_1001841, "user is not found");
			e = GFARM_ERR_NO_SUCH_USER;
		} else if (*groupname != '\0' &&
			   (new_group = group_lookup(groupname)) == NULL) {
			gflog_debug(GFARM_MSG_1001842, "group is not found");
			e = GFARM_ERR_NO_SUCH_GROUP;
		} else if (new_user != NULL && !user_is_root(inode, user) &&
		    (user != inode_get_user(inode) ||
		    new_user != user)) {
			gflog_debug(GFARM_MSG_1001843,
			    "operation is not permitted for user");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (new_group != NULL && !user_is_root(inode, user) &&
		    (user != inode_get_user(inode) ||
		    !user_in_group(user, new_group))) {
			gflog_debug(GFARM_MSG_1001844,
			    "operation is not permitted for group");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001845, "db_begin() failed: %s",
			    gfarm_error_string(e));
		} else {
			e = inode_set_owner(inode, new_user, new_group);
			db_end(diag);
		}

		free(username);
		free(groupname);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_cksum_get(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	gfarm_int32_t flags = 0;
	size_t cksum_len = 0;
	struct host *spool_host = NULL;
	struct process *process;
	char *cksum_type = NULL, *cksumbuf = NULL, *cksum;
	int alloced = 0;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CKSUM_GET";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CKSUM_GET, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001846,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001847,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
				GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001848,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_cksum_get(process, peer, fd,
				&cksum_type, &cksum_len, &cksum, &flags)) !=
				GFARM_ERR_NO_ERROR) {
			/*
			 * We cannot access cksum_type and cksum
			 * outside of giant
			 */
			gflog_debug(GFARM_MSG_1001849,
			    "process_cksum_get() failed: %s",
			    gfarm_error_string(e));
		} else if (cksum_type == NULL) {
			cksum_type = "";
			cksum_len = 0;
			cksumbuf = "";
		} else if ((cksum_type = strdup_log(cksum_type, diag)) ==
		    NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else {
			GFARM_MALLOC_ARRAY(cksumbuf, cksum_len);
			if (cksumbuf == NULL) {
				e = GFARM_ERR_NO_MEMORY;
				free(cksum_type);
#if 1 /* shut up warning by Fortify */
				cksum_type = NULL;
#endif
			} else {
				memcpy(cksumbuf, cksum, cksum_len);
				alloced = 1;
			}
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "sbi", &cksum_type, sizeof(cksumbuf), &cksum_len, cksumbuf,
	    &flags);
	if (alloced) {
		free(cksum_type);
		free(cksumbuf);
	}
	return (e2);
}

gfarm_error_t
gfm_server_cksum_set(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	gfarm_int32_t flags;
	struct host *spool_host = NULL;
	struct process *process;
	char *cksum_type;
	size_t cksum_len;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	struct gfarm_timespec mtime;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CKSUM_SET";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CKSUM_SET, "sbili",
	    &cksum_type, sizeof(cksum), &cksum_len, cksum, &flags,
	    &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(cksum_type);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(cksum_type);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001851,
			    "operation is not permitted: from_client");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1001852,
			    "operation is not permitted: peer_get_host() "
			    "failed");
		} else if ((process = peer_get_process(peer)) == NULL) {
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
			gflog_debug(GFARM_MSG_1001853,
			    "operation is not permitted: peer_get_process() "
			    "failed");
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001854,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if (strlen(cksum_type) > GFM_PROTO_CKSUM_TYPE_MAXLEN ||
		    cksum_len > GFM_PROTO_CKSUM_MAXLEN) {
			gflog_debug(GFARM_MSG_1003480,
			    "%s: invalid cksum type:\"%s\" length: %d bytes",
			    diag, cksum_type, (int)cksum_len);
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001855, "db_begin() failed: %s",
			    gfarm_error_string(e));
		} else {
			e = process_cksum_set(process, peer, fd,
			    cksum_type, cksum_len, cksum, flags, &mtime);
			db_end(diag);
		}

		free(cksum_type);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

/*
 * A closure/context for GFM_PROTO_SCHEDULE_FILE request receiver and
 * replier.
 */
typedef struct {
	/*
	 * Filled in initialization:
	 */
	int from_client;

	/*
	 * Filled in request phase:
	 */
	gfarm_error_t req_error;
	char *domain;		/* malloc'd implicitly. */

	/*
	 * Filled in reply phase:
	 */
	gfarm_error_t rep_error;
	gfarm_int32_t nhosts;
	struct host **hosts;	/* malloc'd implicitly. */
} GFM_PROTO_SCHEDULE_FILE_context;

static void
GFM_PROTO_SCHEDULE_FILE_context_initialize(
	GFM_PROTO_SCHEDULE_FILE_context *cp,
	int from_client)
{
	cp->from_client = from_client;

	cp->req_error = GFARM_ERR_UNKNOWN;
	cp->domain = NULL;

	cp->rep_error = GFARM_ERR_UNKNOWN;
	cp->nhosts = 0;
	cp->hosts = NULL;
}

static void
GFM_PROTO_SCHEDULE_FILE_context_finalize(GFM_PROTO_SCHEDULE_FILE_context *cp)
{
	free(cp->domain);
	cp->domain = NULL;

	free(cp->hosts);
	cp->hosts = NULL;
}

/*
 * GFM_PROTO_SCHEDULE_FILE request receiver.
 */
static gfarm_error_t
GFM_PROTO_SCHEDULE_FILE_receive_request(
	enum request_reply_mode mode,
	struct peer *peer,
	size_t *sizep,
	int skip,
	struct relayed_request *r,
	void *closure,
	const char *diag)
{
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;
	GFM_PROTO_SCHEDULE_FILE_context *cp = 
		(GFM_PROTO_SCHEDULE_FILE_context *)closure;

	assert(cp != NULL);

	/*
	 * Note:
	 *	You don't have to worry about the mode in this case.
	 */
	ret = gfm_server_relay_get_request_dynarg(peer, sizep, skip, r, diag,
	    "s", &cp->domain);
	if (ret != GFARM_ERR_NO_ERROR)
		cp->req_error = ret;
	else if (cp->domain == NULL)
		cp->req_error = GFARM_ERR_NO_MEMORY;
	else if (*(cp->domain) != '\0')
		cp->req_error = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	else
		cp->req_error = GFARM_ERR_NO_ERROR;

	if (cp->req_error != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED, "%s: %s failed: %s",
			diag, 
			"GFM_PROTO_SCHEDULE_FILE_receive_request()",
			gfarm_error_string(cp->req_error));
	}
	return ret;
}

static gfarm_error_t
GFM_PROTO_SCHEDULE_FILE_send_reply(
	enum request_reply_mode mode,
	struct peer *peer,
	size_t *sizep,
	int skip,
	void *closure,
	const char *diag)
{
	GFM_PROTO_SCHEDULE_FILE_context *cp = 
		(GFM_PROTO_SCHEDULE_FILE_context *)closure;
	gfarm_error_t ret = GFARM_ERR_UNKNOWN;

	assert(cp != NULL);

	if (!skip) {
		gfarm_error_t rep_error = GFARM_ERR_UNKNOWN;
		gfarm_int32_t i;

		if (mode != RELAY_TRANSFER) {
			struct process *process = NULL;
			gfarm_int32_t fd = (gfarm_int32_t)-INT_MAX;

			if (cp->req_error != GFARM_ERR_NO_ERROR) {
				/*
				 * We already got an error in request
				 * phase. Just calculate a send size
				 * and return.
				 */
				rep_error = cp->req_error;
				goto calc_or_reply;
			}

			rep_error = wait_db_update_info(peer, DBUPDATE_HOST,
			    diag);
			if (rep_error != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
				    "%s: failed to wait for the backend DB"
				    "to be updated: %s",
				    diag, gfarm_error_string(rep_error));
				goto calc_or_reply;
			}

			/*
			 * Check validness of the parameters just ONCE
			 * in !RELAY_TRANSFER mode phase.
			 */
			giant_lock();

			if (!cp->from_client &&
				peer_get_host(peer) == NULL) {
				rep_error = GFARM_ERR_OPERATION_NOT_PERMITTED;
				gflog_error(GFARM_MSG_UNFIXED,
					"%s: %s failed: %s",
					diag,
					"peer_get_host()",
					gfarm_error_string(rep_error));
				goto unlock;
			}

			process = peer_get_process(peer);
			if (process == NULL) {
				rep_error = GFARM_ERR_OPERATION_NOT_PERMITTED;
				gflog_error(GFARM_MSG_UNFIXED,
					"%s: %s failed: %s",
					diag,
					"peer_get_process()",
					gfarm_error_string(rep_error));
				goto unlock;
			}

			rep_error = peer_fdpair_get_current(peer, &fd);
			if (rep_error != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s: %s failed: %s",
					diag,
					"peer_fdpair_get_current()",
					gfarm_error_string(rep_error));
				goto unlock;
			}

			rep_error = process_schedule_file(process, peer, fd,
				&cp->nhosts, &cp->hosts);
			if (rep_error != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s: %s failed: %s",
					diag,
					"process_schedule_file()",
					gfarm_error_string(rep_error));
				goto unlock;
			}

		unlock:
			giant_unlock();

		} else {
			/*
			 * Otherwise, we already checked the validness
			 * of parameters and the error code is stored
			 * in the context.
			 */
			rep_error = cp->rep_error;
		}

	calc_or_reply:
		ret = gfm_server_relay_put_reply_dynarg(peer, sizep, diag,
		    rep_error, "");
		if (ret != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
				"%s: %s failed: %s",
				diag,
				"gfm_server_relay_put_reply_dynarg()",
				gfarm_error_string(ret));
			goto done;
		}
		if (rep_error != GFARM_ERR_NO_ERROR) {
			/*
			 * Nothing to do anymore.
			 */
			goto done;
		}

		ret = gfm_server_relay_put_reply_arg_dynarg(peer, sizep, diag,
		    "i", cp->nhosts);
		if (ret != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
				"%s: %s failed: %s",
				diag,
				"gfm_server_relay_put_reply_arg_dynarg()",
				gfarm_error_string(ret));
			goto done;
		}

		giant_lock();
		for (i = 0; i < cp->nhosts; i++) {
			ret = host_schedule_reply_arg_dynarg(cp->hosts[i],
			    peer, sizep, diag);
			if (ret != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_UNFIXED,
					"%s: %s failed: %s",
					diag,
					"host_schedule_reply_arg_dynarg()",
					gfarm_error_string(ret));
				goto done;
			}
		}
		giant_unlock();

	done:
		cp->rep_error = rep_error;

	} else {
		ret = GFARM_ERR_NO_ERROR;
		cp->rep_error = ret;
	}

	return ret;
}

gfarm_error_t
gfm_server_schedule_file(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	GFM_PROTO_SCHEDULE_FILE_context c;
	static const char diag[] = "GFM_PROTO_SCHEDULE_FILE";

	GFM_PROTO_SCHEDULE_FILE_context_initialize(&c, from_client);
	e = gfm_server_relay_request_reply(peer, xid, skip,
	    GFM_PROTO_SCHEDULE_FILE_receive_request,
	    GFM_PROTO_SCHEDULE_FILE_send_reply,
	    GFM_PROTO_SCHEDULE_FILE, &c, diag);
	if (e != GFARM_ERR_NO_ERROR) { 
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s",
			diag, gfarm_error_string(e));
	} else {
		e = c.rep_error;
	}
	GFM_PROTO_SCHEDULE_FILE_context_finalize(&c);
	return (e);
}

gfarm_error_t
gfm_server_schedule_file_with_program(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *domain;
	static const char diag[] = "GFM_PROTO_SCHEDULE_FILE_WITH_PROGRAM";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000385,
	    "schedule_file_with_program: not implemented");

	e = gfm_server_get_request(
		peer, sizep, diag, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001863,
			"schedule_file_with_program request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(domain);
		return (GFARM_ERR_NO_ERROR);
	}

	free(domain);
	e = gfm_server_put_reply(peer, xid, sizep, diag,
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED, "");
	return (e != GFARM_ERR_NO_ERROR ? e :
	    GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

gfarm_error_t
gfm_server_remove(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	struct process *process = NULL;
	gfarm_int32_t cfd;
	struct inode *base;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	char *path;
	int hlink_removed = 0;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;
	int peer_port;
	struct inode_trace_log_info inodet;
	struct relayed_request *relay;

	static const char diag[] = "GFM_PROTO_REMOVE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REMOVE, "s", &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001865,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001866,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001867,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &base))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001868,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001869,
			    "db_begin() failed: %s",
			    gfarm_error_string(e));
		} else {
			e = inode_unlink(base, name, process, &inodet,
			    &hlink_removed);
			db_end(diag);
			if (gfarm_ctxp->file_trace &&
			    e == GFARM_ERR_NO_ERROR &&
			    !GFARM_S_ISDIR(inodet.imode)) {
				gettimeofday(&tv, NULL);
				trace_seq_num =
				    trace_log_get_sequence_number();
			}
		}

		giant_unlock();
	}

	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, &e, "");

	if (relay == NULL &&
	    gfarm_ctxp->file_trace && !GFARM_S_ISDIR(inodet.imode)) {
		if ((e = process_get_path_for_trace_log(process, cfd,
		    &path)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003296,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else {
			peer_port = 0;
			peer_get_port(peer, &peer_port);
			gflog_trace(GFARM_MSG_1003297,
			    "%lld/%010ld.%06ld/%s/%s/%d/%s/%s/%d//%lld/%lld"
			    "////\"%s/%s\"///",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    trace_log_get_operation_name(
			    hlink_removed, inodet.imode),
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (unsigned long long)inodet.inum,
			    (unsigned long long)inodet.igen,
			    path, name);
			free(path);
		}
	}

	free(name);
	return (e2);
}

gfarm_error_t
gfm_server_rename(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *sname, *dname;
	struct process *process = NULL;
	gfarm_int32_t sfd, dfd;
	struct inode *sdir, *ddir;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	char *spath, *dpath;
	int dst_removed = 0;
	int hlink_removed = 0;
	gfarm_uint64_t trace_seq_num_rename = 0, trace_seq_num_remove = 0;
	int peer_port;
	struct timeval tv;
	struct inode_trace_log_info srct, dstt;
	struct relayed_request *relay;

	static const char diag[] = "GFM_PROTO_RENAME";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_RENAME, "ss", &sname, &dname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(sname);
		free(dname);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001871,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001872,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001873,
			    "peer_fdpair_get_saved() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001874,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, sfd, &sdir))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001875,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, dfd, &ddir))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001876,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001877, "db_begin() failed: %s",
				    gfarm_error_string(e));
		} else {
			e = inode_rename(sdir, sname, ddir, dname, process,
			    &srct, &dstt, &dst_removed, &hlink_removed);
			if (gfarm_ctxp->file_trace &&
			    e == GFARM_ERR_NO_ERROR) {
				gettimeofday(&tv, NULL),
				trace_seq_num_rename =
				    trace_log_get_sequence_number();
				if (dst_removed)
					trace_seq_num_remove =
					    trace_log_get_sequence_number();
			}
			db_end(diag);
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, &e, "");

	if (relay == NULL &&
	    gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR &&
	    !GFARM_S_ISDIR(srct.imode)) {
		if ((e = process_get_path_for_trace_log(process, sfd,
		    &spath)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003298,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_path_for_trace_log(
		    process, dfd, &dpath)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003299,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
			free(spath);
		} else {
			peer_port = 0;
			peer_get_port(peer, &peer_port),
			gflog_trace(GFARM_MSG_1003300,
			    "%lld/%010ld.%06ld/%s/%s/%d/MOVE/%s/%d//%lld"
			    "/%lld////\"%s/%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num_rename,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (unsigned long long)srct.inum,
			    (unsigned long long)srct.igen,
			    spath, sname,
			    dpath, dname);
			if (dst_removed)
				gflog_trace(GFARM_MSG_1003301,
				    "%lld/%010ld.%06ld/%s/%s/%d/%sOW/%s/%d"
				    "//%lld/%lld////\"%s/%s\"///",
				    (unsigned long long)trace_seq_num_remove,
				    (long int)tv.tv_sec,
				    (long int)tv.tv_usec,
				    peer_get_username(peer),
				    peer_get_hostname(peer), peer_port,
				    trace_log_get_operation_name(
				    hlink_removed, dstt.imode),
				    gfarm_host_get_self_name(),
				    gfmd_port,
				    (unsigned long long)dstt.inum,
				    (unsigned long long)dstt.igen,
				    dpath, dname);
			free(spath);
			free(dpath);
		}
	}

	free(sname);
	free(dname);
	return (e2);
}

gfarm_error_t
gfm_server_flink(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	struct host *spool_host = NULL;
	struct process *process = NULL;
	gfarm_int32_t sfd, dfd;
	struct inode *src, *base;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	char *spath, *dpath;
	int peer_port;
	struct timeval tv;
	gfarm_int64_t trace_seq_num = 0;
	struct relayed_request *relay;

	static const char diag[] = "GFM_PROTO_FLINK";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FLINK, "s", &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001879,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001880,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001881,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001882,
			    "peer_fdpair_get_saved() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, sfd, &src))
			   != GFARM_ERR_NO_ERROR)  {
			gflog_debug(GFARM_MSG_1001883,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if (!inode_is_file(src)) {
			gflog_debug(GFARM_MSG_1001884,
			    "inode is not file");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001885,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, dfd, &base))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001886,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001887,
			    "db_begin() failed: %s", gfarm_error_string(e));
		} else {
			e = inode_create_link(base, name, process, src);
			db_end(diag);
			if (gfarm_ctxp->file_trace
			    && e == GFARM_ERR_NO_ERROR) {
				gettimeofday(&tv, NULL);
				trace_seq_num =
				    trace_log_get_sequence_number();
			}
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, &e, "");

	if (relay == NULL &&
	    gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		if ((e = process_get_path_for_trace_log(process, sfd, &spath))
			!= GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003302,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_path_for_trace_log(process, dfd,
				&dpath)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003303,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
			free(spath);
		} else {
			peer_port = 0;
			peer_get_port(peer, &peer_port),
			gflog_trace(GFARM_MSG_1003304,
			    "%lld/%010ld.%06ld/%s/%s/%d/LINK/%s/%d//%lld"
			    "/%lld////\"%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (unsigned long long)inode_get_number(src),
			    (unsigned long long)inode_get_gen(src),
			    spath,
			    dpath, name);
			free(spath);
			free(dpath);
		}
	}
	free(name);

	return (e2);
}

gfarm_error_t
gfm_server_mkdir(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *name;
	gfarm_int32_t mode;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *base;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_MKDIR";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_MKDIR, "si", &name, &mode);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001889,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001890,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001891,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &base))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001892,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if (mode & ~GFARM_S_ALLPERM) {
			gflog_debug(GFARM_MSG_1001893,
			    "argument 'mode' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001894, "db_begin() failed: %s",
			    gfarm_error_string(e));
		} else {
			e = inode_create_dir(base, name, process, mode);
			db_end(diag);
		}

		giant_unlock();
	}
	free(name);
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_symlink(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *source_path, *name;
	struct host *spool_host = NULL;
	struct process *process = NULL;
	gfarm_int32_t cfd;
	struct inode *base;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	char *dpath;
	int peer_port;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;
	struct inode_trace_log_info inodet;
	struct relayed_request *relay;

	static const char diag[] = "GFM_PROTO_SYMLINK";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SYMLINK, "ss", &source_path, &name);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(source_path);
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001896,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001897,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001898,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001899,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &base))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001900,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001901,
			    "db_begin() failed: %s", gfarm_error_string(e));
		} else {
			e = inode_create_symlink(base, name, process,
			     source_path, &inodet);
			db_end(diag);
			if (gfarm_ctxp->file_trace &&
			    e == GFARM_ERR_NO_ERROR) {
				gettimeofday(&tv, NULL);
				trace_seq_num =
				    trace_log_get_sequence_number();
			}
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, &e, "");

	if (relay == NULL &&
	    gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		gettimeofday(&tv, NULL);
		if ((e = process_get_path_for_trace_log(process, cfd,
		    &dpath)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003305,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else {
			peer_port = 0;
			peer_get_port(peer, &peer_port);
			gflog_trace(GFARM_MSG_1003306,
			    "%lld/%010ld.%06ld/%s/%s/%d/SYMLINK/%s/%d//%lld"
			    "/%lld////\"%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfmd_port,
			    (unsigned long long)inodet.inum,
			    (unsigned long long)inodet.igen,
			    source_path,
			    dpath, name);
			free(dpath);
		}
	}

	free(source_path);
	free(name);
	return (e2);
}

gfarm_error_t
gfm_server_readlink(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e2;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	char *source_path = NULL;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_READLINK";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_READLINK, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (!from_client &&
		    (spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001902,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001903,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001904,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001905,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((source_path = inode_get_symlink(inode)) == NULL) {
			gflog_debug(GFARM_MSG_1001906, "invalid argument");
			e = GFARM_ERR_INVALID_ARGUMENT; /* not a symlink */
		} else if ((source_path =
		    strdup_log(source_path, diag)) == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		}

		giant_unlock();
	}

	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "s", &source_path);
	if (relay == NULL && e == GFARM_ERR_NO_ERROR)
		free(source_path);
	return (e2);
}

gfarm_error_t
gfm_server_getdirpath(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e, e_rpc;

	struct process *process;
	gfarm_int32_t cfd;
	struct inode *dir;
	char *s = NULL;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_GETDIRPATH";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_GETDIRPATH, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001908,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001909,
			    "peer_fdpair_get_current() "
			    "failed: %s", gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &dir)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001910,
			    "process_get_file_inode() "
			    "failed: %s", gfarm_error_string(e));
		} else {
			e = inode_getdirpath(dir, process, &s);
		}

		giant_unlock();
	}
	e_rpc = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "s", &s);
	if (relay == NULL && e == GFARM_ERR_NO_ERROR)
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
		gflog_debug(GFARM_MSG_1001911, "operation is not permitted");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001912, "peer_get_process() failed");
		return (GFARM_ERR_OPERATION_NOT_PERMITTED);
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001913, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
		return (e);
	} else if ((e = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001914, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
		return (e);
	} else if ((dir = inode_get_dir(inode)) == NULL) {
		gflog_debug(GFARM_MSG_1001915, "inode_get_dir() failed");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	} else if ((e = process_get_dir_key(process, peer, fd,
		    &key, &keylen)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001916,
			"process_get_dir_key() failed: %s",
			gfarm_error_string(e));
		return (e);
	} else if (key == NULL &&
		 (e = process_get_dir_offset(process, peer, fd,
		    &dir_offset)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001917,
			"process_get_dir_offset() failed: %s",
			gfarm_error_string(e));
		return (e);
	} else if (n <= 0) {
		gflog_debug(GFARM_MSG_1001918,
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
gfm_server_getdirents(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
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

	e_ret = gfm_server_get_request(peer, sizep, diag, "i", &n);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e_rpc = wait_db_update_info(peer, DBUPDATE_FS_DIRENT, diag);
	if (e_rpc != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
		/* Continue processing. */
	}
	giant_lock();

	if (e_rpc != GFARM_ERR_NO_ERROR) {
		; /* Continue processing. */
	} else if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001920, "fs_dir_get() failed: %s",
			gfarm_error_string(e_rpc));
	} else if (n > 0 && GFARM_MALLOC_ARRAY(p,  n) == NULL) {
		gflog_debug(GFARM_MSG_1001921, "allocation of array failed");
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else { /* note: (n == 0) means the end of the directory */
		for (i = 0; i < n; ) {
			if ((e_rpc = dir_cursor_get_name_and_inode(dir, &cursor,
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
		if (e_rpc == GFARM_ERR_NO_ERROR) {
			fs_dir_remember_cursor(peer, process, fd, dir,
			    &cursor, n == 0);
			if (i > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
		n = i;
	}

	giant_unlock();

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", n);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			e_ret = gfp_xdr_send(client, "sil",
			    p[i].name, p[i].type, p[i].inum);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000386,
				    "%s@%s: getdirents: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e_ret));
				break;
			}
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	if (p != NULL) {
		for (i = 0; i < n; i++)
			free(p[i].name);
		free(p);
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_getdirentsplus(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
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

	e_ret = gfm_server_get_request(peer, sizep, diag, "i", &n);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e_rpc = wait_db_update_info(peer,
	    DBUPDATE_FS_DIRENT | DBUPDATE_USER | DBUPDATE_GROUP, diag);
	if (e_rpc != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
		/* Continue processing. */
	}
	giant_lock();

	if (e_rpc != GFARM_ERR_NO_ERROR) {
		; /* Continue processing. */
	} else if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001923, "fs_dir_get() failed: %s",
		    gfarm_error_string(e_rpc));
	} else if (n > 0 && GFARM_MALLOC_ARRAY(p,  n) == NULL) {
		gflog_debug(GFARM_MSG_1001924, "allocation of array failed");
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else { /* note: (n == 0) means the end of the directory */
		for (i = 0; i < n; ) {
			if ((e_rpc = dir_cursor_get_name_and_inode(dir, &cursor,
			    &p[i].name, &entry_inode)) != GFARM_ERR_NO_ERROR ||
			    p[i].name == NULL) {
				gflog_debug(GFARM_MSG_1001925,
					"dir_cursor_get_name_and_inode() "
					"failed: %s",
					gfarm_error_string(e_rpc));
				break;
			}
			if ((e_rpc = inode_get_stat(entry_inode, &p[i].st)) !=
			    GFARM_ERR_NO_ERROR) {
				free(p[i].name);
				gflog_debug(GFARM_MSG_1001926,
					"inode_get_stat() failed: %s",
					gfarm_error_string(e_rpc));
				break;
			}

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		if (e_rpc == GFARM_ERR_NO_ERROR) {
			fs_dir_remember_cursor(peer, process, fd, dir,
			    &cursor, n == 0);
			if (i > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
		n = i;
	}

	giant_unlock();
	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", n);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			struct gfs_stat *st = &p[i].st;

			e_ret = gfp_xdr_send(client, "sllilsslllilili",
			    p[i].name,
			    st->st_ino, st->st_gen, st->st_mode, st->st_nlink,
			    st->st_user, st->st_group, st->st_size,
			    st->st_ncopy,
			    st->st_atimespec.tv_sec, st->st_atimespec.tv_nsec,
			    st->st_mtimespec.tv_sec, st->st_mtimespec.tv_nsec,
			    st->st_ctimespec.tv_sec, st->st_ctimespec.tv_nsec);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000387,
				    "%s@%s: getdirentsplus: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e_ret));
				break;
			}
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	if (p != NULL) {
		for (i = 0; i < n; i++) {
			free(p[i].name);
			gfs_stat_free(&p[i].st);
		}
		free(p);
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_getdirentsplusxattr(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep,
	int from_client, int skip)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
	gfarm_int32_t fd, n, nattrpatterns, i, j;
	char **attrpatterns;
	struct process *process;
	struct inode *inode, *entry_inode;
	Dir dir;
	DirCursor cursor;
	struct dir_result_rec {
		char *name;
		struct gfs_stat st;
		size_t nxattrs;
		struct xattr_list *xattrs;
	} *p = NULL, *pp;
	struct xattr_list *px;
	struct db_waitctx waitctx;
	static const char diag[] = "GFM_PROTO_GETDIRENTSPLUSXATTR";

	e_ret = gfm_server_get_request(peer, sizep, diag,
	    "ii", &n, &nattrpatterns);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);

	e_ret = gfm_server_recv_attrpatterns(peer, sizep, skip, nattrpatterns,
	    &attrpatterns, diag);
	/* don't have to free attrpatterns in the return case */
	if (e_ret != GFARM_ERR_NO_ERROR || skip)
		return (e_ret);

	/* NOTE: attrpatterns may be NULL here in case of memory shortage */

	if (attrpatterns == NULL) {
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else if ((e_rpc = wait_db_update_info(peer, DBUPDATE_FS_DIRENT |
	    DBUPDATE_USER | DBUPDATE_GROUP | DBUPDATE_XMLATTR, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
		/* Continue processing. */
	}

	giant_lock();

	if (e_rpc != GFARM_ERR_NO_ERROR) {
		;
	} else if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002504, "fs_dir_get() failed: %s",
		    gfarm_error_string(e_rpc));
	} else if (n > 0 && GFARM_CALLOC_ARRAY(p,  n) == NULL) {
		gflog_debug(GFARM_MSG_1002505, "allocation of array failed");
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else { /* NOTE: (n == 0) means the end of the directory */
		for (i = 0; i < n; ) {
			if ((e_rpc = dir_cursor_get_name_and_inode(dir, &cursor,
			    &p[i].name, &entry_inode)) != GFARM_ERR_NO_ERROR ||
			    p[i].name == NULL) {
				gflog_debug(GFARM_MSG_1002506,
				    "dir_cursor_get_name_and_inode() "
				    "failed: %s",
				    gfarm_error_string(e_rpc));
				break;
			}
			if ((e_rpc = inode_get_stat(entry_inode, &p[i].st)) !=
			    GFARM_ERR_NO_ERROR) {
				free(p[i].name);
				gflog_debug(GFARM_MSG_1002507,
				    "inode_get_stat() failed: %s",
				    gfarm_error_string(e_rpc));
				break;
			}

			i++;
			if (!dir_cursor_next(dir, &cursor))
				break;
		}
		if (e_rpc == GFARM_ERR_NO_ERROR) {
			fs_dir_remember_cursor(peer, process, fd, dir,
			    &cursor, n == 0);
			if (i > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
		n = i;
	}

	if (e_rpc == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			pp = &p[i];
			e_rpc = inode_xattr_list_get_cached_by_patterns(
			    pp->st.st_ino, attrpatterns, nattrpatterns,
			    &pp->xattrs, &pp->nxattrs);
			if (e_rpc != GFARM_ERR_NO_ERROR) {
				pp->xattrs = NULL;
				pp->nxattrs = 0;
			}
			for (j = 0; j < pp->nxattrs; j++) {
				px = &pp->xattrs[j];
				if (px->value == NULL) {
					/* not cached */
					db_waitctx_init(&waitctx);
					e_rpc = db_xattr_get(0,
					    pp->st.st_ino, px->name,
					    &px->value, &px->size, &waitctx);
					if (e_rpc == GFARM_ERR_NO_ERROR) {
						/*
						 * XXX this is slow,
						 * but we don't know
						 * the safe window size
						 */
						giant_unlock();
						e_rpc = dbq_waitret(&waitctx);
						giant_lock();
					}
					db_waitctx_fini(&waitctx);
					/*
					 * if error happens,
					 * px->value == NULL here
					 */
					if (e_rpc != GFARM_ERR_NO_ERROR)
						break;
				}
				e_rpc = acl_convert_for_getxattr(
				    inode_lookup(pp->st.st_ino),
				    px->name, &px->value, &px->size);
				if (e_rpc != GFARM_ERR_NO_ERROR) {
					gflog_debug(GFARM_MSG_1002853,
					    "acl_convert_for_getxattr()"
					    " failed: %s",
					    gfarm_error_string(e_rpc));
					break;
				}
			}
			if (e_rpc != GFARM_ERR_NO_ERROR)
				break;
		}
	}

	giant_unlock();

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", n);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; i++) {
			struct gfs_stat *st = &p[i].st;

			e_ret = gfp_xdr_send(client, "sllilsslllililii",
			    p[i].name,
			    st->st_ino, st->st_gen, st->st_mode, st->st_nlink,
			    st->st_user, st->st_group, st->st_size,
			    st->st_ncopy,
			    st->st_atimespec.tv_sec, st->st_atimespec.tv_nsec,
			    st->st_mtimespec.tv_sec, st->st_mtimespec.tv_nsec,
			    st->st_ctimespec.tv_sec, st->st_ctimespec.tv_nsec,
			    (int)p[i].nxattrs);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002508,
				    "%s@%s: getdirentsplusxattr: %s",
				    peer_get_username(peer),
				    peer_get_hostname(peer),
				    gfarm_error_string(e_ret));
				break;
			}
			for (j = 0; j < p[i].nxattrs; j++) {
				px = &p[i].xattrs[j];
				e_ret = gfp_xdr_send(client, "sb",
				    px->name, px->size, px->value);
				if (e_ret != GFARM_ERR_NO_ERROR) {
					gflog_warning(GFARM_MSG_1002509,
					    "%s@%s: getdirentsplusxattr: %s",
					    peer_get_username(peer),
					    peer_get_hostname(peer),
					    gfarm_error_string(e_ret));
					break;
				}
			}
			if (e_ret != GFARM_ERR_NO_ERROR)
				break;
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	if (p != NULL) {
		for (i = 0; i < n; i++) {
			free(p[i].name);
			gfs_stat_free(&p[i].st);
			inode_xattr_list_free(p[i].xattrs, p[i].nxattrs);
		}
		free(p);
	}
	if (attrpatterns != NULL) {
		for (i = 0; i < nattrpatterns; i++)
			free(attrpatterns[i]);
		free(attrpatterns);
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_seek(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, whence;
	gfarm_off_t offset, current, max;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	Dir dir;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_SEEK";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SEEK, "li", &offset, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (!from_client &&
			(spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001928,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001929,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001930,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001931,
			    "process_get_file_inode() failed: %s",
			    gfarm_error_string(e));
		} else if ((dir = inode_get_dir(inode)) == NULL) {
			gflog_debug(GFARM_MSG_1001932,
			    "inode_get_dir() failed");
			e = GFARM_ERR_NOT_A_DIRECTORY;
		} else if ((e = process_get_dir_offset(process, peer, fd,
			    &current)) != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001933,
			    "process_get_dir_offset() failed: %s",
			    gfarm_error_string(e));
		} else if (whence < 0 || whence > 2) {
			gflog_debug(GFARM_MSG_1001934,
			    "argument 'whence' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else {
			max = dir_get_entry_count(dir);
			switch (whence) {
			case 0:
				break;
			case 1:
				offset += current;
				break;
			case 2:
				offset += max;
				break;
			default:
				assert(0);
			}
			if (offset != current) {
				if (offset < 0)
					offset = 0;
				else if (offset > max)
					offset = max;
				process_clear_dir_key(process, peer, fd);
				process_set_dir_offset(process, peer, fd,
						       offset);
			}
		}
		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "l", &offset));
}

struct reopen_resume_arg {
	int relayed;
	gfp_xdr_xid_t xid;
	int fd;
};

gfarm_error_t
reopen_resume(struct peer *peer, void *closure, int *suspendedp)
{
	gfarm_error_t e;
	struct reopen_resume_arg *arg = closure;
	struct host *spool_host;
	struct process *process;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0, flags = 0, to_create = 0;
	gfp_xdr_xid_t xid;
	size_t junk = 0, *sizep;
	int transaction = 0;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;

	static const char diag[] = "reopen_resume";

	giant_lock();

	if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002261,
		    "%s: peer_get_host() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002262,
		    "%s: peer_get_process() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		e = process_reopen_file(process, peer, spool_host, arg->fd,
		    &inum, &gen, &mode, &flags, &to_create);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			if ((e = process_new_generation_wait(peer, arg->fd,
			    reopen_resume, arg)) == GFARM_ERR_NO_ERROR) {
				*suspendedp = 1;
				giant_unlock();
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		trace_seq_num = trace_log_get_sequence_number();
		gettimeofday(&tv, NULL);
	}

	sizep = arg->relayed ? &junk : NULL;
	xid = arg->xid;
	free(arg);
	giant_unlock();
	e2 = gfm_server_put_reply(peer, xid, sizep, diag, e, "lliii",
	    inum, gen, mode, flags, to_create);

	if (gfarm_ctxp->file_trace && e == GFARM_ERR_NO_ERROR) {
		gflog_trace(GFARM_MSG_1003307,
		    "%lld/%010ld.%06ld////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    (long int)tv.tv_sec, (long int)tv.tv_usec,
		    gfarm_host_get_self_name(), gfmd_port,
		    host_name(spool_host),
		    (long long int)inum,
		    (long long int)gen);
	}

	return (e2);
}

gfarm_error_t
gfm_server_reopen(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int *suspendedp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_int32_t mode = 0, flags = 0, to_create = 0;
	struct reopen_resume_arg *arg;
	int transaction = 0;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;
	struct relayed_request *relay;

	static const char diag[] = "GFM_PROTO_REOPEN";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REOPEN, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001935,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001936,
			    "peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001937,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001938,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			e = process_reopen_file(process, peer, spool_host,
			    fd, &inum, &gen, &mode, &flags, &to_create);
			if (transaction)
				db_end(diag);
			if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
				GFARM_MALLOC(arg);
				if (arg == NULL) {
					e = GFARM_ERR_NO_MEMORY;
				} else {
					arg->relayed = sizep != NULL;
					arg->xid = xid;
					arg->fd = fd;
					if ((e = process_new_generation_wait(
					    peer, fd, reopen_resume, arg)) ==
					    GFARM_ERR_NO_ERROR) {
						*suspendedp = 1;
						giant_unlock();
						return (GFARM_ERR_NO_ERROR);
					}
				}
			}
		}
		if (gfarm_ctxp->file_trace && to_create
					   && e == GFARM_ERR_NO_ERROR) {
			trace_seq_num = trace_log_get_sequence_number();
			gettimeofday(&tv, NULL);
		}

		giant_unlock();
	}
	e2 = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "lliii", &inum, &gen, &mode, &flags, &to_create);

	if (relay == NULL &&
	    gfarm_ctxp->file_trace && to_create && e == GFARM_ERR_NO_ERROR) {
		gflog_trace(GFARM_MSG_1003308,
		    "%lld/%010ld.%06ld////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    (long int)tv.tv_sec, (long int)tv.tv_usec,
		    gfarm_host_get_self_name(),
		    gfmd_port,
		    host_name(spool_host),
		    (long long int)inum,
		    (long long int)gen);
	}

	return (e2);
}

gfarm_error_t
gfm_server_close_read(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct gfarm_timespec atime;
	struct host *spool_host;
	struct process *process;
	int transaction = 0;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CLOSE_READ";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CLOSE_READ, "li", &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001940,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001941,
			    "peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001942,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001943,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			/*
			 * closing must be done regardless of the result of
			 * db_begin().
			 * because not closing may cause descriptor leak.
			 */
			e = process_close_file_read(process, peer, fd, &atime);
			if (transaction)
				db_end(diag);
			if (e == GFARM_ERR_NO_ERROR) /* permission ok */
				e = peer_fdpair_close_current(peer);
		}

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

struct close_v2_4_resume_arg {
	int relayed;
	gfp_xdr_xid_t xid;
	int fd;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
};

gfarm_error_t
close_write_v2_4_resume(struct peer *peer, void *closure, int *suspendedp)
{
	gfarm_error_t e_ret, e_rpc;
	struct close_v2_4_resume_arg *arg = closure;
	struct host *spool_host;
	struct process *process;
	int transaction = 0;
	gfarm_int32_t flags = 0;
	gfarm_ino_t inum = 0;
	gfarm_int64_t old_gen = 0, new_gen = 0;
	gfp_xdr_xid_t xid;
	size_t junk = 0, *sizep;
	char *trace_log;
	static const char diag[] = "close_v2_4_resume";

	giant_lock();

	if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002263,
		    "%s: peer_get_host() failed", diag);
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002264,
		    "%s: peer_get_process() failed", diag);
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e_rpc = process_close_file_write(
		    process, peer, arg->fd, arg->size, &arg->atime, &arg->mtime,
		    &flags, &inum, &old_gen, &new_gen, &trace_log);
		if (transaction)
			db_end(diag);

		if (e_rpc == GFARM_ERR_NO_ERROR) { /* permission ok */
			e_rpc = peer_fdpair_close_current(peer);
		} else if (e_rpc ==
		    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			if ((e_rpc = process_new_generation_wait(peer, arg->fd,
			    close_write_v2_4_resume, arg)) ==
			    GFARM_ERR_NO_ERROR) {
				*suspendedp = 1;
				giant_unlock();
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	sizep = arg->relayed ? &junk : NULL;
	xid = arg->xid;
	free(arg);
	giant_unlock();

	if (e_rpc == GFARM_ERR_NO_ERROR && gfarm_ctxp->file_trace && 
	    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0 &&
	    trace_log != NULL) {
		gflog_trace(GFARM_MSG_1003435, "%s", trace_log);
		free(trace_log);
	}

	e_ret = gfm_server_put_reply(peer, xid, sizep, diag, e_rpc, "ill",
	    flags, old_gen, new_gen);
	if (e_rpc == GFARM_ERR_NO_ERROR && e_ret != GFARM_ERR_NO_ERROR) {
		/*
		 * There is severe race condition here (SourceForge #419),
		 * but there is no guarantee that this error is logged.
		 * because network communication error may happen later.
		 */
		gflog_error(GFARM_MSG_1003481,
		    "%s: inode %lld generation %lld -> %lld: %s", diag,
		    (long long)inum, (long long)old_gen, (long long)new_gen,
		    gfarm_error_string(e_ret));
	}
	return (e_ret);
}

/* trace_log is malloc(3)ed string, thus caller should free(3) the memory. */
gfarm_error_t
gfm_server_close_write_common(const char *diag,
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep, int from_client,
	gfarm_off_t size,
	struct gfarm_timespec *atime, struct gfarm_timespec *mtime,
	gfarm_int32_t *flagsp,
	gfarm_ino_t *inump, gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	char **trace_logp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct process *process;
	int transaction = 0;

	struct close_v2_4_resume_arg *arg;

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1001944, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (peer_get_host(peer) == NULL) {
		gflog_debug(GFARM_MSG_1001945, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001946, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001947,
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
		    atime, mtime,
		    flagsp, inump, old_genp, new_genp, trace_logp);
		if (transaction)
			db_end(diag);

		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
		else if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			GFARM_MALLOC(arg);
			if (arg == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				arg->relayed = sizep != NULL;
				arg->xid = xid;
				arg->fd = fd;
				arg->size = size;
				arg->atime = *atime;
				arg->mtime = *mtime;
				if ((e = process_new_generation_wait(peer, fd,
				    close_write_v2_4_resume, arg)) ==
				    GFARM_ERR_NO_ERROR) {
					return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
				}
			}
		}
	}
	return (e);
}

gfarm_error_t
gfm_server_close_write(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CLOSE_WRITE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CLOSE_WRITE, "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		e = gfm_server_close_write_common(diag, peer, xid, sizep,
		    from_client, size, &atime, &mtime,
		    NULL, NULL, NULL, NULL, NULL);

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_close_write_v2_4(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int *suspendedp)
{
	gfarm_error_t e_rpc, e_ret;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t flags;
	gfarm_ino_t inum = 0;
	gfarm_int64_t old_gen = 0, new_gen = 0;
	char *trace_log;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_CLOSE_WRITE_V2_4";

	e_rpc = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_CLOSE_WRITE_V2_4, "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e_rpc != GFARM_ERR_NO_ERROR)
		return (e_rpc);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		e_rpc = gfm_server_close_write_common(diag, peer, xid, sizep,
		    from_client, size, &atime, &mtime,
		    &flags, &inum, &old_gen, &new_gen, &trace_log);

		giant_unlock();

		if (e_rpc == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			*suspendedp = 1;
			return (GFARM_ERR_NO_ERROR);
		}

		if (e_rpc == GFARM_ERR_NO_ERROR && gfarm_ctxp->file_trace && 
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED)
		    != 0 && trace_log != NULL) {
			gflog_trace(GFARM_MSG_1003309, "%s", trace_log);
			free(trace_log);
		}
	}
	e_ret = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e_rpc, "ill", &flags, &old_gen, &new_gen);
	if (e_rpc == GFARM_ERR_NO_ERROR && e_ret != GFARM_ERR_NO_ERROR) {
		/*
		 * There is severe race condition here (SourceForge #419),
		 * but there is no guarantee that this error is logged.
		 * because network communication error may happen later.
		 */
		gflog_error(GFARM_MSG_1003482,
		    "%s: inode %lld generation %lld -> %lld: %s", diag,
		    (long long)inum, (long long)old_gen, (long long)new_gen,
		    gfarm_error_string(e_ret));
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_fhclose_read(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct gfarm_timespec atime;
	struct host *spool_host;
	int transaction = 0;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FHCLOSE_READ";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FHCLOSE_READ, "llli",
	    &inum, &gen, &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1003311,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1003312,
			    "peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((inode = inode_lookup(inum)) == NULL) {
			gflog_debug(GFARM_MSG_1003313,
			    "inode_lookup() failed");
			e = GFARM_ERR_STALE_FILE_HANDLE;
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;

			/*
			 * closing must be done regardless of the result of
			 * db_begin().
			 * because not closing may cause descriptor leak.
			 */
			e = inode_fhclose_read(inode, &atime);
			if (transaction)
				db_end(diag);
		}

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	     &e, ""));
}

static gfarm_error_t
fhclose_write(struct peer *peer, struct host *spool_host, struct inode *inode,
	gfarm_off_t size,
	struct gfarm_timespec *atimep, struct gfarm_timespec *mtimep,
	gfarm_int32_t *flagsp,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	gfarm_uint64_t *cookiep, char **trace_logp, const char *diag)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	int transaction = 0, generation_updated;
	gfarm_int32_t flags = 0;
	gfarm_uint64_t cookie;

	if (db_begin(diag) == GFARM_ERR_NO_ERROR)
		transaction = 1;

	/* closing must be done regardless of the result of db_begin(). */
	e = inode_file_handle_update(inode, size, atimep, mtimep, spool_host,
	    old_genp, new_genp, &generation_updated, trace_logp);
	if (e == GFARM_ERR_NO_ERROR && generation_updated) {
		flags = GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED;
		if ((e = peer_add_pending_new_generation_by_cookie(
		    peer, inode, &cookie)) != GFARM_ERR_NO_ERROR) {
			;
		} else if ((e = inode_new_generation_by_cookie_start(
		    inode, peer, cookie)) != GFARM_ERR_NO_ERROR) {
			peer_remove_pending_new_generation_by_cookie(
			    peer, cookie, NULL);
		}
	}
	if (transaction)
		db_end(diag);

	if (e != GFARM_ERR_NO_ERROR)
		return (e);

	*flagsp = flags;
	if ((flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0)
		*cookiep = cookie;

	return (GFARM_ERR_NO_ERROR);
}


struct fhclose_write_resume_arg {
	int relayed;
	gfp_xdr_xid_t xid;
	struct inode *inode;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int64_t old_gen; /* only for logging */
};

gfarm_error_t
fhclose_write_resume(struct peer *peer, void *closure, int *suspendedp)
{
	gfarm_error_t e;
	struct fhclose_write_resume_arg *arg = closure;
	struct host *spool_host;
	gfarm_int64_t old_gen = 0, new_gen = 0;
	gfarm_int32_t flags = 0;
	gfarm_uint64_t cookie = 0;
	gfp_xdr_xid_t xid;
	size_t junk = 0, *sizep;
	char *trace_log;
	static const char diag[] = "fhclose_write_resume";

	giant_lock();

	if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002263,
		    "%s: peer_get_host() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!inode_is_file(arg->inode)) {
		gflog_warning(GFARM_MSG_UNFIXED,
		    "%s: inode %lld:%lld: not a file",
		    diag, (long long)inode_get_number(arg->inode),
		    (long long)arg->old_gen);
		e = GFARM_ERR_STALE_FILE_HANDLE;
	} else if (inode_new_generation_is_pending(arg->inode)) {
		if ((e = inode_new_generation_wait(arg->inode, peer,
		    fhclose_write_resume, arg)) == GFARM_ERR_NO_ERROR) {
			*suspendedp = 1;
			giant_unlock();
			return (GFARM_ERR_NO_ERROR);
		}
	} else {
		e = fhclose_write(peer, spool_host, arg->inode, arg->size,
		    &arg->atime, &arg->mtime,
		    &flags, &old_gen, &new_gen, &cookie, &trace_log, diag);
	}
	sizep = arg->relayed ? &junk : NULL;
	xid = arg->xid;
	free(arg);

	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR && gfarm_ctxp->file_trace &&
	    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0 &&
	    trace_log != NULL) {
		gflog_trace(GFARM_MSG_UNFIXED, "%s", trace_log);
		free(trace_log);
	}

	return (gfm_server_put_reply(peer, xid, sizep, diag, e, "illl",
	    flags, old_gen, new_gen, cookie));
}

gfarm_error_t
gfm_server_fhclose_write(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int *suspendedp)
{
	gfarm_error_t e;
	struct host *spool_host;
	gfarm_ino_t inum;
	gfarm_int64_t old_gen, new_gen = 0;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t flags = 0;
	gfarm_uint64_t cookie = 0;
	struct inode *inode;
	struct fhclose_write_resume_arg *arg;
	char *trace_log;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_FHCLOSE_WRITE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_FHCLOSE_WRITE, "llllili",
	    &inum, &old_gen, &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1003314,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1003315,
			    "peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((inode = inode_lookup(inum)) == NULL) {
			gflog_debug(GFARM_MSG_1003316,
			     "inode_lookup() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (!inode_is_file(inode)) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: inode %lld:%lld: not a file",
			    diag, (long long)inum, (long long)old_gen);
			e = GFARM_ERR_STALE_FILE_HANDLE;
		} else if (inode_new_generation_is_pending(inode)) {
			GFARM_MALLOC(arg);
			if (arg == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				arg->relayed = sizep != NULL;
				arg->xid = xid;
				arg->inode = inode;
				arg->size = size;
				arg->atime = atime;
				arg->mtime = mtime;
				arg->old_gen = old_gen; /* for logging */
				if ((e = inode_new_generation_wait(inode, peer,
				    fhclose_write_resume, arg)) ==
				    GFARM_ERR_NO_ERROR) {
					*suspendedp = 1;
					giant_unlock();
					return (GFARM_ERR_NO_ERROR);
				}
			}
		} else {
			e = fhclose_write(peer, spool_host, inode, size,
			    &atime, &mtime,
			    &flags, &old_gen, &new_gen, &cookie,
			    &trace_log, diag);
		}

		giant_unlock();

		if (e == GFARM_ERR_NO_ERROR && gfarm_ctxp->file_trace &&
		    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED)
		    != 0 && trace_log != NULL) {
			gflog_trace(GFARM_MSG_UNFIXED, "%s", trace_log);
			free(trace_log);
		}
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	     &e, "illl", &flags, &old_gen, &new_gen, &cookie));
}

gfarm_error_t
gfm_server_generation_updated(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, result;
	struct host *spool_host;
	struct process *process;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_GENERATION_UPDATED";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_GENERATION_UPDATED, "i", &result);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1002266,
			    "%s: from client", diag);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1002267,
			    "%s: peer_get_host() failed", diag);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1002268,
			    "%s: peer_get_process() failed", diag);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002269,
			    "%s: peer_fdpair_get_current() failed: %s",
			    diag, gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, fd, &inode))
			   != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_UNFIXED,
			    "%s: process_get_file_inode() failed: %s",
			    diag, gfarm_error_string(e));
		} else if ((e = process_new_generation_done(process, peer, fd,
		    result)) != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1002270,
			 "%s: host %s, fd %d: new generation wakeup(%s): %s\n",
			    diag, host_name(spool_host), fd,
			    gfarm_error_string(result), gfarm_error_string(e));
		} else if (result != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: inode %lld:%lld on host %s, fd %d: "
			    "new generation rename: %s\n",
			    diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    host_name(spool_host), fd,
			    gfarm_error_string(result));
		}

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_generation_updated_by_cookie(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_uint64_t cookie;
	gfarm_int32_t result;
	struct host *spool_host;
	struct inode *inode;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_GENERATION_UPDATED_BY_COOKIE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_GENERATION_UPDATED_BY_COOKIE, "li", &cookie, &result);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1003317, "%s: from client",
			    diag);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1003318,
			    "%s: peer_get_host() failed", diag);
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (!peer_remove_pending_new_generation_by_cookie(
		    peer, cookie, &inode)) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: unknown cookie %lld from %s",
			    diag, (long long)cookie, host_name(spool_host));
			e = GFARM_ERR_BAD_COOKIE;
		} else if ((e = inode_new_generation_by_cookie_finish(
		    inode, cookie, peer, result)) != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: host %s, cookie %lld: "
			    "new generation wakeup(%s): %s\n",
			    diag, host_name(spool_host), (long long)cookie,
			    gfarm_error_string(result), gfarm_error_string(e));
		} else if (result != GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_UNFIXED,
			    "%s: inode %lld:%lld on host %s, cookie %lld: "
			    "new generation rename: %s\n",
			    diag,
			    (long long)inode_get_number(inode),
			    (long long)inode_get_gen(inode),
			    host_name(spool_host), (long long)cookie,
			    gfarm_error_string(result));
		}

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_lock(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_LOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000388, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_LOCK, "llii", &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag, 
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_trylock(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_TRYLOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000389, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_TRYLOCK, "llii", &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_unlock(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_UNLOCK";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000390, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_UNLOCK, "llii", &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_lock_info(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_off_t start, len;
	gfarm_int32_t type, whence;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_LOCK_INFO";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000391, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_LOCK_INFO, "llii", &start, &len, &type, &whence);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_glob(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_GLOB";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000392, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_GLOB, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_schedule(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_SCHEDULE";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000393, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_SCHEDULE, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_pio_open(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_PIO_OPEN";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000394, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_PIO_OPEN, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_pio_set_paths(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_PIO_SET_PATHS";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000395, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_PIO_SET_PATHS, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_pio_close(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_PIO_CLOSE";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000396, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_PIO_CLOSE, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_pio_visit(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_PIO_VISIT";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000397, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_PIO_CLOSE, "");
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_replica_list_by_name(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, int from_client, int skip)
{
	struct peer *mhpeer;
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
	struct host *spool_host;
	struct process *process;
	int fd, i;
	gfarm_int32_t n = 0;
	struct inode *inode;
	char **hosts = NULL;
	static const char diag[] = "GFM_PROTO_REPLICA_LIST_BY_NAME";

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e_rpc = wait_db_update_info(peer, DBUPDATE_FS | DBUPDATE_HOST, diag);
	if (e_rpc != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
	}

	giant_lock();

	if (e_rpc != GFARM_ERR_NO_ERROR) {
		;
	} else if (!from_client &&
	    (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001948,
		    "operation is not permitted");
		    e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001949, "peer_get_process() failed");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e_rpc = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001950,
		    "peer_fdpair_get_current() failed: %s",
		    gfarm_error_string(e_rpc));
	} else if ((e_rpc = process_get_file_inode(process, fd, &inode))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001951,
		    "process_get_file_inode() failed: %s",
		    gfarm_error_string(e_rpc));
	} else
		e_rpc = inode_replica_list_by_name(inode, &n, &hosts);

	giant_unlock();

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", n);
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e_ret = gfp_xdr_send(peer_get_conn(peer), "s",
			    hosts[i]);
			if (e_ret != GFARM_ERR_NO_ERROR)
				break;
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	if (e_rpc == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i)
			free(hosts[i]);
		free(hosts);
	}

	return (e_ret);
}

gfarm_error_t
gfm_server_replica_list_by_host(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICA_LIST_BY_HOST";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000398, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_LIST_BY_HOST, "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_replica_remove_by_host(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *host;
	gfarm_int32_t port;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE_BY_HOST";

	/* XXX - NOT IMPLEMENTED */
	gflog_error(GFARM_MSG_1000399, "%s: not implemented", diag);

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_REMOVE_BY_HOST, "si",
	    &host, &port);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(host);
		return (GFARM_ERR_NO_ERROR);
	}

	free(host);
	e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED;
	e = gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "");
	return (e);
}

gfarm_error_t
gfm_server_replica_remove_by_file(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *hostname;
	struct process *process;
	gfarm_int32_t cfd;
	struct inode *inode;
	struct host *host, *spool_host;
	struct relayed_request *relay;
	int transaction = 0;
	struct file_opening *fo;
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE_BY_FILE";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_REMOVE_BY_FILE, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(hostname);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(hostname);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001953,
			    "peer_get_process() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if (process_get_user(process) == NULL) {
			gflog_debug(GFARM_MSG_1001954,
			    "process_get_user() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001955,
			    "peer_fdpair_get_current() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_file_inode(process, cfd, &inode))
			!=  GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1001956,
			    "process_get_file_inode() failed: %s",
			    gfarm_error_string(e));
		} else if ((host = host_lookup(hostname)) == NULL) {
			gflog_debug(GFARM_MSG_1001957, "host_lookup() failed");
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if (
		    (spool_host = inode_writing_spool_host(inode)) != NULL &&
			spool_host == host) {
			gflog_debug(GFARM_MSG_1001958,
			    "inode_writing_spool_host() failed");
			e = GFARM_ERR_TEXT_FILE_BUSY;
		} else if ((e = process_get_file_opening(process, cfd, &fo)) !=
			   GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1003649,
			    "process_get_file_opening() failed: %s",
			    gfarm_error_string(e));
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			e = inode_remove_replica_protected(inode, host, fo);
			if (transaction)
				db_end(diag);
		}

		free(hostname);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

/*
 * Closure for
 *     gfm_server_replica_info_get_request()
 *     gfm_server_replica_info_get_reply()
 *     gfm_server_replica_info_get()
 *
 * They are protocol handlers for
 *     GFM_PROTO_REPLICA_INFO_GET
 */
struct replica_info_closure {
	gfarm_int32_t iflags;
	gfarm_int32_t nhosts;
	char **hosts;
	gfarm_int64_t *gens;
	gfarm_int32_t *oflags;
	gfarm_error_t error;
};

static void
replica_info_closure_init(struct replica_info_closure *closure)
{
	closure->iflags = 0;
	closure->nhosts = 0;
	closure->hosts  = NULL;
	closure->gens   = NULL;
	closure->oflags = NULL;
	closure->error  = GFARM_ERR_NO_ERROR;
}

static gfarm_int32_t
replica_info_closure_get_iflags(struct replica_info_closure *closure)
{
	return (closure->iflags);
}

static void
replica_info_closure_get_info(struct replica_info_closure *closure,
    gfarm_int32_t *nhosts, char ***hosts,
    gfarm_int64_t **gens, gfarm_int32_t **oflags)
{
	*nhosts = closure->nhosts;
	*hosts  = closure->hosts;
	*gens   = closure->gens;
	*oflags = closure->oflags;
}

static gfarm_error_t
replica_info_closure_get_error(struct replica_info_closure *closure)
{
	return (closure->error);
}

static void
replica_info_closure_set_iflags(struct replica_info_closure *closure, 
    gfarm_int32_t iflags)
{
	closure->iflags = iflags;
}

static void
replica_info_closure_set_info(struct replica_info_closure *closure, 
    gfarm_int32_t nhosts, char **hosts,
    gfarm_int64_t *gens, gfarm_int32_t *oflags)
{
	closure->nhosts = nhosts;
	closure->hosts  = hosts;
	closure->gens   = gens;
	closure->oflags = oflags;
}

static void
replica_info_closure_set_error(struct replica_info_closure *closure, 
    gfarm_error_t e)
{
	closure->error = e;
}

static void
replica_info_closure_term(struct replica_info_closure *closure)
{
	gfarm_int32_t i;

	if (closure->hosts != NULL) {
		for (i = 0; i < closure->nhosts; ++i)
			free(closure->hosts[i]);
		free(closure->hosts);
	}
	free(closure->gens);
	free(closure->oflags);
}

static gfarm_error_t
gfm_server_replica_info_get_request(enum request_reply_mode mode,
	struct peer *peer, size_t *sizep, int skip, struct relayed_request *r,
	void *closure, const char *diag)
{
	gfarm_error_t e;
	gfarm_int32_t iflags = replica_info_closure_get_iflags(closure);

	e = gfm_server_relay_get_request_dynarg(peer, sizep, skip, r, diag,
	    "i", &iflags);
	if (mode != RELAY_TRANSFER)
		replica_info_closure_set_iflags(closure, iflags);
	return (e);
}

static gfarm_error_t
gfm_server_replica_info_get_reply(enum request_reply_mode mode,
	struct peer *peer, size_t *sizep, int skip, void *closure,
	const char *diag)
{
	gfarm_error_t e_ret;
	gfarm_error_t e_rpc = replica_info_closure_get_error(closure);
	gfarm_int32_t iflags = replica_info_closure_get_iflags(closure);
	struct host *spool_host;
	struct process *process;
	int fd, i;
	struct inode *inode;
	gfarm_int32_t n = 0;
	char **hosts = NULL;
	gfarm_int64_t *gens = NULL;
	gfarm_int32_t *oflags = NULL;
	int from_client = 
	    (peer_get_auth_id_type(peer) == GFARM_AUTH_ID_TYPE_USER);

	if (skip)
		return (GFARM_ERR_NO_ERROR);

	/* do not relay RPC to master gfmd */
	if (mode == RELAY_TRANSFER) {
		replica_info_closure_get_info(closure, &n, &hosts, &gens,
		    &oflags);
	} else {
		e_rpc = wait_db_update_info(peer, DBUPDATE_FS | DBUPDATE_HOST,
		    diag);
		if (e_rpc != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_UNFIXED,
			    "%s: failed to wait for the backend DB to be "
			    "updated: %s",
			    diag, gfarm_error_string(e_rpc));
		}

		giant_lock();
		spool_host = peer_get_host(peer);
		if (e_rpc != GFARM_ERR_NO_ERROR)
			;
		else if (!from_client && spool_host == NULL)
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		else if ((process = peer_get_process(peer)) == NULL)
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		else if ((e_rpc = peer_fdpair_get_current(peer, &fd)) != 
		    GFARM_ERR_NO_ERROR)
			;
		else if ((e_rpc = process_get_file_inode(process, fd,
		    &inode)) != GFARM_ERR_NO_ERROR)
			;
		else {
			e_rpc = inode_replica_info_get(inode, iflags, &n,
			    &hosts, &gens, &oflags);
		}
		giant_unlock();
		replica_info_closure_set_error(closure, e_rpc);
		/*
		 * It works fine even when inode_replica_info_get() has failed.
		 */
		replica_info_closure_set_info(closure, n, hosts, gens, oflags);
	}

	e_ret = gfm_server_relay_put_reply_dynarg(peer, sizep, diag, e_rpc,
	    "");
	if (e_ret != GFARM_ERR_NO_ERROR || e_rpc != GFARM_ERR_NO_ERROR)
		goto end;

	e_ret = gfm_server_relay_put_reply_arg_dynarg(peer, sizep, diag,
	    "i", n);
	if (e_ret != GFARM_ERR_NO_ERROR)
		goto end;
	for (i = 0; i < n; ++i) {
		e_ret = gfm_server_relay_put_reply_arg_dynarg(peer, sizep,
		    diag, "sli", hosts[i], gens[i], oflags[i]);
		if (e_ret != GFARM_ERR_NO_ERROR)
			goto end;
	}

end:
	return (e_ret);
}

gfarm_error_t
gfm_server_replica_info_get(struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, int from_client, int skip)
{
	gfarm_error_t e;
	struct replica_info_closure closure;
	static const char diag[] = "GFM_PROTO_REPLICA_INFO_GET";

	replica_info_closure_init(&closure);
	if ((e = gfm_server_relay_request_reply(peer, xid, skip,
	    gfm_server_replica_info_get_request,
	    gfm_server_replica_info_get_reply,
	    GFM_PROTO_REPLICA_INFO_GET, &closure, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED, "%s: %s",
		    diag, gfarm_error_string(e));
	} else
		e = replica_info_closure_get_error(&closure);

	replica_info_closure_term(&closure);
	return (e);
}

gfarm_error_t
gfm_server_replicate_file_from_to(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	char *srchost;
	char *dsthost;
	gfarm_int32_t flags;
	struct process *process;
	gfarm_int32_t cfd;
	struct host *src, *dst;
	struct inode *inode;
	struct file_replication *fr;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICATE_FILE_FROM_TO";

#ifdef __GNUC__ /* shut up stupid warning by gcc */
	src = NULL;
	dst = NULL;
	fr = NULL;
#endif

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICATE_FILE_FROM_TO, "ssi",
	    &srchost, &dsthost, &flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(srchost);
		free(dsthost);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */

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
		else if ((e = process_prepare_to_replicate(process, peer, src,
		   dst, cfd, flags, &fr, &inode)) != GFARM_ERR_NO_ERROR)
			;
		else {
			inode_replication_start(inode);
		}

		giant_unlock();
	}
	free(srchost);
	free(dsthost);
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

struct replica_adding_resume_arg {
	int relayed;
	gfp_xdr_xid_t xid;
	int fd;
	char *src_host;
};

gfarm_error_t
replica_adding_resume(struct peer *peer, void *closure, int *suspendedp)
{
	gfarm_error_t e;
	struct replica_adding_resume_arg *arg = closure;
	struct host *spool_host;
	struct process *process;
	struct host *src;
	struct inode *inode;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	struct gfarm_timespec *mtime;
	gfarm_int64_t mtime_sec = 0;
	gfarm_int32_t mtime_nsec = 0;
	gfp_xdr_xid_t xid;
	size_t junk = 0, *sizep;
	static const char diag[] = "replica_adding_resume";

	giant_lock();

	if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002272,
		    "%s: peer_get_host() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002273,
		    "%s: peer_get_process() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((src = host_lookup(arg->src_host)) == NULL) {
		e = GFARM_ERR_UNKNOWN_HOST;
	} else if ((e = process_replica_adding(process, peer,
	    src, spool_host, arg->fd, &inode)) ==
	    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
		if ((e = process_new_generation_wait(peer, arg->fd,
		    replica_adding_resume, arg)) ==
		    GFARM_ERR_NO_ERROR) {
			*suspendedp = 1;
			giant_unlock();
			return (GFARM_ERR_NO_ERROR);
		}
	} else if (e != GFARM_ERR_NO_ERROR)
		;
	else {
		inum = inode_get_number(inode);
		gen = inode_get_gen(inode);
		mtime = inode_get_mtime(inode);
		mtime_sec = mtime->tv_sec;
		mtime_nsec = mtime->tv_nsec;
	}

	/* we don't maintain file_replication in this case */

	sizep = arg->relayed ? &junk : NULL;
	xid = arg->xid;
	free(arg->src_host);
	free(arg);
	giant_unlock();
	return (gfm_server_put_reply(peer, xid, sizep, diag, e, "llli",
	    inum, gen, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_server_replica_adding(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip, int *suspendedp)
{
	gfarm_error_t e;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	struct gfarm_timespec *mtime;
	gfarm_int64_t mtime_sec = 0;
	gfarm_int32_t fd, mtime_nsec = 0;
	struct host *src, *spool_host;
	struct process *process;
	char *src_host;
	struct inode *inode;
	struct replica_adding_resume_arg *arg;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDING";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_ADDING, "s", &src_host);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(src_host);
		return (GFARM_ERR_NO_ERROR);
	}

	if (relay != NULL) {
		free(src_host);
	} else {
		/* do not relay RPC to master gfmd */
		giant_lock();

		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001960,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001961,
			    "peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((process = peer_get_process(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001962,
			    "operation is not permitted: peer_get_process() "
			    "failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
			   GFARM_ERR_NO_ERROR)
			;
		else if ((src = host_lookup(src_host)) == NULL)
			e = GFARM_ERR_UNKNOWN_HOST;
		else if ((e = process_replica_adding(process, peer,
		     src, spool_host, fd, &inode)) ==
			 GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			GFARM_MALLOC(arg);
			if (arg == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				arg->relayed = sizep != NULL;
				arg->xid = xid;
				arg->fd = fd;
				arg->src_host = src_host;
				if ((e = process_new_generation_wait(peer, fd,
				     replica_adding_resume, arg)) ==
				    GFARM_ERR_NO_ERROR) {
					*suspendedp = 1;
					giant_unlock();
					return (GFARM_ERR_NO_ERROR);
				}
			}
		} else if (e != GFARM_ERR_NO_ERROR)
			;
		else {
			inum = inode_get_number(inode);
			gen = inode_get_gen(inode);
			mtime = inode_get_mtime(inode);
			mtime_sec = mtime->tv_sec;
			mtime_nsec = mtime->tv_nsec;
		}

		/* we don't maintain file_replication in this case */

		free(src_host);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, "llli", &inum, &gen, &mtime_sec, &mtime_nsec));
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
		gflog_debug(GFARM_MSG_1001963,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001964,
			"operation is not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001965,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001966,
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
	/* we don't maintain file_replication in this case */
	return (e);
}

/* obsolete protocol */
gfarm_error_t
gfm_server_replica_added(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t flags, mtime_nsec;
	gfarm_int64_t mtime_sec;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_ADDED, "s", "ili",
	    &flags, &mtime_sec, &mtime_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		e = gfm_server_replica_added_common(diag, peer, from_client,
		    flags, mtime_sec, mtime_nsec, -1);
		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_replica_added2(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t flags, mtime_nsec;
	gfarm_int64_t mtime_sec;
	gfarm_off_t size;
	struct relayed_request *relay;
	static const char diag[] = "GFM_PROTO_REPLICA_ADDED2";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_ADDED2, "ilil",
	    &flags, &mtime_sec, &mtime_nsec, &size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();

		e = gfm_server_replica_added_common(diag, peer, from_client,
		    flags, mtime_sec, mtime_nsec, size);

		giant_unlock();
	}
	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

gfarm_error_t
gfm_server_replica_lost(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct host *spool_host = NULL;
	struct inode *inode;
	struct relayed_request *relay;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_REPLICA_LOST";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_LOST, "ll", &inum, &gen);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001970,
			    "operation is not permitted");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001971,
			"operation is not permitted: peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((inode = inode_lookup(inum)) == NULL) {
			gflog_debug(GFARM_MSG_1001972,
			    "inode_lookup() failed");
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if (inode_get_gen(inode) != gen) {
			gflog_debug(GFARM_MSG_1001973,
			    "inode_get_gen() failed");
			e = GFARM_ERR_NO_SUCH_OBJECT;
		} else if (!inode_is_file(inode)) {
			gflog_debug(GFARM_MSG_1001973, "%s: not a file", diag);
			e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		} else if (inode_is_opened_on(inode, spool_host)) {
			/*
			 * http://sourceforge.net/apps/trac/gfarm/ticket/455
			 * http://sourceforge.net/apps/trac/gfarm/ticket/666
			 * race condtion between REOPEN and O_CREAT
			 */
			gflog_debug(GFARM_MSG_1003554, "%s: writing", diag);
			e = GFARM_ERR_FILE_BUSY;
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			e = inode_remove_replica_metadata(inode, spool_host,
			    gen);
			if (transaction)
				db_end(diag);
		}
		giant_unlock();
		if (e == GFARM_ERR_NO_ERROR) {
			gflog_warning(GFARM_MSG_1003485,
			    "inode %lld:%lld on %s: invalid metadata deleted",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
		}
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

static gfarm_error_t
dead_file_copy_check(gfarm_ino_t inum, gfarm_uint64_t gen, struct host *host)
{
	struct inode *inode = inode_lookup_including_free(inum);

	if (dead_file_copy_existing(inode_get_dead_copies(inode), gen, host)) {
		gflog_debug(GFARM_MSG_1003486,
		    "%lld:%lld on %s: has dead_file_copy",
		    (long long)inum, (long long)gen, host_name(host));
		return  (GFARM_ERR_FILE_BUSY); /* busy file */
	}
	return (GFARM_ERR_NO_SUCH_OBJECT); /* invalid file */
}

gfarm_error_t
gfm_server_replica_add(struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_off_t size;
	struct host *spool_host;
	struct inode *inode;
	struct file_copy *copy;
	struct relayed_request *relay;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_REPLICA_ADD";

	e = gfm_server_relay_get_request(peer, sizep, skip, &relay, diag,
	    GFM_PROTO_REPLICA_ADD, "lll", &inum, &gen, &size);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1001975,
			    "not permitted : from_client");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* error */
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1001976,
			"operation is not permitted: peer_get_host() failed");
			e = GFARM_ERR_OPERATION_NOT_PERMITTED; /* error */
		} else if ((inode = inode_lookup(inum)) == NULL) {
			gflog_debug(GFARM_MSG_1001977,
			    "inode_lookup() failed");
			e = dead_file_copy_check(inum, gen, spool_host);
		} else if (!inode_is_file(inode)) {
			gflog_debug(GFARM_MSG_1003487,
			    "%lld:%lld on %s: not a regular file",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
			e = dead_file_copy_check(inum, gen, spool_host);
			if (e == GFARM_ERR_NO_SUCH_OBJECT) /* invalid file */
				e = GFARM_ERR_NOT_A_REGULAR_FILE;
		} else if (inode_is_opened_for_writing(inode)) {
			/* include generation updating */
			gflog_debug(GFARM_MSG_1003488,
			    "%lld:%lld on %s: opened for writing",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
			e = GFARM_ERR_FILE_BUSY; /* busy file */
		} else if (inode_get_gen(inode) != gen) {
			/* though this is not opened for writing... */
			gflog_debug(GFARM_MSG_1001978,
			    "inode_get_gen() failed");
			e = dead_file_copy_check(inum, gen, spool_host);
		} else if ((copy = inode_get_file_copy(inode, spool_host))
		    != NULL) {
			/* registered replica */
			if (!file_copy_is_valid(copy)) {
				gflog_debug(GFARM_MSG_1003555,
				    "%lld:%lld on %s: being replicated",
				    (long long)inum, (long long)gen,
				    host_name(spool_host));
				e = GFARM_ERR_FILE_BUSY; /* busy file */
			} else if (file_copy_is_being_removed(copy)) {
				gflog_debug(GFARM_MSG_1003556,
				    "%lld:%lld on %s: being removed",
				    (long long)inum, (long long)gen,
				    host_name(spool_host));
				e = GFARM_ERR_FILE_BUSY; /* busy file */
			} else if (inode_get_size(inode) == size) {
#if 0				/* verbose message */
				gflog_debug(GFARM_MSG_1003489,
				    "%lld:%lld on %s: a correct file",
				    (long long)inum, (long long)gen,
				    host_name(spool_host));
#endif
				/* correct file */
				e = GFARM_ERR_ALREADY_EXISTS;
			} else {
				gflog_warning(GFARM_MSG_1003557,
				    "%lld:%lld on %s: invalid file replica",
				    (long long)inum, (long long)gen,
				    host_name(spool_host));
				/* invalid file */
				e = GFARM_ERR_INVALID_FILE_REPLICA;
			}
		} else if (inode_get_size(inode) != size) {
			gflog_notice(GFARM_MSG_1003558,
			    "%lld:%lld on %s: invalid file replica, rejected",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
			e = GFARM_ERR_INVALID_FILE_REPLICA; /* invalid file */
		} else { /* add a replica */
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			e = inode_add_replica(inode, spool_host, 1);
			if (transaction)
				db_end(diag);
		}
		giant_unlock();
	}

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay, diag,
	    &e, ""));
}

static gfarm_error_t
gfm_server_replica_get_my_entries_common(
	const char *diag, struct peer *peer, gfp_xdr_xid_t xid,
	size_t *sizep, int from_client, int skip, int with_size)
{
	struct peer *mhpeer;
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	int size_pos;
	gfarm_ino_t start_inum, inum, table_size;
	int i, n_req, n_ret = 0;
	struct host *spool_host;
	struct inode *inode;
	struct entry_result {
		gfarm_ino_t inum;
		gfarm_uint64_t gen;
		gfarm_off_t size;
		int flags;
	} *ents = NULL;

	e_ret = gfm_server_get_request(peer, sizep, diag,
	    "li", &start_inum, &n_req);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	e_rpc = wait_db_update_info(peer, DBUPDATE_FS, diag);
	if (e_rpc != GFARM_ERR_NO_ERROR) {
		gflog_error(GFARM_MSG_UNFIXED,
		    "%s: failed to wait for the backend DB to be updated: %s",
		    diag, gfarm_error_string(e_rpc));
		/* Continue processing. */
	}

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1003490, "not permitted: from_client");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1003491,
		    "not permitted: peer_get_host() failed");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (n_req <= 0) {
		gflog_debug(GFARM_MSG_1003492, "n_req is 0");
		e_rpc = GFARM_ERR_INVALID_ARGUMENT;
	} else if (GFARM_MALLOC_ARRAY(ents, n_req) == NULL) {
		gflog_debug(GFARM_MSG_1003493, "no memory");
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else {
		if (start_inum < inode_root_number())
			start_inum = inode_root_number();
		table_size = inode_table_current_size();
		e_rpc = GFARM_ERR_NO_SUCH_OBJECT;
		for (inum = start_inum; inum < table_size && n_ret < n_req;
		    inum++) {
			inode = inode_lookup(inum);
			if (inode && inode_is_file(inode) &&
			    inode_has_file_copy(inode, spool_host)) {
				ents[n_ret].inum = inode_get_number(inode);
				ents[n_ret].gen = inode_get_gen(inode);
				ents[n_ret].size = inode_get_size(inode);
				e_rpc = GFARM_ERR_NO_ERROR;
				n_ret++;
			}
		}
	}
	giant_unlock();

	e_ret = gfm_server_put_reply_begin(peer, &mhpeer, xid, &size_pos, diag,
	    e_rpc, "i", n_ret);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n_ret; i++) {
			if (with_size)
				e_ret = gfp_xdr_send(client, "lll",
				    ents[i].inum, ents[i].gen, ents[i].size);
			else
				e_ret = gfp_xdr_send(client, "ll",
				    ents[i].inum, ents[i].gen);
			if (e_ret != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1003494,
				    "replica_get_my_entries @%s: %s",
				    peer_get_hostname(peer),
				    gfarm_error_string(e_ret));
				break;
			}
		}
		gfm_server_put_reply_end(peer, mhpeer, diag, size_pos);
	}

	free(ents);
	return (e_ret);
}

gfarm_error_t
gfm_server_replica_get_my_entries(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_REPLICA_GET_MY_ENTRIES";

	return (gfm_server_replica_get_my_entries_common(
	    diag, peer, xid, sizep, from_client, skip, 0));
}

gfarm_error_t
gfm_server_replica_get_my_entries2(
	struct peer *peer, gfp_xdr_xid_t xid, size_t *sizep,
	int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_REPLICA_GET_MY_ENTRIES2";

	return (gfm_server_replica_get_my_entries_common(
	    diag, peer, xid, sizep, from_client, skip, 1));
}

gfarm_error_t
gfm_server_replica_create_file_in_lost_found(struct peer *peer,
	gfp_xdr_xid_t xid, size_t *sizep, int from_client, int skip)
{
	gfarm_error_t e_ret, e_rpc;
	gfarm_ino_t inum_old, inum_new = 0;
	gfarm_uint64_t gen_old, gen_new = 0;
	gfarm_off_t size;
	struct host *spool_host = NULL;
	struct inode *inode;
	struct gfarm_timespec mtime;
	int transaction = 0;
	struct relayed_request *relay;
	static const char diag[]
		= "GFM_PROTO_REPLICA_CREATE_FILE_IN_LOST_FOUND";

	e_ret = gfm_server_relay_get_request(
	     peer, sizep, skip, &relay, diag,
	     GFM_PROTO_REPLICA_CREATE_FILE_IN_LOST_FOUND, "lllli",
	     &inum_old, &gen_old, &size, &mtime.tv_sec, &mtime.tv_nsec);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	if (relay == NULL) {
		/* do not relay RPC to master gfmd */
		giant_lock();
		if (from_client) { /* from gfsd only */
			gflog_debug(GFARM_MSG_1003495,
			    "not permitted: from_client");
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else if ((spool_host = peer_get_host(peer)) == NULL) {
			gflog_debug(GFARM_MSG_1003496,
			    "not permitted: peer_get_host() failed");
			e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
		} else {
			if (db_begin(diag) == GFARM_ERR_NO_ERROR)
				transaction = 1;
			e_rpc = inode_create_file_in_lost_found(
			    spool_host, inum_old, gen_old,
			    size, &mtime, &inode);
			if (e_rpc == GFARM_ERR_NO_ERROR) {
				inum_new = inode_get_number(inode);
				gen_new = inode_get_gen(inode);
			}
			if (transaction)
				db_end(diag);
		}
		giant_unlock();
		if (e_rpc == GFARM_ERR_NO_ERROR) {
			gflog_notice(GFARM_MSG_UNFIXED,
			    "inode %lld:%lld on %s -> %lld:%lld: "
			    "moved to lost+found",
			    (long long)inum_old, (long long)gen_old,
			    host_name(spool_host),
			    (long long)inum_new, (long long)gen_new);
		}
	} else
		e_rpc = GFARM_ERR_NO_ERROR;

	return (gfm_server_relay_put_reply(peer, xid, sizep, relay,
	    diag, &e_rpc, "ll", &inum_new, &gen_new));
}

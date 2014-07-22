#include <pthread.h>	/* db_access.h currently needs this */
#include <assert.h>
#include <stdarg.h> /* for "gfp_xdr.h" */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h> /* for sprintf(), snprintf() */
#include <sys/time.h> /* for gettimeofday() */

#define GFARM_INTERNAL_USE
#include <gfarm/gflog.h>
#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>
#include <gfarm/gfs.h>

#include "gfutil.h"

#include "patmatch.h"
#include "gfp_xdr.h"
#include "auth.h"
#include "gfm_proto.h"

#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "host.h"
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
#include "config.h" /* for gfarm_host_get_self_name(), gfarm_metadb_server_port */

static char dot[] = ".";
static char dotdot[] = "..";

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
	gfarm_error_t e = GFARM_ERR_NO_ERROR, on_error;
	static const char diag[] = "GFM_PROTO_COMPOUND_ON_ERROR";

	e = gfm_server_get_request(peer, diag, "i", &on_error);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001784,
			"compound_on_error request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (level < 1) /* there isn't COMPOUND_BEGIN ... END block around */
		e = GFARM_ERR_INVALID_ARGUMENT;
	else
		*on_errorp = on_error;
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_get_fd(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd = 0;
	struct process *process;
	static const char diag[] = "GFM_PROTO_GET_FD";

	e = gfm_server_get_request(peer, diag, "");
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001785,
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
	static const char diag[] = "GFM_PROTO_PUT_FD";

	e = gfm_server_get_request(peer, diag, "i", &fd);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001786,
			"put_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = process_verify_fd(process, peer, fd, diag))
	    != GFARM_ERR_NO_ERROR)
		;
	else {
		peer_fdpair_set_current(peer, fd, diag);
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
		gflog_debug(GFARM_MSG_1001787,
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
		e = peer_fdpair_save(peer, diag);
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
		gflog_debug(GFARM_MSG_1001788, "restore_fd request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else
		e = peer_fdpair_restore(peer, diag);
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
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
	int created, desired_number, transaction = 0;;
	gfarm_int32_t cfd, fd = -1;

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
	if ((e = process_get_file_inode(process, peer, cfd, &base, diag))
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

		if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
			trace_seq_num = trace_log_get_sequence_number();
			gettimeofday(&tv, NULL);
			if ((e = process_get_path_for_trace_log(process, peer,
			    cfd, &parent_path, diag)) != GFARM_ERR_NO_ERROR) {
				gflog_error(GFARM_MSG_1003286,
				    "process_get_path_for_trace_log() failed: %s",
				    gfarm_error_string(e));
			} else {
				peer_get_port(peer, &peer_port);
				snprintf(tmp_str, sizeof(tmp_str),
				    "%lld/%010ld.%06ld/%s/%s/%d/CREATE/%s/%d//%lld/%lld////\"%s/%s\"///",
				    (unsigned long long)trace_seq_num,
				    (long int)tv.tv_sec,
				    (long int)tv.tv_usec,
				    peer_get_username(peer),
				    peer_get_hostname(peer), peer_port,
				    gfarm_host_get_self_name(),
				    gfarm_metadb_server_port,
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
			process_close_file(process, peer, fd, NULL, diag);
			if (inode_unlink(base, name, process,
				&inodet, &hlink_removed) == GFARM_ERR_NO_ERROR) {
				if (gfarm_file_trace) {
					trace_seq_num =
					    trace_log_get_sequence_number();
					gettimeofday(&tv, NULL);
					if ((e = process_get_path_for_trace_log(
					    process, peer, cfd, &parent_path,
					    diag)) != GFARM_ERR_NO_ERROR) {
						gflog_error(GFARM_MSG_1003287,
						    "process_get_path_for_trace_log() failed: %s",
						    gfarm_error_string(e));
					} else {
						peer_get_port(peer,
						    &peer_port);
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
						    gfarm_host_get_self_name(), gfarm_metadb_server_port,
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
		if (inode_has_desired_number(inode, &desired_number) ||
		    inode_traverse_desired_replica_number(base,
		    &desired_number))
			process_record_desired_number(process, peer, fd,
			    desired_number, diag);
	}

	/* set full path to file_opening */
	if (gfarm_file_trace) {
		if (strcmp(name, dot) != 0 && strcmp(name, dotdot) != 0) {
			if ((e = process_get_path_for_trace_log(
			    process, peer, cfd, &parent_path, diag))
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

	peer_fdpair_set_current(peer, fd, diag);
	*inump = inode_get_number(inode);
	*genp = inode_get_gen(inode);
	*modep = inode_get_mode(inode);

	if (gfarm_file_trace) {
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
				    process, peer, fd, child_path, diag))
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

	return(GFARM_ERR_NO_ERROR);
}

gfarm_error_t
gfm_server_create(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	char *name;
	gfarm_int32_t flag, perm;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0;
	char *create_log = NULL, *remove_log = NULL;
	static const char diag[] = "GFM_PROTO_CREATE";

	e = gfm_server_get_request(peer, diag, "sii", &name, &flag, &perm);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001799, "create request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
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
	e2 = gfm_server_put_reply(peer, diag, e, "lli",
	    inum, gen, mode);

	if (gfarm_file_trace && create_log != NULL) {
		gflog_trace(GFARM_MSG_1003293, "%s", create_log);
		free(create_log);
	}
	if (gfarm_file_trace && remove_log != NULL) {
		gflog_trace(GFARM_MSG_1003294, "%s", remove_log);
		free(remove_log);
	}

	return (e2);
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
		gflog_debug(GFARM_MSG_1001800, "open request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	e = gfm_server_open_common(diag, peer, from_client,
	    name, flag, 0, 0, &inum, &gen, &mode, NULL, NULL);

	if (debug_mode) {
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_info(GFARM_MSG_1000378, "open(%s) -> error: %s",
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
		gflog_debug(GFARM_MSG_1001801, "open_root request failed: %s",
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
		gflog_debug(GFARM_MSG_1001802, "process_open_file() failed: %s",
			gfarm_error_string(e));
	} else
		peer_fdpair_set_current(peer, fd, diag);

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
		gflog_debug(GFARM_MSG_1001803,
			"open_parent request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (flag & ~GFARM_FILE_USER_MODE) {
		gflog_debug(GFARM_MSG_1001804, "argument 'flag' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((op = accmode_to_op(flag)) & GFS_W_OK) {
		gflog_debug(GFARM_MSG_1001805, "inode is a directory");
		e = GFARM_ERR_IS_A_DIRECTORY;
	} else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
		gflog_debug(GFARM_MSG_1001808, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &base, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001809, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = inode_lookup_parent(base, process, op, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001810, "inode_lookup_parent() failed"
			": %s", gfarm_error_string(e));
	} else if ((e = process_open_file(process, inode, flag, 0,
	    peer, spool_host, &fd)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001811, "process_open_file() failed: "
			"%s", gfarm_error_string(e));
	} else
		peer_fdpair_set_current(peer, fd, diag);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_fhopen(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	gfarm_uint32_t flag;
	struct process *process;
	struct inode *inode;
	struct user *user;
	gfarm_int32_t fd;
	gfarm_int32_t mode = 0;
	static const char diag[] = "GFM_PROTO_FHOPEN";
	char *msg;

	e = gfm_server_get_request(peer, diag, "lli", &inum, &gen, &flag);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003764,
		    "%s: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (!from_client || (process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		msg = "no such process";
	} else if ((user = process_get_user(process)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		msg = "no such user";
	} else if ((inode = inode_lookup(inum)) == NULL) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		msg = "no such inode";
	} else if (!user_is_root(inode, user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		msg = "not gfarmroot";
	} else if (inode_get_gen(inode) != gen) {
		e = GFARM_ERR_NO_SUCH_OBJECT;
		msg = "generation mismatch";
	} else if  (flag & ~GFARM_FILE_USER_MODE) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		msg = "invalid flag";
	} else if ((e = process_open_file(process, inode, flag, 0, peer,
		    NULL, &fd)) != GFARM_ERR_NO_ERROR)
		msg = "process_open_file";
	else {
		peer_fdpair_set_current(peer, fd, diag);
		mode = inode_get_mode(inode);
	}
	giant_unlock();
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1003765, "%s: %lld:%lld: %s: %s", diag,
		  (long long)inum, (long long)gen, msg, gfarm_error_string(e));

	return (gfm_server_put_reply(peer, diag, e, "i", mode));
}

gfarm_error_t
gfm_server_close(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_int32_t fd = -1;
	int transaction = 0;
	char *trace_log = NULL;
	static const char diag[] = "GFM_PROTO_CLOSE";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_close_file(process, peer, fd, &trace_log, diag);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "");
	if (gfarm_file_trace && trace_log != NULL) {
		gflog_trace(GFARM_MSG_1003295, "%s", trace_log);
		free(trace_log);
	}
	return (e2);
}

static gfarm_error_t
gfm_server_verify_type_common(struct peer *peer, int from_client, int skip,
	int tf, const char *diag)
{
	gfarm_error_t e;
	struct process *process;
	gfarm_uint32_t type;
	gfarm_int32_t cfd;
	struct inode *inode;
	gfarm_mode_t mode = 0;

	e = gfm_server_get_request(peer, diag, "i", &type);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001815,
			"verify_type request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002844,
			"operation is not permitted : peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002845, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &inode, diag
	    )) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002846, "process_get_file_inode() "
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
			(tf ? GFARM_ERR_NO_ERROR : GFARM_ERR_IS_A_DIRECTORY) :
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

	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_verify_type(struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE";
	return (gfm_server_verify_type_common(
		peer, from_client, skip, 1, diag));
}

gfarm_error_t
gfm_server_verify_type_not(struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_VERIFY_TYPE_NOT";
	return (gfm_server_verify_type_common(
		peer, from_client, skip, 0, diag));
}

gfarm_error_t
gfm_server_revoke_gfsd_access(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct file_opening *fo = NULL;
	struct process *process = NULL;
	gfarm_pid_t pid_for_logging;
	gfarm_ino_t inum_for_logging;
	gfarm_uint64_t igen_for_logging;
	int done = 0;
	static const char diag[] = "GFM_PROTO_REVOKE_GFSD_ACCESS";

	if ((e = gfm_server_get_request(peer, diag, "i", &fd))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002847,
		    "revoke_fd_host failed : %s", gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();
	if (!from_client) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002848,
			"%s", gfarm_error_string(e));
	} else if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002849,
			"%s", gfarm_error_string(e));
	} else if ((e = process_get_file_opening(process, peer, fd, &fo, diag))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002850,
			"%s", gfarm_error_string(e));
	} else if ((fo->flag & GFARM_FILE_ACCMODE) != GFARM_FILE_RDONLY) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002851,
			"%s", gfarm_error_string(e));
	} else if (fo->opener != NULL) {
		fo->u.f.spool_opener = NULL;
		fo->u.f.spool_host = NULL;
		fo->flag |= GFARM_FILE_GFSD_ACCESS_REVOKED;
		done = 1;
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
	gflog_info(GFARM_MSG_1003800,
	    "%s:%s: pid:%lld fd:%d inode:%llu:%llu %s request: %s",
	    peer_get_username(peer), peer_get_hostname(peer),
	    (long long)pid_for_logging, (int)fd,
	    (long long)inum_for_logging, (long long)igen_for_logging,
	    diag, e != GFARM_ERR_NO_ERROR ? gfarm_error_string(e) :
	    done ? "done" : "ignored");
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) == GFARM_ERR_NO_ERROR)
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
gfm_server_recv_attrpatterns(struct peer *peer, int skip,
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
		e = gfp_xdr_recv(client, 0, &eof, "s", &attrpattern);
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
gfm_server_fgetattrplus(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
	gfarm_int32_t flags, nattrpatterns, fd;
	char **attrpatterns = NULL;
	int i, j, needs_free = 0;
	struct host *spool_host = NULL;
	struct process *process;
	struct inode *inode;
	struct gfs_stat st;
	size_t nxattrs = 0;
	struct xattr_list *xattrs = NULL, *px;
	struct db_waitctx waitctx;
	static const char diag[] = "GFM_PROTO_FGETATTRPLUS";

#ifdef __GNUC__ /* workaround gcc warning: may be used uninitialized */
	memset(&st, 0, sizeof(st));
#endif

	e_ret = gfm_server_get_request(peer, diag, "ii",
	    &flags, &nattrpatterns);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);

	e_ret = gfm_server_recv_attrpatterns(peer, skip, nattrpatterns,
	    &attrpatterns, diag);
	/* don't have to free attrpatterns in the return case */
	if (e_ret != GFARM_ERR_NO_ERROR || skip)
		return (e_ret);

	/* NOTE: attrpatterns may be NULL here in case of memory shortage */

	giant_lock();

	if (attrpatterns == NULL)
		e_rpc = GFARM_ERR_NO_MEMORY;
	else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
	} else if ((e_rpc = process_get_file_inode(process, peer, fd, &inode,
	    diag)) != GFARM_ERR_NO_ERROR) {
	} else if ((e_rpc = inode_get_stat(inode, &st)) !=
	    GFARM_ERR_NO_ERROR) {
	} else {
		needs_free = 1;
		e_rpc = inode_xattr_list_get_cached_by_patterns(
		    st.st_ino, attrpatterns, nattrpatterns, &xattrs, &nxattrs);
		for (j = 0; j < nxattrs; j++) {
			px = &xattrs[j];
			if (px->value != NULL) /* cached */
				goto acl_convert;

			/* not cached */
			db_waitctx_init(&waitctx);
			e_rpc = db_xattr_get(0, st.st_ino,
			    px->name, &px->value, &px->size,
			    &waitctx);
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

acl_convert:
			e_rpc = acl_convert_for_getxattr(
				inode, px->name, &px->value, &px->size);
			if (e_rpc != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1002852,
				 "acl_convert_for_getxattr() failed: %s",
				 gfarm_error_string(e_rpc));
				break;
			}
		}
	}

	giant_unlock();
	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "llilsslllililii",
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
		gflog_debug(GFARM_MSG_1001820, "futimes request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
		gflog_debug(GFARM_MSG_1001823, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001824, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((user = process_get_user(process)) == NULL) {
		gflog_debug(GFARM_MSG_1001825, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (user != inode_get_user(inode) &&
	    !user_is_root(inode, user) &&
	    (e = process_get_file_writable(process, peer, fd, diag)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001826, "permission denied");
		e = GFARM_ERR_PERMISSION_DENIED;
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001827, "db_begin() failed: %s",
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
		gflog_debug(GFARM_MSG_1001828, "fchmod request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001833,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if (user != inode_get_user(inode) &&
	    !user_is_root(inode, user)) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001834,
			"operation is not permitted for user");
	} else {
		if (!user_is_root(inode, user) &&
		    !user_in_group(user, inode_get_group(inode)))
			mode &= ~GFARM_S_ISGID; /* POSIX requirement */
		e = inode_set_mode(inode, mode);
	}

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
		gflog_debug(GFARM_MSG_1001835, "fchown request failed: %s",
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
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
	static const char diag[] = "GFM_PROTO_CKSUM_GET";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
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
		gflog_debug(GFARM_MSG_1001848, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_cksum_get(process, peer, fd,
	    &cksum_type, &cksum_len, &cksum, &flags, diag))
	    != GFARM_ERR_NO_ERROR) {
		/* We cannot access cksum_type and cksum outside of giant */
		gflog_debug(GFARM_MSG_1001849,
			"process_cksum_get() failed: %s",
			gfarm_error_string(e));
	} else if (cksum_type == NULL)
		cksum_len = 0;
	else if ((cksum_type = strdup_log(cksum_type, diag)) == NULL)
		e = GFARM_ERR_NO_MEMORY;
	else if (cksum_len > 0) {
		GFARM_MALLOC_ARRAY(cksumbuf, cksum_len);
		if (cksumbuf == NULL)
			e = GFARM_ERR_NO_MEMORY;
		else
			memcpy(cksumbuf, cksum, cksum_len);
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "sbi",
	    cksum_type == NULL ? "" : cksum_type, cksum_len,
	    cksumbuf == NULL ? "" : cksumbuf, flags);
	free(cksum_type);
	free(cksumbuf);
	return (e2);
}

gfarm_error_t
gfm_server_cksum_set(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, flags;
	struct process *process;
	struct inode *inode;
	struct user *user;
	char *cksum_type;
	size_t cksum_len;
	char cksum[GFM_PROTO_CKSUM_MAXLEN];
	struct gfarm_timespec mtime;
	static const char diag[] = "GFM_PROTO_CKSUM_SET";

	e = gfm_server_get_request(peer, diag, "sbili",
	    &cksum_type, sizeof(cksum), &cksum_len, cksum, &flags,
	    &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001850,
			"cksum_set request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(cksum_type);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1001853,
			"operation is not permitted: peer_get_process() "
			"failed");
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001854, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003801, "process_get_file_inode: %s",
		    gfarm_error_string(e));
	} else if (from_client &&
	    ((user = process_get_user(process)) == NULL ||
	     !user_is_root(inode, user))) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1003802, "user_is_root: (%s) %s",
		    user_name(user), gfarm_error_string(e));
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
		    cksum_type, cksum_len, cksum, flags, &mtime, diag);
		db_end(diag);
	}
	giant_unlock();
	free(cksum_type);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_schedule_file(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e_save;
	char *domain;
	gfarm_int32_t i, fd, nhosts;
	struct host **hosts, *spool_host = NULL;
	struct process *process;
	static const char diag[] = "GFM_PROTO_SCHEDULE_FILE";

	e = gfm_server_get_request(peer, diag, "s", &domain);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001856,
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
		gflog_debug(GFARM_MSG_1001857,
			"function not implemented");
		e = GFARM_ERR_FUNCTION_NOT_IMPLEMENTED; /* XXX FIXME */
	} else if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001858,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001859,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001860, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else {
		e = process_schedule_file(process, peer, fd, &nhosts, &hosts,
		    diag);
	}

	free(domain);
	giant_unlock();

	if (e != GFARM_ERR_NO_ERROR)
		return (gfm_server_put_reply(peer, diag, e, ""));
	
	e_save = gfm_server_put_reply(peer, diag, e, "i", nhosts);
	for (i = 0; i < nhosts; i++) {
		e = host_schedule_reply(hosts[i], peer, diag);
		if (e_save == GFARM_ERR_NO_ERROR)
			e_save = e;
	}
	free(hosts);
	return (e_save);
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

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	char *path;
	int hlink_removed = 0;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;
	int peer_port;
	struct inode_trace_log_info inodet;

	static const char diag[] = "GFM_PROTO_REMOVE";

	e = gfm_server_get_request(peer, diag, "s", &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001864,
			"remove request failed: %s", gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001865,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001866, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001867, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &base, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001868, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001869, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_unlink(base, name, process,
			&inodet, &hlink_removed);
		db_end(diag);
		if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR &&
		    !GFARM_S_ISDIR(inodet.imode)) {
			gettimeofday(&tv, NULL);
			trace_seq_num = trace_log_get_sequence_number();
		}
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "");

	if (gfarm_file_trace && !GFARM_S_ISDIR(inodet.imode)) {
		if ((e = process_get_path_for_trace_log(process, peer, cfd,
		    &path, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003296,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else {
			peer_get_port(peer, &peer_port);
			gflog_trace(GFARM_MSG_1003297,
			    "%lld/%010ld.%06ld/%s/%s/%d/%s/%s/%d//%lld/%lld////\"%s/%s\"///",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    trace_log_get_operation_name(
			    hlink_removed, inodet.imode),
			    gfarm_host_get_self_name(),
			    gfarm_metadb_server_port,
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
gfm_server_rename(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	char *sname, *dname;
	struct process *process;
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

	static const char diag[] = "GFM_PROTO_RENAME";

	e = gfm_server_get_request(peer, diag, "ss", &sname, &dname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001870, "rename request failed: %s",
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
		gflog_debug(GFARM_MSG_1001871,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001872, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001873,
			"peer_fdpair_get_saved() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001874, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, sfd, &sdir, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001875, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, dfd, &ddir, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001876, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001877, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_rename(sdir, sname, ddir, dname, process,
			&srct, &dstt, &dst_removed, &hlink_removed);
		if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
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
	e2 = gfm_server_put_reply(peer, diag, e, "");

	if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR &&
	    !GFARM_S_ISDIR(srct.imode)) {
		if ((e = process_get_path_for_trace_log(process, peer, sfd,
		    &spath, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003298,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_path_for_trace_log(
		    process, peer, dfd, &dpath, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003299,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
			free(spath);
		} else {
			peer_get_port(peer, &peer_port),
			gflog_trace(GFARM_MSG_1003300,
			    "%lld/%010ld.%06ld/%s/%s/%d/MOVE/%s/%d//%lld/%lld////\"%s/%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num_rename,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfarm_metadb_server_port,
			    (unsigned long long)srct.inum,
			    (unsigned long long)srct.igen,
			    spath, sname,
			    dpath, dname);
			if (dst_removed)
				gflog_trace(GFARM_MSG_1003301,
				    "%lld/%010ld.%06ld/%s/%s/%d/%sOW/%s/%d//%lld/%lld////\"%s/%s\"///",
				    (unsigned long long)trace_seq_num_remove,
				    (long int)tv.tv_sec,
				    (long int)tv.tv_usec,
				    peer_get_username(peer),
				    peer_get_hostname(peer), peer_port,
				    trace_log_get_operation_name(
				    hlink_removed, dstt.imode),
				    gfarm_host_get_self_name(),
				    gfarm_metadb_server_port,
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
gfm_server_flink(struct peer *peer, int from_client, int skip)
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

	static const char diag[] = "GFM_PROTO_FLINK";

	e = gfm_server_get_request(peer, diag, "s", &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001878, "flink request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001879,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001880,
			"operation is not permitted: peer_get_process() "
			"failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001881, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_saved(peer, &sfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001882,
			"peer_fdpair_get_saved() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, sfd, &src, diag))
	    != GFARM_ERR_NO_ERROR)  {
		gflog_debug(GFARM_MSG_1001883, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if (!inode_is_file(src)) {
		gflog_debug(GFARM_MSG_1001884,
			"inode is not file");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &dfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001885, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, dfd, &base, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001886, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001887, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_create_link(base, name, process, src);
		db_end(diag);
		if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
			gettimeofday(&tv, NULL);
			trace_seq_num = trace_log_get_sequence_number();
		}
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "");

	if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
		if ((e = process_get_path_for_trace_log(process, peer, sfd,
		    &spath, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003302,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else if ((e = process_get_path_for_trace_log(process, peer,
		    dfd, &dpath, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003303,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
			free(spath);
		} else {
			peer_get_port(peer, &peer_port),
			gflog_trace(GFARM_MSG_1003304,
			    "%lld/%010ld.%06ld/%s/%s/%d/LINK/%s/%d//%lld/%lld////\"%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfarm_metadb_server_port,
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
		gflog_debug(GFARM_MSG_1001888, "mkdir request failed :%s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(name);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001889, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001890, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001891, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &base, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001892, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if (mode & ~GFARM_S_ALLPERM) {
		gflog_debug(GFARM_MSG_1001893, "argument 'mode' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001894, "db_begin() failed: %s",
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

	static const char diag[] = "GFM_PROTO_SYMLINK";

	e = gfm_server_get_request(peer, diag, "ss", &source_path, &name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001895, "symlink request failed: %s",
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
		gflog_debug(GFARM_MSG_1001896, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001897, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001898, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001899, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &base, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001900, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = db_begin(diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001901, "db_begin() failed: %s",
			gfarm_error_string(e));
	} else {
		e = inode_create_symlink(base, name, process,
			source_path, &inodet);
		db_end(diag);
		if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
			gettimeofday(&tv, NULL);
			trace_seq_num = trace_log_get_sequence_number();
		}
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "");

	if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
		gettimeofday(&tv, NULL);
		if ((e = process_get_path_for_trace_log(process, peer, cfd,
		    &dpath, diag)) != GFARM_ERR_NO_ERROR) {
			gflog_error(GFARM_MSG_1003305,
			    "process_get_path_for_trace_log() failed: %s",
			    gfarm_error_string(e));
		} else {
			peer_get_port(peer, &peer_port);
			gflog_trace(GFARM_MSG_1003306,
			    "%lld/%010ld.%06ld/%s/%s/%d/SYMLINK/%s/%d//%lld/%lld////\"%s\"///\"%s/%s\"",
			    (unsigned long long)trace_seq_num,
			    (long int)tv.tv_sec, (long int)tv.tv_usec,
			    peer_get_username(peer),
			    peer_get_hostname(peer), peer_port,
			    gfarm_host_get_self_name(),
			    gfarm_metadb_server_port,
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
		gflog_debug(GFARM_MSG_1001902, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001903, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001904, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001905, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
	} else if ((source_path = inode_get_symlink(inode)) == NULL) {
		gflog_debug(GFARM_MSG_1001906, "invalid argument");
		e = GFARM_ERR_INVALID_ARGUMENT; /* not a symlink */
	} else if ((source_path = strdup_log(source_path, diag)) == NULL) {
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
	char *s = NULL;
	static const char diag[] = "GFM_PROTO_GETDIRPATH";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001908, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001909, "peer_fdpair_get_current() "
			"failed: %s", gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &dir, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001910, "process_get_file_inode() "
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
	struct inode **inodep, Dir *dirp, DirCursor *cursorp, const char *diag)
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001914, "process_get_file_inode() "
			"failed: %s", gfarm_error_string(e));
		return (e);
	} else if ((dir = inode_get_dir(inode)) == NULL) {
		gflog_debug(GFARM_MSG_1001915, "inode_get_dir() failed");
		return (GFARM_ERR_NOT_A_DIRECTORY);
	} else if ((e = process_get_dir_key(process, peer, fd,
		    &key, &keylen, diag)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001916,
			"process_get_dir_key() failed: %s",
			gfarm_error_string(e));
		return (e);
	} else if (key == NULL &&
		 (e = process_get_dir_offset(process, peer, fd,
		    &dir_offset, diag)) != GFARM_ERR_NO_ERROR) {
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
	gfarm_int32_t fd, Dir dir, DirCursor *cursor, int eof,
	const char *diag)
{
	DirEntry entry;
	gfarm_off_t dir_offset;

	if (eof || (entry = dir_cursor_get_entry(dir, cursor)) == NULL) {
		process_clear_dir_key(process, peer, fd, diag);
		dir_offset = dir_get_entry_count(dir);
	} else {
		int namelen;
		char *name = dir_entry_get_name(entry, &namelen);

		process_set_dir_key(process, peer, fd, name, namelen, diag);
		dir_offset = dir_cursor_get_pos(dir, cursor);
	}
	process_set_dir_offset(process, peer, fd, dir_offset, diag);
}

gfarm_error_t
gfm_server_getdirents(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
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

	e_ret = gfm_server_get_request(peer, diag, "i", &n);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor, diag)) != GFARM_ERR_NO_ERROR) {
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
			    &cursor, n == 0, diag);
			if (i > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
		n = i;
	}

	giant_unlock();

	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "i", n);
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
	}
	if (p != NULL) {
		for (i = 0; i < n; i++)
			free(p[i].name);
		free(p);
	}
	return (e_ret);
}

gfarm_error_t
gfm_server_getdirentsplus(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
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

	e_ret = gfm_server_get_request(peer, diag, "i", &n);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor, diag)) != GFARM_ERR_NO_ERROR) {
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
			    &cursor, n == 0, diag);
			if (i > 0) /* XXX is this check necessary? */
				inode_accessed(inode);
		}
		n = i;
	}

	giant_unlock();
	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "i", n);
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
gfm_server_getdirentsplusxattr(struct peer *peer, int from_client, int skip)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
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

	e_ret = gfm_server_get_request(peer, diag, "ii", &n, &nattrpatterns);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);

	e_ret = gfm_server_recv_attrpatterns(peer, skip, nattrpatterns,
	    &attrpatterns, diag);
	/* don't have to free attrpatterns in the return case */
	if (e_ret != GFARM_ERR_NO_ERROR || skip)
		return (e_ret);

	/* NOTE: attrpatterns may be NULL here in case of memory shortage */

	giant_lock();

	if (attrpatterns == NULL) {
		e_rpc = GFARM_ERR_NO_MEMORY;
	} else if ((e_rpc = fs_dir_get(peer, from_client, &n, &process, &fd,
	    &inode, &dir, &cursor, diag)) != GFARM_ERR_NO_ERROR) {
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
			    &cursor, n == 0, diag);
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
				if (px->value != NULL) /* cached */
					goto acl_convert;

				/* not cached */
				db_waitctx_init(&waitctx);
				e_rpc = db_xattr_get(0, pp->st.st_ino,
				    px->name, &px->value, &px->size,
				    &waitctx);
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
acl_convert:
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

	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "i", n);
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
		gflog_debug(GFARM_MSG_1001927, "seek request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001928, "operation is not permitted");
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001931,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((dir = inode_get_dir(inode)) == NULL) {
		gflog_debug(GFARM_MSG_1001932, "inode_get_dir() failed");
		e = GFARM_ERR_NOT_A_DIRECTORY;
	} else if ((e = process_get_dir_offset(process, peer, fd,
	    &current, diag)) != GFARM_ERR_NO_ERROR) {
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
		case 0: break;
		case 1: offset += current; break;
		case 2: offset += max; break;
		default: assert(0);
		}
		if (offset != current) {
			if (offset < 0)
				offset = 0;
			else if (offset > max)
				offset = max;
			process_clear_dir_key(process, peer, fd, diag);
			process_set_dir_offset(process, peer, fd,
			    offset, diag);
		}
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "l", offset));
}

struct reopen_resume_arg {
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
		    &inum, &gen, &mode, &flags, &to_create, diag);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			if ((e = process_new_generation_wait(peer, arg->fd,
			    reopen_resume, arg, diag)) == GFARM_ERR_NO_ERROR) {
				*suspendedp = 1;
				giant_unlock();
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
		trace_seq_num = trace_log_get_sequence_number();
		gettimeofday(&tv, NULL);
	}

	free(arg);
	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "lliii",
	    inum, gen, mode, flags, to_create);

	if (gfarm_file_trace && e == GFARM_ERR_NO_ERROR) {
		gflog_trace(GFARM_MSG_1003307,
		    "%lld/%010ld.%06ld////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    (long int)tv.tv_sec, (long int)tv.tv_usec,
		    gfarm_host_get_self_name(), gfarm_metadb_server_port,
		    host_name(spool_host),
		    (long long int)inum,
		    (long long int)gen);
	}

	return (e2);
}

gfarm_error_t
gfm_server_reopen(struct peer *peer, int from_client, int skip,
	int *suspendedp)
{
	gfarm_error_t e;
	gfarm_int32_t fd;
	struct host *spool_host = NULL;
	struct process *process;
	gfarm_ino_t inum = 0;
	gfarm_uint64_t gen = 0;
	gfarm_int32_t mode = 0, flags = 0, to_create = 0;
	struct reopen_resume_arg *arg;
	int transaction = 0;

	/* for gfarm_file_trace */
	gfarm_error_t e2;
	gfarm_uint64_t trace_seq_num = 0;
	struct timeval tv;

	static const char diag[] = "GFM_PROTO_REOPEN";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1001935, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001936, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001937, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001938,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		e = process_reopen_file(process, peer, spool_host, fd,
		    &inum, &gen, &mode, &flags, &to_create, diag);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			GFARM_MALLOC(arg);
			if (arg == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				arg->fd = fd;
				if ((e = process_new_generation_wait(peer, fd,
				    reopen_resume, arg, diag))
				    == GFARM_ERR_NO_ERROR) {
					*suspendedp = 1;
					giant_unlock();
					return (GFARM_ERR_NO_ERROR);
				}
			}
		}
	}
	if (gfarm_file_trace && to_create && e == GFARM_ERR_NO_ERROR) {
		trace_seq_num = trace_log_get_sequence_number();
		gettimeofday(&tv, NULL);
	}

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "lliii",
	    inum, gen, mode, flags, to_create);

	if (gfarm_file_trace && to_create && e == GFARM_ERR_NO_ERROR) {
		gflog_trace(GFARM_MSG_1003308,
		    "%lld/%010ld.%06ld////REPLICATE/%s/%d/%s/%lld/%lld///////",
		    (long long int)trace_seq_num,
		    (long int)tv.tv_sec, (long int)tv.tv_usec,
		    gfarm_host_get_self_name(),
		    gfarm_metadb_server_port,
		    host_name(spool_host),
		    (long long int)inum,
		    (long long int)gen);
	}

	return (e2);
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
		gflog_debug(GFARM_MSG_1001939, "close_read request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1001940, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001941, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001942, "peer_get_process() failed");
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
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = process_close_file_read(process, peer, fd, &atime, diag);
		if (transaction)
			db_end(diag);
		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

struct close_v2_4_resume_arg {
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
		    &flags, &inum, &old_gen, &new_gen, &trace_log, diag);
		if (transaction)
			db_end(diag);

		if (e_rpc == GFARM_ERR_NO_ERROR) { /* permission ok */
			e_rpc = peer_fdpair_close_current(peer);
		} else if (e_rpc ==
		    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			if ((e_rpc = process_new_generation_wait(peer, arg->fd,
			    close_write_v2_4_resume, arg, diag)) ==
			    GFARM_ERR_NO_ERROR) {
				*suspendedp = 1;
				giant_unlock();
				return (GFARM_ERR_NO_ERROR);
			}
		}
	}
	free(arg);
	giant_unlock();

	if (e_rpc == GFARM_ERR_NO_ERROR && gfarm_file_trace &&
	    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0 &&
	    trace_log != NULL) {
		gflog_trace(GFARM_MSG_1003435, "%s", trace_log);
		free(trace_log);
	}

	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "ill",
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
	struct peer *peer, int from_client,
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
		    flagsp, inump, old_genp, new_genp, trace_logp, diag);
		if (transaction)
			db_end(diag);

		if (e == GFARM_ERR_NO_ERROR) /* permission ok */
			e = peer_fdpair_close_current(peer);
		else if (e == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
			GFARM_MALLOC(arg);
			if (arg == NULL) {
				e = GFARM_ERR_NO_MEMORY;
			} else {
				arg->fd = fd;
				arg->size = size;
				arg->atime = *atime;
				arg->mtime = *mtime;
				if ((e = process_new_generation_wait(peer, fd,
				    close_write_v2_4_resume, arg, diag)) ==
				    GFARM_ERR_NO_ERROR) {
					return (GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE);
				}
			}
		}
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
	    &atime, &mtime, NULL, NULL, NULL, NULL, NULL);

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_close_write_v2_4(struct peer *peer, int from_client, int skip,
	int *suspendedp)
{
	gfarm_error_t e_rpc, e_ret;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t flags;
	gfarm_ino_t inum = 0;
	gfarm_int64_t old_gen = 0, new_gen = 0;
	char *trace_log;
	static const char diag[] = "GFM_PROTO_CLOSE_WRITE_V2_4";

	e_rpc = gfm_server_get_request(peer, diag, "llili",
	    &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e_rpc != GFARM_ERR_NO_ERROR)
		return (e_rpc);
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	e_rpc = gfm_server_close_write_common(diag, peer, from_client, size,
	    &atime, &mtime, &flags, &inum, &old_gen, &new_gen, &trace_log);

	giant_unlock();

	if (e_rpc == GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
		*suspendedp = 1;
		return (GFARM_ERR_NO_ERROR);
	}

	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "ill",
	    flags, old_gen, new_gen);

	if (e_rpc == GFARM_ERR_NO_ERROR && gfarm_file_trace &&
	    (flags & GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED) != 0 &&
	    trace_log != NULL) {
		gflog_trace(GFARM_MSG_1003309, "%s", trace_log);
		free(trace_log);
	}
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
gfm_server_fhclose_read(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct gfarm_timespec atime;
	struct host *spool_host;
	int transaction = 0;
	struct inode *inode;
	static const char diag[] = "GFM_PROTO_FHCLOSE_READ";

	e = gfm_server_get_request(peer, diag, "llli",
	    &inum, &gen, &atime.tv_sec, &atime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003310,
		    "fhclose_read request failed: %s",
		    gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1003311, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1003312, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((inode = inode_lookup(inum)) == NULL) {
		gflog_debug(GFARM_MSG_1003313, "inode_lookup() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;

		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = inode_fhclose_read(inode, &atime);
		if (transaction)
			db_end(diag);
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static gfarm_error_t
gfm_server_fhclose_write_common(const char *diag, struct peer *peer,
	int from_client, gfarm_ino_t inum,
	gfarm_off_t size, struct gfarm_timespec *atime,
	struct gfarm_timespec *mtime, const char *cksum_type, size_t cksum_len,
	const char *cksum, gfarm_int32_t cksum_flags, gfarm_int32_t *flagp,
	gfarm_int64_t *old_genp, gfarm_int64_t *new_genp,
	gfarm_uint64_t *cookiep)
{
	gfarm_error_t e;
	int transaction = 0;
	int generation_updated = 0;
	struct inode *inode;

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1003314, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (peer_get_host(peer) == NULL) {
		gflog_debug(GFARM_MSG_1003315, "peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((inode = inode_lookup(inum)) == NULL) {
		gflog_debug(GFARM_MSG_1003316, "inode_lookup() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/*
		 * closing must be done regardless of the result of db_begin().
		 * because not closing may cause descriptor leak.
		 */
		e = inode_fhclose_write(inode, size, atime, mtime,
		    cksum_type, cksum_len, cksum, cksum_flags,
		    old_genp, new_genp, &generation_updated);
		if (e == GFARM_ERR_NO_ERROR && generation_updated) {
			*flagp = GFM_PROTO_CLOSE_WRITE_GENERATION_UPDATE_NEEDED;
			*cookiep = peer_add_cookie(peer);
		}
		if (transaction)
			db_end(diag);
	}
	giant_unlock();
	return (e);
}

gfarm_error_t
gfm_server_fhclose_write(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t flags = 0;
	gfarm_int64_t old_gen, new_gen;
	gfarm_uint64_t cookie = 0;
	static const char diag[] = "GFM_PROTO_FHCLOSE_WRITE";

	e = gfm_server_get_request(peer, diag, "llllili",
	    &inum, &old_gen, &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	new_gen = old_gen;
	e = gfm_server_fhclose_write_common(diag, peer, from_client,
	    inum, size, &atime, &mtime, NULL, 0, NULL, 0,
	    &flags, &old_gen, &new_gen, &cookie);

	return (gfm_server_put_reply(peer, diag, e, "illl",
	    flags, old_gen, new_gen, cookie));
}

gfarm_error_t
gfm_server_fhclose_write_cksum(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_off_t size;
	struct gfarm_timespec atime, mtime;
	gfarm_int32_t flags = 0;
	gfarm_int64_t old_gen, new_gen;
	gfarm_uint64_t cookie = 0;
	gfarm_int32_t cksum_len, cksum_flags;
	char *cksum_type, cksum[GFM_PROTO_CKSUM_MAXLEN];
	static const char diag[] = "GFM_PROTO_FHCLOSE_WRITE_CKSUM";

	e = gfm_server_get_request(peer, diag, "llllilisbi",
	    &inum, &old_gen, &size,
	    &atime.tv_sec, &atime.tv_nsec, &mtime.tv_sec, &mtime.tv_nsec,
	    &cksum_type, sizeof(cksum), &cksum_len, cksum, &cksum_flags);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (skip) {
		free(cksum_type);
		return (GFARM_ERR_NO_ERROR);
	}
	new_gen = old_gen;
	e = gfm_server_fhclose_write_common(diag, peer, from_client,
	    inum, size, &atime, &mtime,
	    cksum_type, cksum_len, cksum, cksum_flags,
	    &flags, &old_gen, &new_gen, &cookie);
	free(cksum_type);

	return (gfm_server_put_reply(peer, diag, e, "illl",
	    flags, old_gen, new_gen, cookie));
}

gfarm_error_t
gfm_server_generation_updated(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_int32_t fd, result;
	struct host *spool_host;
	struct process *process;
	struct inode *inode;
	static const char diag[] = "GFM_PROTO_GENERATION_UPDATED";

	e = gfm_server_get_request(peer, diag, "i", &result);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002265, "%s request failed: %s",
		    diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1002266, "%s: from client", diag);
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
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003483,
		    "%s: process_get_file_inode() failed: %s",
		    diag, gfarm_error_string(e));
	} else if ((e = process_new_generation_done(process, peer, fd, result,
	    diag)) != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1002270,
		    "%s: host %s, fd %d: new generation wakeup(%s): %s\n",
		    diag, host_name(spool_host), fd,
		    gfarm_error_string(result), gfarm_error_string(e));
	} else if (result != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003484,
		    "%s: inode %lld:%lld on host %s, fd %d: "
		    "new generation rename: %s\n",
		    diag,
		    (long long)inode_get_number(inode),
		    (long long)inode_get_gen(inode),
		    host_name(spool_host), fd,
		    gfarm_error_string(result));
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_generation_updated_by_cookie(struct peer *peer, int from_client,
    int skip)
{
	gfarm_error_t e;
	gfarm_uint64_t cookie;
	gfarm_int32_t result;
	struct host *spool_host;
	static const char diag[] = "GFM_PROTO_GENERATION_UPDATED_BY_COOKIE";

	e = gfm_server_get_request(peer, diag, "li", &cookie, &result);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002265, "%s request failed: %s",
		    diag, gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1003317, "%s: from client", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1003318,
		    "%s: peer_get_host() failed", diag);
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (!peer_delete_cookie(peer, cookie)) {
		gflog_debug(GFARM_MSG_1003319,
		    "%s: peer_delete_cookie() failed", diag);
		e = GFARM_ERR_BAD_COOKIE;
	} else if (result != GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003702,
		    "%s: host %s, cookie %lld: "
		    "new generation rename: %s\n",
		    diag, host_name(spool_host), (long long)cookie,
		    gfarm_error_string(result));
	}

	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, ""));
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
	gfarm_int32_t n = 0;
	struct inode *inode;
	char **hosts;
	static const char diag[] = "GFM_PROTO_REPLICA_LIST_BY_NAME";

	if (skip)
		return (GFARM_ERR_NO_ERROR);
	giant_lock();

	if (!from_client && (spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001948, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001949, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001950,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001951,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else
		e = inode_replica_list_by_name(inode, &n, &hosts);

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	/* if network error doesn't happen, e2 == e here */
	if (e2 == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e2 = gfp_xdr_send(peer_get_conn(peer), "s", hosts[i]);
			if (e2 != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	if (e == GFARM_ERR_NO_ERROR) {
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
	int transaction = 0;
	struct file_opening *fo;
	static const char diag[] = "GFM_PROTO_REPLICA_REMOVE_BY_FILE";

	e = gfm_server_get_request(peer, diag, "s", &hostname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001952,
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
		gflog_debug(GFARM_MSG_1001953, "peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if (process_get_user(process) == NULL) {
		gflog_debug(GFARM_MSG_1001954, "process_get_user() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &cfd)) !=
		GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001955,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, cfd, &inode, diag
	    )) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001956,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((host = host_lookup(hostname)) == NULL) {
		gflog_debug(GFARM_MSG_1001957, "host_lookup() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if ((spool_host = inode_writing_spool_host(inode)) != NULL &&
		spool_host == host) {
		gflog_debug(GFARM_MSG_1001958,
			"inode_writing_spool_host() failed");
		e = GFARM_ERR_TEXT_FILE_BUSY;
	} else if ((e = process_get_file_opening(process, peer, cfd, &fo, diag)
	    ) != GFARM_ERR_NO_ERROR) {
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
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_replica_info_get(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e, e2;
	struct host *spool_host;
	struct process *process;
	int fd, i;
	gfarm_int32_t iflags, n = 0;
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
	else if ((e = process_get_file_inode(process, peer, fd, &inode, diag))
	    != GFARM_ERR_NO_ERROR)
		;
	else
		e = inode_replica_info_get(inode, iflags,
		    &n, &hosts, &gens, &oflags);

	giant_unlock();
	e2 = gfm_server_put_reply(peer, diag, e, "i", n);
	/* if network error doesn't happen, e2 == e here */
	if (e2 == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < n; ++i) {
			e2 = gfp_xdr_send(peer_get_conn(peer), "sli",
			    hosts[i], gens[i], oflags[i]);
			if (e2 != GFARM_ERR_NO_ERROR)
				break;
		}
	}
	if (e == GFARM_ERR_NO_ERROR) {
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
	src = NULL;
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
	    cfd, flags, &fr, &inode, diag)) != GFARM_ERR_NO_ERROR)
		;
	else {
		srcport = host_port(src);
		ino = inode_get_number(inode);
		gen = inode_get_gen(inode);
	}

	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR) {
		/*
		 * host_name() is always callable without giant_lock,
		 * and even accessible after the removal of the host.
		 */
		e = async_back_channel_replication_request(
		    host_name(src), srcport, dst, ino, gen, fr);
		if (e != GFARM_ERR_NO_ERROR) {
			giant_lock();
			file_replicating_free_by_error_before_request(fr);
			giant_unlock();
		}
	}
	free(srchost);
	free(dsthost);

	return (gfm_server_put_reply(peer, diag, e, ""));
}

struct replica_adding_resume_arg {
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
	    src, spool_host, arg->fd, &inode, diag)) ==
	    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
		if ((e = process_new_generation_wait(peer, arg->fd,
		    replica_adding_resume, arg, diag)) ==
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

	/* we don't maintain file_replicating in this case */

	free(arg->src_host);
	free(arg);
	giant_unlock();
	return (gfm_server_put_reply(peer, diag, e, "llli",
	    inum, gen, mtime_sec, mtime_nsec));
}

gfarm_error_t
gfm_server_replica_adding(struct peer *peer, int from_client, int skip,
	int *suspendedp)
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
	static const char diag[] = "GFM_PROTO_REPLICA_ADDING";

	e = gfm_server_get_request(peer, diag, "s", &src_host);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001959,
			"%s request: %s", diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(src_host);
		return (GFARM_ERR_NO_ERROR);
	}
	giant_lock();

	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1001960,
			"operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001961, "peer_get_host() failed");
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
	    src, spool_host, fd, &inode, diag)) ==
	    GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE) {
		GFARM_MALLOC(arg);
		if (arg == NULL) {
			e = GFARM_ERR_NO_MEMORY;
		} else {
			arg->fd = fd;
			arg->src_host = src_host;
			if ((e = process_new_generation_wait(peer, fd,
			    replica_adding_resume, arg, diag)) ==
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
		    flags, mtime_sec, mtime_nsec, size, diag);
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
		gflog_debug(GFARM_MSG_1001967, "%s request failed: %s",
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
		gflog_debug(GFARM_MSG_1001968, "%s request failed: %s",
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
gfm_server_replica_lost(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	gfarm_ino_t inum;
	gfarm_uint64_t gen;
	struct host *spool_host = NULL;
	struct inode *inode;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_REPLICA_LOST";

	e = gfm_server_get_request(peer, diag, "ll", &inum, &gen);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001969,
			"replica_remove request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1001970, "operation is not permitted");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1001971,
			"operation is not permitted: peer_get_host() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((inode = inode_lookup(inum)) == NULL) {
		gflog_debug(GFARM_MSG_1001972, "inode_lookup() failed");
		e = GFARM_ERR_NO_SUCH_OBJECT;
	} else if (inode_get_gen(inode) != gen) {
		gflog_debug(GFARM_MSG_1001973, "inode_get_gen() failed");
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
		e = inode_remove_replica_metadata(inode, spool_host, gen);
		if (transaction)
			db_end(diag);
	}
	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR) {
		gflog_warning(GFARM_MSG_1003485,
		    "inode %lld:%lld on %s: invalid metadata deleted",
		    (long long)inum, (long long)gen, host_name(spool_host));
	}

	return (gfm_server_put_reply(peer, diag, e, ""));
}

static gfarm_error_t
dead_file_copy_check(gfarm_ino_t inum, gfarm_uint64_t gen, struct host *host)
{
	if (dead_file_copy_existing(inum, gen, host)) {
		gflog_debug(GFARM_MSG_1003486,
		    "%lld:%lld on %s: has dead_file_copy",
		    (long long)inum, (long long)gen, host_name(host));
		return  (GFARM_ERR_FILE_BUSY); /* busy file */
	}
	return (GFARM_ERR_NO_SUCH_OBJECT); /* invalid file */
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
	struct file_copy *copy;
	int transaction = 0;
	static const char diag[] = "GFM_PROTO_REPLICA_ADD";

	e = gfm_server_get_request(peer, diag, "lll", &inum, &gen, &size);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001974,
			"replica_add request failed: %s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip)
		return (GFARM_ERR_NO_ERROR);

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
		gflog_debug(GFARM_MSG_1001977, "inode_lookup() failed");
		e = dead_file_copy_check(inum, gen, spool_host);
	} else if (!inode_is_file(inode)) {
		gflog_debug(GFARM_MSG_1003487,
		    "%lld:%lld on %s: not a regular file",
		    (long long)inum, (long long)gen, host_name(spool_host));
		e = dead_file_copy_check(inum, gen, spool_host);
		if (e == GFARM_ERR_NO_SUCH_OBJECT)
			e = GFARM_ERR_NOT_A_REGULAR_FILE; /* invalid file */
	} else if (inode_is_opened_for_writing(inode)) {
		/* include generation updating */
		gflog_debug(GFARM_MSG_1003488,
		    "%lld:%lld on %s: opened for writing",
		    (long long)inum, (long long)gen, host_name(spool_host));
		e = GFARM_ERR_FILE_BUSY; /* busy file */
	} else if (inode_get_gen(inode) != gen) {
		/* though this is not opened for writing... */
		gflog_debug(GFARM_MSG_1001978, "inode_get_gen() failed");
		e = dead_file_copy_check(inum, gen, spool_host);
	} else if ((copy = inode_get_file_copy(inode, spool_host)) != NULL) {
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
#if 0			/* verbose message */
			gflog_debug(GFARM_MSG_1003489,
			    "%lld:%lld on %s: a correct file",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
#endif
			e = GFARM_ERR_ALREADY_EXISTS; /* correct file */
		} else {
			gflog_warning(GFARM_MSG_1003557,
			    "%lld:%lld on %s: invalid file replica",
			    (long long)inum, (long long)gen,
			    host_name(spool_host));
			e = GFARM_ERR_INVALID_FILE_REPLICA; /* invalid file */
		}
	} else if (inode_get_size(inode) != size) {
		gflog_notice(GFARM_MSG_1003558,
		    "%lld:%lld on %s: invalid file replica, rejected",
		    (long long)inum, (long long)gen, host_name(spool_host));
		e = GFARM_ERR_INVALID_FILE_REPLICA; /* invalid file */
	} else { /* add a replica */
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		e = inode_add_replica(inode, spool_host, 1);
		if (transaction)
			db_end(diag);
	}
	giant_unlock();

	return (gfm_server_put_reply(peer, diag, e, ""));
}

static gfarm_error_t
gfm_server_replica_get_my_entries_common(
	const char *diag, struct peer *peer,
	int from_client, int skip, int with_size)
{
	struct gfp_xdr *client = peer_get_conn(peer);
	gfarm_error_t e_ret, e_rpc;
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

	e_ret = gfm_server_get_request(peer, diag, "li", &start_inum, &n_req);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

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

	e_ret = gfm_server_put_reply(peer, diag, e_rpc, "i", n_ret);
	/* if network error doesn't happen, e_ret == e_rpc here */
	if (e_ret == GFARM_ERR_NO_ERROR)
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
	free(ents);
	return (e_ret);
}

gfarm_error_t
gfm_server_replica_get_my_entries(
	struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_REPLICA_GET_MY_ENTRIES";

	return (gfm_server_replica_get_my_entries_common(
	    diag, peer, from_client, skip, 0));
}

gfarm_error_t
gfm_server_replica_get_my_entries2(
	struct peer *peer, int from_client, int skip)
{
	static const char diag[] = "GFM_PROTO_REPLICA_GET_MY_ENTRIES2";

	return (gfm_server_replica_get_my_entries_common(
	    diag, peer, from_client, skip, 1));
}

gfarm_error_t
gfm_server_replica_create_file_in_lost_found(struct peer *peer,
	int from_client, int skip)
{
	gfarm_error_t e_ret, e_rpc;
	gfarm_ino_t inum_old, inum_new = 0;
	gfarm_uint64_t gen_old, gen_new = 0;
	gfarm_off_t size;
	struct host *spool_host = NULL;
	struct inode *inode;
	struct gfarm_timespec mtime;
	int transaction = 0;
	static const char diag[]
		= "GFM_PROTO_REPLICA_CREATE_FILE_IN_LOST_FOUND";

	e_ret = gfm_server_get_request(peer, diag, "lllli",
	     &inum_old, &gen_old, &size, &mtime.tv_sec, &mtime.tv_nsec);
	if (e_ret != GFARM_ERR_NO_ERROR)
		return (e_ret);
	if (skip)
		return (GFARM_ERR_NO_ERROR);

	giant_lock();
	if (from_client) { /* from gfsd only */
		gflog_debug(GFARM_MSG_1003495, "not permitted: from_client");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((spool_host = peer_get_host(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1003496,
		    "not permitted: peer_get_host() failed");
		e_rpc = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		e_rpc = inode_create_file_in_lost_found(
			spool_host, inum_old, gen_old, size, &mtime, &inode);
		if (e_rpc == GFARM_ERR_NO_ERROR) {
			inum_new = inode_get_number(inode);
			gen_new = inode_get_gen(inode);
		}
		if (transaction)
			db_end(diag);
	}
	giant_unlock();
	if (e_rpc == GFARM_ERR_NO_ERROR) {
		gflog_notice(GFARM_MSG_1003497,
		    "inode %lld:%lld on %s -> %lld:%lld: "
		    "moved to lost+found",
		    (long long)inum_old, (long long)gen_old,
		    host_name(spool_host),
		    (long long)inum_new, (long long)gen_new);
	}

	return (gfm_server_put_reply(peer, diag, e_rpc, "ll",
	    inum_new, gen_new));
}

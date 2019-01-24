/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h> /* fd_set for "filetab.h" */
#include <sys/socket.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"
#include "thrsubr.h"

#include "config.h"	/* gfarm_metadb_admin_user */
#include "auth.h"
#include "gfp_xdr.h"
#include "xattr_info.h"
#include "repattr.h"

#include "subr.h"
#include "rpcsubr.h"
#include "db_access.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "metadb_common.h"
#include "dir.h"
#include "acl.h"
#include "user.h"
#include "host.h"
#include "replica_check.h"
#include "gfm_proto.h"

static gfarm_error_t
xattr_inherit_common(struct inode *parent, struct inode *child,
		     const char *name, void **value_p, size_t *size_p)
{
	gfarm_error_t e;

	*value_p = NULL;
	e = inode_xattr_get_cache(parent, 0, name, value_p, size_p);
	if (e == GFARM_ERR_NO_SUCH_OBJECT || *value_p == NULL)
		return (GFARM_ERR_NO_ERROR);
	else if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003027,
			    "inode_xattr_get_cache(%s) failed: %s",
			    name, gfarm_error_string(e));
		return (e);
	}

	e = inode_xattr_add(child, 0, name, *value_p, *size_p);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003028,
			    "inode_xattr_add(%lld, %s): %s",
			    (unsigned long long)inode_get_number(child),
			    name, gfarm_error_string(e));
		return (e);
	}
	return (GFARM_ERR_NO_ERROR);
}

gfarm_error_t
xattr_inherit(struct inode *parent, struct inode *child,
	      void **acl_def_p, size_t *acl_def_size_p,
	      void **acl_acc_p, size_t *acl_acc_size_p,
	      void **root_user_p, size_t *root_user_size_p,
	      void **root_group_p, size_t *root_group_size_p)
{
	gfarm_error_t e = acl_inherit_default_acl(parent, child,
						  acl_def_p, acl_def_size_p,
						  acl_acc_p, acl_acc_size_p);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003029,
			    "acl_inherit_default_acl() failed: %s",
			    gfarm_error_string(e));
		return (e);
	}

	if (inode_is_symlink(child))
		return (GFARM_ERR_NO_ERROR);  /* not inherit */

	e = xattr_inherit_common(parent, child, GFARM_ROOT_EA_USER,
			     root_user_p, root_user_size_p);
	if (e != GFARM_ERR_NO_ERROR)
		goto error;
	e = xattr_inherit_common(parent, child, GFARM_ROOT_EA_GROUP,
			     root_group_p, root_group_size_p);
	if (e != GFARM_ERR_NO_ERROR)
		goto error;

	return (GFARM_ERR_NO_ERROR);
error:
	if (*acl_def_p != NULL)
		free(*acl_def_p);
	if (*acl_acc_p != NULL)
		free(*acl_acc_p);
	if (*root_user_p != NULL)
		free(*root_user_p);
	if (*root_group_p != NULL)
		free(*root_group_p);

	return (e);
}

static int
user_is_owner_or_root(struct inode *inode, struct user *user)
{
	return (user == inode_get_user(inode) ||
	    user_is_root_for_inode(user, inode));
}

#define XATTR_OP_GET	1
#define XATTR_OP_SET	2
#define XATTR_OP_REMOVE	4

static gfarm_error_t
xattr_access(int xmlMode, struct inode *inode, struct user *user,
	     const char *attrname, int op_inode, int op_xattr)
{
	if (xmlMode) { /* any attrname */
		if (inode_is_symlink(inode))
			goto symlink;
		return (inode_access(inode, user, op_inode));
	}

	if (strncmp(attrname, GFARM_EA_PREFIX, GFARM_EA_PREFIX_LEN) == 0) {
		const char *type = attrname + GFARM_EA_PREFIX_LEN;
		if (strcmp(type, GFARM_EA_NCOPY_TYPE) != 0 &&
		    strcmp(type, "md5") != 0 &&
		    strcmp(type, "acl_access") != 0 &&
		    strcmp(type, "acl_default") != 0 &&
		    strcmp(type, GFARM_EA_REPATTR_TYPE) != 0 &&
		    strcmp(type, GFARM_EA_DIRECTORY_QUOTA_TYPE) != 0)
			goto not_supp;
		else if (inode_is_symlink(inode))
			goto symlink;
		else if (op_xattr == XATTR_OP_GET)
			return (GFARM_ERR_NO_ERROR); /* Anyone can get */
		else if (user_is_owner_or_root(inode, user))
			/* XATTR_OP_SET or XATTR_OP_REMOVE */
			return (GFARM_ERR_NO_ERROR);
		else
			goto not_permit;
	} else if (strncmp(GFARM_ROOT_EA_PREFIX, attrname,
			   GFARM_ROOT_EA_PREFIX_LEN) == 0) {
		const char *type = attrname + GFARM_ROOT_EA_PREFIX_LEN;
		if (strcmp("user", type) != 0 && strcmp("group", type) != 0)
			goto not_supp;
		else if (inode_is_symlink(inode))
			goto symlink;
		else if (user_is_root_for_inode(user, inode))
			return (GFARM_ERR_NO_ERROR);
		else
			goto not_permit;
	} else if (strncmp("user.", attrname, 5) == 0 &&
		   strlen(attrname) >= 6) {
		if (inode_is_symlink(inode))
			goto symlink;
		return (inode_access(inode, user, op_inode));
	} else if (strncmp("system.", attrname, 7) == 0) {
		/* workaround for gfarm2fs_fix_acl */
		if (op_xattr == XATTR_OP_SET)
			goto not_supp;
		else if (user_is_owner_or_root(inode, user))
			/* XATTR_OP_GET or XATTR_OP_REMOVE */
			return (GFARM_ERR_NO_ERROR);
		else
			goto not_permit;
	}
	/* else : not supported */
not_supp:
	gflog_debug(GFARM_MSG_1003030,
		    "not supported to modify `%s'", attrname);
	return (GFARM_ERR_OPERATION_NOT_SUPPORTED);
not_permit:
	gflog_debug(GFARM_MSG_1003031,
		    "not permitted to modify `%s'", attrname);
	return (GFARM_ERR_OPERATION_NOT_PERMITTED);
symlink:
	gflog_debug(GFARM_MSG_1003032,
		    "symlinks cannot have `%s'", attrname);
	return (GFARM_ERR_OPERATION_NOT_PERMITTED);
}

static int
isvalid_attrname(const char *attrname)
{
	int namelen = strlen(attrname);
	return ((0 < namelen) && (namelen <= GFARM_XATTR_NAME_MAX));
}

static int
is_string(void *value, size_t size)
{
	char *p = value;
	int i;

	for (i = 0; i < size; i++)
		if (p[i] == '\0')
			return (1);
	return (0);
}

static gfarm_error_t
xattr_check_repattr(
	struct inode *inode, const char *attrname,
	void **valuep, size_t *sizep, int *change)
{
	gfarm_error_t e;
	gfarm_repattr_t *reps = NULL;
	size_t nreps = 0;
	char *repattr = NULL, *repattr2;

	if (!is_string(*valuep, *sizep)) {
		gflog_debug(GFARM_MSG_1004039,
		    "gfarm.replicainfo is not a string");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if ((e = gfarm_repattr_reduce(*valuep, &reps, &nreps))
	    != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004040,
		    "gfarm_repattr_reduce() failed: %s",
		    gfarm_error_string(e));
	} else if (nreps == 0 || reps == NULL) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1004041,
		    "invalid repattr: %s", (char *)(*valuep));
	} else if ((e = gfarm_repattr_stringify(reps, nreps, &repattr))
		   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1004042,
		    "gfarm_repattr_stringify() failed: %s",
		    gfarm_error_string(e));
	} else {
		if (inode_has_repattr(inode, &repattr2)) {
			if (strcmp(repattr, repattr2) == 0)
				*change = 0;
			else
				*change = 1;
			free(repattr2);
		} else
			*change = 1;
		free(*valuep);
		*valuep = repattr;
		*sizep = strlen(repattr) + 1;
	}
	gfarm_repattr_free_all(nreps, reps);
	return (e);
}

static gfarm_error_t
xattr_check_ncopy(
	struct inode *inode, const char *attrname,
	const void *value, size_t size, int *change)
{
	gfarm_error_t e;
	unsigned int n;
	int num_new, num_old, all_digit;

	e = inode_xattr_to_uint(value, size, &n, &all_digit);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003657,
		    "xattr_to_uint for gfarm.ncopy: %s",
		    gfarm_error_string(e));
		return (e);
	}
	num_new = n; /* uint to int */
	if (num_new < 0) {
		gflog_debug(GFARM_MSG_1003658, "overflow for " GFARM_EA_NCOPY);
		return (GFARM_ERR_RESULT_OUT_OF_RANGE);
	}
	if (!all_digit) {
		gflog_debug(GFARM_MSG_1003659,
		    "invalid format for " GFARM_EA_NCOPY);
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (inode_has_desired_number(inode, &num_old) && num_new == num_old) {
		*change = 0;
		gflog_debug(GFARM_MSG_1003660,
		    "gfarm.ncopy=%d: not changed", num_new);
		return (GFARM_ERR_NO_ERROR);
	}
	*change = 1;
	return (GFARM_ERR_NO_ERROR); /* update */
}

static gfarm_error_t
xattr_check_replica_spec(
	struct inode *inode, const char *attrname,
	void **valuep, size_t *sizep, int *have, int *change)
{
	if (strcmp(attrname, GFARM_EA_NCOPY) == 0) {
		*have = 1;
		return (xattr_check_ncopy(
		    inode, attrname, *valuep, *sizep, change));
	} else if (strcmp(attrname, GFARM_EA_REPATTR) == 0) {
		*have = 1;
		return (xattr_check_repattr(
		    inode, attrname, valuep, sizep, change));
	} else {
		*have = 0;
		*change = 0;
		return (GFARM_ERR_NO_ERROR);
	}
}

static gfarm_error_t
xattr_check_acl(
	struct inode *inode, char *attrname,
	void **valuep, size_t *sizep, int *done)
{
	gfarm_error_t e;
	gfarm_acl_type_t acltype;

	if (strcmp(attrname, GFARM_ACL_EA_ACCESS) == 0)
		acltype = GFARM_ACL_TYPE_ACCESS;
	else if (strcmp(attrname, GFARM_ACL_EA_DEFAULT) == 0)
		acltype = GFARM_ACL_TYPE_DEFAULT;
	else {
		*done = 0;
		return (GFARM_ERR_NO_ERROR);
	}

	e = acl_convert_for_setxattr(inode, acltype, valuep, sizep);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003033,
		    "acl_convert_for_setxattr(%s): %s",
		    attrname, gfarm_error_string(e));
		*done = 1;
		return (e);
	}
	/* The *valuep has only version number if size == 4 */
	if (*valuep == NULL || *sizep <= 4) {
		void *value;
		size_t size;

		e = inode_xattr_get_cache(inode, 0, attrname, &value, &size);
		if (e == GFARM_ERR_NO_ERROR) {
			free(value);
			(void)inode_xattr_remove(inode, 0, attrname);
			e = db_xattr_remove(
			    0, inode_get_number(inode), attrname);
			if (e != GFARM_ERR_NO_ERROR)
				gflog_error(GFARM_MSG_1003034,
				    "db_xattr_remove(0, %lld, %s): %s",
				    (long long)inode_get_number(inode),
				    attrname, gfarm_error_string(e));
		}
		*done = 1;
		return (GFARM_ERR_NO_ERROR);
	}
	*done = 0;
	return (GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
xattr_set(int xmlMode, struct inode *inode,
	 char *attrname, void **valuep, size_t *sizep, int flags,
	 struct db_waitctx *waitctx, int *addattr)
{
	gfarm_error_t e;
	int have_replica_spec, change_replica_spec = 0, done;
	int size_limit;

	if (xmlMode) {
		/*
		 * NOTE: the "+ 1" below is for trailing NUL.
		 * XXX:
		 *	the XML protocol should use "s" format
		 *	instead of "b"/"B",
		 *	then extra "+ 1" in here and there was unnecessary,
		 *	but it's too late.
		 */
		size_limit = gfarm_xmlattr_size_limit + 1;
	} else
		size_limit = gfarm_xattr_size_limit;

	*addattr = 0;
	if (!isvalid_attrname(attrname)) {
		gflog_debug(GFARM_MSG_1002066,
			"argument 'attrname' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	} else if (*sizep > size_limit) {
		return (GFARM_ERR_ARGUMENT_LIST_TOO_LONG); /* i.e. E2BIG */
	}

	if (xmlMode) {
		if (!gfarm_utf8_validate_sequences(*valuep, *sizep)) {
			gflog_debug(GFARM_MSG_1003500,
			    "argument '*valuep' is not a valid UTF-8 string");
			return (GFARM_ERR_ILLEGAL_BYTE_SEQUENCE);
		}
	} else {
		e = xattr_check_replica_spec(
		    inode, attrname, valuep, sizep,
		    &have_replica_spec, &change_replica_spec);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1004043,
			    "xattr_check_replica_spec(): %s",
			    gfarm_error_string(e));
			return (e);
		}
		if (have_replica_spec) {
			if (!change_replica_spec) /* not add/modify */
				return (GFARM_ERR_NO_ERROR);
			/* else: need to update xattr */
		} else {
			e = xattr_check_acl(
			    inode, attrname, valuep, sizep, &done);
			if (e != GFARM_ERR_NO_ERROR)
				return (e);
			else if (done)
				return (GFARM_ERR_NO_ERROR);
			/* else: need to update xattr */
		}
	}

	if ((flags & (GFS_XATTR_CREATE|GFS_XATTR_REPLACE))
		== (GFS_XATTR_CREATE|GFS_XATTR_REPLACE)) {
		gflog_debug(GFARM_MSG_1002067,
			"argument 'flags' is invalid");
		return (GFARM_ERR_INVALID_ARGUMENT);
	}
	if (flags & GFS_XATTR_REPLACE) {
		e = inode_xattr_modify(inode, xmlMode, attrname,
		    *valuep, *sizep);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002068,
			    "inode_xattr_modefy(%s): %s",
			    attrname, gfarm_error_string(e));
			return (e);
		}
	} else {
		e = inode_xattr_add(inode, xmlMode, attrname, *valuep, *sizep);
		if (e == GFARM_ERR_NO_ERROR)
			*addattr = 1;
		else if (e == GFARM_ERR_ALREADY_EXISTS &&
		    (flags & GFS_XATTR_CREATE) == 0)
			e = inode_xattr_modify(inode, xmlMode, attrname,
			    *valuep, *sizep);
		if (e != GFARM_ERR_NO_ERROR) {
			gflog_debug(GFARM_MSG_1002069,
				"inode_xattr_add() failed:%s",
				gfarm_error_string(e));
			return (e);
		}
	}
	if (change_replica_spec)
		replica_check_start_xattr_update();

	if (*addattr) {
		e = db_xattr_add(xmlMode, inode_get_number(inode),
		    attrname, *valuep, *sizep, waitctx);
	} else
		e = db_xattr_modify(xmlMode, inode_get_number(inode),
		    attrname, *valuep, *sizep, waitctx);

	return (e);
}

gfarm_error_t
gfm_server_setxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	const char *diag =
	    xmlMode ? "GFM_PROTO_XMLATTR_SET" : "GFM_PROTO_XATTR_SET";
	char *attrname = NULL;
	size_t size;
	void *value = NULL;
	int flags, transaction = 0;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;
	struct db_waitctx ctx, *waitctx;
	int addattr = 0;

	e = gfm_server_get_request(peer, diag,
	    "sBi", &attrname, &size, &value, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002070,
			"gfm_server_get_request() failure:%s",
			gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(attrname);
		free(value);
		return (GFARM_ERR_NO_ERROR);
	}
	if (xmlMode) {
		waitctx = &ctx;
#ifdef ENABLE_XMLATTR
		if (((char *)value)[size-1] != '\0') {
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_1002071,
				"argument 'xmlMode' is invalid");
			goto quit;
		}
#else
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		gflog_debug(GFARM_MSG_1002072,
			"operation is not supported(xmlMode)");
		goto quit;
#endif
	} else
		waitctx = NULL;

	db_waitctx_init(waitctx);
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_1002073,
			"peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002074,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002075,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = xattr_access(xmlMode, inode, process_get_user(process),
				     attrname, GFS_W_OK, XATTR_OP_SET))
		   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003035,
			    "xattr_access() failed: %s",
			    gfarm_error_string(e));
	} else {
		if (db_begin(diag) == GFARM_ERR_NO_ERROR)
			transaction = 1;
		/* value may be changed */
		e = xattr_set(xmlMode, inode, attrname, &value, &size,
		    flags, waitctx, &addattr);
		if (transaction)
			db_end(diag);
	}
	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR) {
		e = dbq_waitret(waitctx);
		if (e == GFARM_ERR_NO_ERROR) {
			giant_lock();
			inode_status_changed(inode);
			giant_unlock();
		} else if (addattr) {
			giant_lock();
			inode_xattr_remove(inode, xmlMode, attrname);
			giant_unlock();
		}
	}
	db_waitctx_fini(waitctx);
quit:
	free(value);
	free(attrname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

gfarm_error_t
gfm_server_getxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	const char *diag =
	    xmlMode ? "GFM_PROTO_XMLATTR_GET" : "GFM_PROTO_XATTR_GET";
	char *attrname = NULL;
	size_t size = 0;
	void *value = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;
	int waitctx_initialized = 0, cached = 0;
	struct db_waitctx waitctx;

	e = gfm_server_get_request(peer, diag, "s", &attrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002079,
			"%s request failed: %s",
			diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(attrname);
		return (GFARM_ERR_NO_ERROR);
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		gflog_debug(GFARM_MSG_1002080,
			"operation is not supported(xmlMode)");
		free(attrname);
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		return (gfm_server_put_reply(peer, diag, e, ""));
	}
#endif
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002081,
			"peer_get_process() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002082,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002083,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = xattr_access(xmlMode, inode, process_get_user(process),
				     attrname, GFS_R_OK, XATTR_OP_GET))
		   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003036,
			"xattr_access() failed: %s",
			gfarm_error_string(e));
	} else if (!gfarm_utf8_validate_string(attrname)) {
		e = GFARM_ERR_ILLEGAL_BYTE_SEQUENCE;
		gflog_debug(GFARM_MSG_1003501,
		    "argument 'attrname' is not a valid UTF-8 string");
	} else if (!isvalid_attrname(attrname)) {
		e = GFARM_ERR_INVALID_ARGUMENT;
		gflog_debug(GFARM_MSG_1002077,
			"argument 'attrname' is invalid");
	} else if ((e = inode_xattr_get_cache(inode, xmlMode, attrname,
	    &value, &size)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002510,
		    "getxattr(%s): %s", attrname, gfarm_error_string(e));
	} else if (value == NULL) { /* not cached */
		db_waitctx_init(&waitctx);
		waitctx_initialized = 1;
		e = db_xattr_get(xmlMode, inode_get_number(inode),
			attrname, &value, &size, &waitctx);
	} else
		cached = 1;
	if (e == GFARM_ERR_NO_ERROR) {
		/* for ACL */
		e = acl_convert_for_getxattr(inode, attrname, &value, &size);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_debug(GFARM_MSG_1003037,
				"acl_convert_for_getxattr() failed: %s",
				gfarm_error_string(e));
	}

	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR && !cached)
		e = dbq_waitret(&waitctx);
	if (waitctx_initialized)
		db_waitctx_fini(&waitctx);

	e = gfm_server_put_reply(peer, diag, e, "b", size, value);
	free(attrname);
	if (value != NULL)
		free(value);
	return (e);
}

gfarm_error_t
gfm_server_listxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	const char *diag =
	    xmlMode ? "GFM_PROTO_XMLATTR_LIST" : "GFM_PROTO_XATTR_LIST";
	size_t size = 0;
	char *value = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;

	if (skip)
		return (GFARM_ERR_NO_ERROR);
#ifndef ENABLE_XMLATTR
	if (xmlMode)
		return gfm_server_put_reply(peer, diag,
				GFARM_ERR_OPERATION_NOT_SUPPORTED, "");
#endif

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002085,
			"peer_get_process() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002086,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002087,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = inode_access(inode, process_get_user(process),
			GFS_R_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002088,
			"inode_access() failed: %s",
			gfarm_error_string(e));
	} else {
		// NOTE: inode_xattrname_list() doesn't access to DB.
		e = inode_xattr_list(inode, xmlMode, &value, &size);
	}
	giant_unlock();

	e = gfm_server_put_reply(peer, diag, e, "b", size, value);
	free(value);
	return e;
}

static gfarm_error_t
removexattr(int xmlMode, struct inode *inode, char *attrname)
{
	gfarm_error_t e;

	if (!gfarm_utf8_validate_string(attrname)) {
		e = GFARM_ERR_ILLEGAL_BYTE_SEQUENCE;
		gflog_debug(GFARM_MSG_1003502,
		    "argument 'attrname' is not a valid UTF-8 string");
	} else if (!isvalid_attrname(attrname)) {
		gflog_debug(GFARM_MSG_1002089,
		    "argument 'attrname' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	} else {
		e = inode_xattr_remove(inode, xmlMode, attrname);
		if (e == GFARM_ERR_NO_ERROR) {
			db_xattr_remove(xmlMode,
				inode_get_number(inode), attrname);
			inode_status_changed(inode);
		}
	}

	return e;
}

gfarm_error_t
gfm_server_removexattr(struct peer *peer, int from_client, int skip,
		int xmlMode)
{
	gfarm_error_t e;
	const char *diag =
	     xmlMode ? "GFM_PROTO_XMLATTR_REMOVE" : "GFM_PROTO_XATTR_REMOVE";
	char *attrname = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;

	e = gfm_server_get_request(peer, diag, "s", &attrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002090,
			"%s request failure",
			diag);
		return (e);
	}
	if (skip) {
		free(attrname);
		return (GFARM_ERR_NO_ERROR);
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		gflog_debug(GFARM_MSG_1002091,
			"operation is not supported(xmlMode)");
		free(attrname);
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		return (gfm_server_put_reply(peer, diag, e, ""));
	}
#endif
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002092,
			"peer_get_process() failed :%s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002093,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002094,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = xattr_access(xmlMode, inode, process_get_user(process),
				     attrname, GFS_W_OK, XATTR_OP_REMOVE))
		   != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003038,
			"xattr_access() failed: %s",
			gfarm_error_string(e));
	} else
		e = removexattr(xmlMode, inode, attrname);
	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR && !xmlMode &&
	    (strcmp(attrname, GFARM_EA_NCOPY) == 0 ||
	     strcmp(attrname, GFARM_EA_REPATTR) == 0))
		replica_check_start_xattr_update();

	free(attrname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}


#ifdef ENABLE_XMLATTR
/*
 * These parameters must be smaller than DBQ_SIZE
 * in db_access.c to avoid dbq_enter() waiting.
 */
#define DEFAULT_INUM_PATH_ARRAY_SIZE 100
#define MINIMUM_DBQ_FREE_NUM 100

/*
 * We use -1 as unset errno because gfarm_errno_t>=0,
 * to distinguish whether db access is completed.
 */
#define UNSET_GFARM_ERRNO	(-1)
#define IS_UNSET_ERRNO(errno)	((errno) < 0)

struct inum_path_array;
struct inum_path_entry {
	struct inum_path_array *array;
	gfarm_ino_t inum;
	char *path;
	gfarm_error_t dberr;
	int nattrs;
	char **attrnames;
};

struct inum_path_array {
	// for DB waiting
	pthread_mutex_t lock;
	pthread_cond_t cond;

	char *expr; // target XPath

	// for cookie_path
	int check_ckpath;
	int check_ckname;
	int ckpathdepth;
	char *restartpath;  // "dir1/dir2" etc
	char *ckpath;       // "dir1\0dir2" etc
	char **ckpathnames;

	// for found path and attrnames
	int nentry;
	int nattrssum;
	int isfilled;
	struct inum_path_entry entries[DEFAULT_INUM_PATH_ARRAY_SIZE];

	// for reply
	int replyentidx;
	int replynameidx;
	int nreplied;
};

static const char inum_path_array_diag[] = "inum_path_array";

static void
inum_path_entry_init(struct inum_path_array *array,
	struct inum_path_entry *entry)
{
	entry->array = array;
	entry->dberr = UNSET_GFARM_ERRNO;
}

static void
inum_path_entry_fini(struct inum_path_entry *entry)
{
	int i;

	free(entry->path);
	for (i = 0; i < entry->nattrs; i++)
		free(entry->attrnames[i]);
	free(entry->attrnames);
}

static void
inum_path_array_init(struct inum_path_array *array, char *expr)
{
	int i;
	static const char diag[] = "inum_path_array_init";

	memset(array, 0, sizeof(*array));
	gfarm_mutex_init(&array->lock, diag, inum_path_array_diag);
	gfarm_cond_init(&array->cond, diag, inum_path_array_diag);

	for (i = 0; i < GFARM_ARRAY_LENGTH(array->entries); i++)
		inum_path_entry_init(array, &array->entries[i]);
	array->expr = expr;
}

static struct inum_path_array *
inum_path_array_alloc(char *expr)
{
	struct inum_path_array *array = GFARM_MALLOC(array);
	if (array != NULL)
		inum_path_array_init(array, expr);
	else
		gflog_debug(GFARM_MSG_1002096,
			"allocation of 'inum_path_array' failed");

	return array;
}

static void
inum_path_array_fini(struct inum_path_array *array)
{
	int i;
	static const char diag[] = "inum_path_array_fini";

	for (i = 0; i < array->nentry; i++) {
		inum_path_entry_fini(&array->entries[i]);
	}
	free(array->restartpath);
	free(array->ckpath);
	free(array->ckpathnames);
	gfarm_cond_destroy(&array->cond, diag, inum_path_array_diag);
	gfarm_mutex_destroy(&array->lock, diag, inum_path_array_diag);
}

static void
inum_path_array_free(struct inum_path_array *array)
{
	if (array != NULL) {
		inum_path_array_fini(array);
		free(array);
	}
}

static void
inum_path_array_reinit(struct inum_path_array *array, char *expr)
{
	if (array != NULL) {
		inum_path_array_fini(array);
		inum_path_array_init(array, expr);
	}
}

static struct inum_path_entry *
inum_path_array_addpath(struct inum_path_array *array, gfarm_ino_t inum,
	char *path)
{
	int n = GFARM_ARRAY_LENGTH(array->entries);
	if (array->nentry < n) {
		array->entries[array->nentry].inum = inum;
		array->entries[array->nentry].path = path;
		array->nentry++;
		if (array->nentry == n)
			array->isfilled = 1;
		return &array->entries[array->nentry - 1];
	} else
		return NULL;
}

void
db_findxmlattr_done(gfarm_error_t e, void *en)
{
	struct inum_path_entry *entry = (struct inum_path_entry *)en;
	struct inum_path_array *array = entry->array;
	static const char diag[] = "db_findxmlattr_done";

	gfarm_mutex_lock(&array->lock, diag, inum_path_array_diag);
	entry->dberr = e;
	gfarm_cond_signal(&array->cond, diag, inum_path_array_diag);
	gfarm_mutex_unlock(&array->lock, diag, inum_path_array_diag);
}

gfarm_error_t
inum_path_array_add_attrnames(void *en, int nfound, void *in)
{
	struct inum_path_entry *entry = (struct inum_path_entry *)en;
	struct inum_path_array *array = entry->array;
	struct xattr_info *vinfo = (struct xattr_info *)in;
	int i;
	static const char diag[] = "inum_path_array_add_attrnames";

	if (nfound <= 0)
		return GFARM_ERR_NO_ERROR;

	GFARM_MALLOC_ARRAY(entry->attrnames, nfound);
	if (entry->attrnames == NULL) {
		gflog_debug(GFARM_MSG_1002097,
			"allocation of 'attrnames' failed");
		return GFARM_ERR_NO_MEMORY;
	}

	gfarm_mutex_lock(&array->lock, diag, inum_path_array_diag);
	entry->nattrs = nfound;
	for (i = 0; i < nfound; i++) {
		entry->attrnames[i] = vinfo[i].attrname;
		vinfo[i].attrname = NULL; // to avoid free by caller
		vinfo[i].namelen = 0;
	}
	array->nattrssum += nfound;
	gfarm_mutex_unlock(&array->lock, diag, inum_path_array_diag);

	return GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
findxmlattr_dbq_enter(struct inum_path_array *array,
	struct inum_path_entry *entry)
{
	gfarm_error_t e;
	int dbbusy = 0;

	if (db_getfreenum() > MINIMUM_DBQ_FREE_NUM) {
		e = db_xmlattr_find(entry->inum, array->expr,
			inum_path_array_add_attrnames, entry,
			db_findxmlattr_done, entry);
	} else {
		array->isfilled = 1;
		dbbusy = 1;
		e = (array->nentry > 1) ? GFARM_ERR_NO_ERROR
			: GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE;
	}
	if ((e != GFARM_ERR_NO_ERROR) || dbbusy) {
		gflog_debug(GFARM_MSG_1002098,
			"error occurred during process:%s",
			gfarm_error_string(e));
		free(entry->path);
		entry->path = NULL;
		array->nentry--;
	}
	return e;
}

static gfarm_error_t
inum_path_array_add(struct inum_path_array *array, gfarm_ino_t inum,
	char *path)
{
	struct inum_path_entry *entry;

	entry = inum_path_array_addpath(array, inum, path);
	if (entry != NULL) {
		gflog_debug(GFARM_MSG_1002099,
			"inum_path_array_addpath() failed");
		return findxmlattr_dbq_enter(array, entry);
	}
	else {
		free(path);
		// path array is enough isfilled.
		return GFARM_ERR_NO_SPACE;
	}
}

static gfarm_error_t
findxmlattr_set_restart_path(struct inum_path_array *array,
	char *path)
{
	char *p, *q;
	int i;
	static const char diag[] = "findxmlattr_set_restart_path";

	/*
	 * if restartpath = "dir1/dir2",
	 *   array->ckpath = "dir1\0dir2";
	 *   array->ckpathdepth = 2;
	 *   array->ckpathnames = { "dir1", "dir2" }
	 */
	array->restartpath = path;
	free(array->ckpath);
	array->ckpath = strdup_log(path, diag);
	if (array->ckpath == NULL)
		return (GFARM_ERR_NO_MEMORY);

	array->check_ckpath = array->check_ckname = 1;
	p = array->ckpath;
	array->ckpathdepth = 1;
	while ((q = strchr(p, '/')) != NULL) {
		array->ckpathdepth++;
		do {
			q++;
		} while (*q == '/');
		p = q;
	}
	GFARM_MALLOC_ARRAY(array->ckpathnames, array->ckpathdepth);
	if (array->ckpathnames == NULL) {
		free(array->ckpath);
		array->ckpath = NULL;
		gflog_debug(GFARM_MSG_1002101,
			"allocation of 'ckpathnames' failed");
		return GFARM_ERR_NO_MEMORY;
	}
	p = array->ckpath;
	for (i = 0; i < array->ckpathdepth; i++) {
		array->ckpathnames[i] = p;
		q = strchr(p, '/');
		if (q == NULL)
			break;
		do {
			*q = '\0';
			q++;
		} while (*q == '/');
		p = q;
	}

	return GFARM_ERR_NO_ERROR;
}

static int
is_find_target(struct inode *inode, struct user *user)
{
	return inode_xattr_has_xmlattrs(inode)
		&& (inode_access(inode, user, GFS_R_OK)
			== GFARM_ERR_NO_ERROR);
}

static gfarm_error_t
findxmlattr_add_selfpath(struct inode *inode, struct user *user,
	char *path, struct inum_path_array *array)
{
	if (!is_find_target(inode, user)){
		return GFARM_ERR_NO_ERROR;
	}
	if (array->check_ckpath) {
		if (strcmp(array->restartpath, path) != 0) {
			return GFARM_ERR_NO_ERROR;
		}
	}
	return inum_path_array_add(array, inode_get_number(inode), path);
}

static char *
make_subpath(char *parent_path, char *name, int namelen)
{
	int pathlen;
	size_t allocsz;
	int overflow = 0;
	char *subpath;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	subpath = NULL;
#endif

	pathlen = (parent_path[0] == '\0') ? 0 : (strlen(parent_path) + 1);
	allocsz = gfarm_size_add(&overflow, pathlen, namelen);
	allocsz = gfarm_size_add(&overflow, allocsz, 1);
	if (!overflow)
		GFARM_MALLOC_ARRAY(subpath, allocsz);
	if (overflow || (subpath == NULL)) {
		gflog_debug(GFARM_MSG_1002102,
			"allocation of 'subpath' failed or overflow");
		return NULL;
	}
	if (pathlen > 0)
		sprintf(subpath, "%s/", parent_path);
	memcpy(subpath + pathlen, name, namelen);
	subpath[pathlen + namelen] = '\0';
	return subpath;
}

static gfarm_error_t
findxmlattr_add_subpaths(struct inode *inode, struct user *user,
	char *path, int curdepth, const int maxdepth,
	struct inum_path_array *array)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	struct inode *entry_inode;
	char *name, *subpath = NULL;
	int namelen, is_dir, is_tgt, skip = 0;

	if (curdepth >= maxdepth)
		return GFARM_ERR_NO_ERROR;
	dir = inode_get_dir(inode);
	if (dir == NULL)
		return GFARM_ERR_NO_ERROR;
	if (inode_access(inode, user, GFS_X_OK) != GFARM_ERR_NO_ERROR)
		return GFARM_ERR_NO_ERROR;

	if ((array->check_ckpath) && (curdepth < array->ckpathdepth)) {
		char *subpath = array->ckpathnames[curdepth];
		if (dir_cursor_lookup(dir, subpath, strlen(subpath), &cursor)
			== 0)
			return GFARM_ERR_NO_ERROR;
		if (curdepth == (array->ckpathdepth -1))
			array->check_ckpath = 0; // it's restart path now
		else
			skip = 1; // doesn't find restart path yet
	} else {
		if (dir_cursor_set_pos(dir, 0, &cursor) == 0)
			return GFARM_ERR_NO_ERROR;
	}

	do {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			break;
		name = dir_entry_get_name(entry, &namelen);
		if (name_is_dot_or_dotdot(name, namelen))
			continue;
		entry_inode = dir_entry_get_inode(entry);
		is_dir = inode_is_dir(entry_inode);
		is_tgt = (!skip && is_find_target(entry_inode, user));
		skip = 0;
		if (!is_dir && !is_tgt)
			continue;

		subpath = make_subpath(path, name, namelen);
		if (subpath == NULL) {
			gflog_debug(GFARM_MSG_1002103,
				"make_subpath() failed");
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		if (is_tgt)
			e = inum_path_array_add(array,
					inode_get_number(entry_inode), subpath);
		if (is_dir && (e == GFARM_ERR_NO_ERROR)) {
			e = findxmlattr_add_subpaths(entry_inode, user,
				subpath, curdepth + 1, maxdepth, array);
		}
		if (!is_tgt)
			free(subpath);
	} while ((e == GFARM_ERR_NO_ERROR) &&
		(dir_cursor_next(dir, &cursor) != 0) && !array->isfilled);

	return e;
}

static gfarm_error_t
findxmlattr_make_patharray(struct inode *inode, struct user *user,
	const int maxdepth, struct inum_path_array *array)
{
	gfarm_error_t e;
	static const char diag[] = "findxmlattr_make_patharray";
	char *toppath = strdup_log("", diag);

	if (toppath == NULL)
		return (GFARM_ERR_NO_MEMORY);

	e = findxmlattr_add_selfpath(inode, user, toppath, array);

	if ((e == GFARM_ERR_NO_ERROR) && inode_is_dir(inode)) {
		e = findxmlattr_add_subpaths(
			inode, user, toppath, 0, maxdepth, array);
	}
	if (array->entries[0].path != toppath)
		free(toppath);
	return e;
}

static void
db_findxmlattr_wait(struct inum_path_array *array, int idx)
{
	struct inum_path_entry *entry;
	static const char diag[] = "db_findxmlattr_wait";

	/*
	 * NOTE: must call with array->lock
	 */
	entry = &array->entries[idx];
	while (IS_UNSET_ERRNO(entry->dberr)) {
		gfarm_cond_wait(&array->cond, &array->lock,
		    diag, inum_path_array_diag);
	}
}

static void
findxmlattr_reset_replyidx(struct inum_path_array *array,
	struct gfs_xmlattr_ctx *ctxp)
{
	struct inum_path_entry *entry;
	int j;

	entry = &array->entries[array->replyentidx];
	if (entry->path != NULL &&
		(strcmp(entry->path, ctxp->cookie_path) == 0)) {
		for (j = 0; j < entry->nattrs; j++) {
			array->nreplied++; // already replied, skip it
			if (strcmp(entry->attrnames[j],
				ctxp->cookie_attrname) == 0) {
				break;
			}
		}
		array->replynameidx = j + 1;
	}
}

static void
findxmlattr_get_nextname(struct inum_path_array *array,
	struct gfs_xmlattr_ctx *ctxp, char **fpathp, char **attrnamep)
{
	struct inum_path_entry *entry;
	int i, j, jinit;

	*fpathp = NULL;
	*attrnamep = NULL;
	if (array->nreplied >= array->nattrssum)
		return;

	j = jinit = array->replynameidx;
	for (i = array->replyentidx; i < array->nentry; i++) {
		entry = &array->entries[i];
		for (j = jinit; j < entry->nattrs; j++) {
			*fpathp = entry->path;
			*attrnamep = entry->attrnames[j];
			array->nreplied++;
			goto quit;
		}
		jinit = 0;
	}
quit:
	array->replyentidx = i;
	array->replynameidx = j + 1;
}

static void
findxmlattr_dbq_wait_all(struct inum_path_array *array,
	struct gfs_xmlattr_ctx *ctxp)
{
	int i;
	static const char diag[] = "findxmlattr_dbq_wait_all";

	if (array == NULL)
		return;

	gfarm_mutex_lock(&array->lock, diag, inum_path_array_diag);
	for (i = 0; i < array->nentry; i++) {
		db_findxmlattr_wait(array, i);
	}
	gfarm_mutex_unlock(&array->lock, diag, inum_path_array_diag);
}

static void
findxmlattr_set_found_attrnames(struct inum_path_array *array,
	struct gfs_xmlattr_ctx *ctxp)
{
	int i, nreply, nremain;
	char *path, *name;

	if (array->check_ckname) {
		findxmlattr_reset_replyidx(array, ctxp);
		array->check_ckname = 0;
	}

	nremain = array->nattrssum - array->nreplied;
	nreply = (nremain >= ctxp->nalloc)
		? ctxp->nalloc : nremain;
	for (i = 0; i < nreply; i++) {
		findxmlattr_get_nextname(array, ctxp, &path, &name);
		ctxp->entries[i].path = path;
		ctxp->entries[i].attrname = name;
	}

	ctxp->nvalid = nreply;
	ctxp->eof = ((array->isfilled == 0)
			&& (array->nattrssum == array->nreplied));
}

static gfarm_error_t
findxmlxattr_restart(struct peer *peer, struct inode *inode,
	struct inum_path_array *array, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	struct process *process = peer_get_process(peer);
	struct user *user = process_get_user(process);
	char *restartpath;
	static const char diag[] = "findxmlxattr_restart";

	if (array->nentry == 0)
		return GFARM_ERR_NO_ERROR;

	restartpath = strdup_log(array->entries[array->nentry-1].path, diag);
	if (restartpath == NULL)
		return (GFARM_ERR_NO_MEMORY);
	inum_path_array_reinit(array, ctxp->expr);
	e = findxmlattr_set_restart_path(array, restartpath);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002106,
			"findxmlattr_set_restart_path() failed: %s",
			gfarm_error_string(e));
		return e;
	}

	giant_lock();
	e = findxmlattr_make_patharray(inode, user,
		ctxp->depth, array);
	giant_unlock();

	findxmlattr_dbq_wait_all(array, ctxp);
	if (e == GFARM_ERR_NO_ERROR) {
		findxmlattr_set_found_attrnames(array, ctxp);
	}

	return e;
}

static int
ctx_has_cookie(struct gfs_xmlattr_ctx *ctxp)
{
	/*
	 * cookie_pathname maybe "" if restart from top directory.
	 * cookie_attrname is always not empty if set.
	 */
	return (ctxp->cookie_attrname[0] != '\0');
}

static gfarm_error_t
findxmlattr(struct peer *peer, struct inode *inode,
	struct gfs_xmlattr_ctx *ctxp, struct inum_path_array **ap)
{
	gfarm_error_t e;
	struct process *process = peer_get_process(peer);
	struct user *user = process_get_user(process);
	struct inum_path_array *array = NULL;

	array = peer_findxmlattrctx_get(peer);
	if (array == NULL) {
		if (ctx_has_cookie(ctxp)) {
			gflog_debug(GFARM_MSG_1002107,
				"argument 'ctxp' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((array = inum_path_array_alloc(ctxp->expr))
			== NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_1002108,
				"inum_path_array_alloc() failed");
		} else {
			e = findxmlattr_make_patharray(inode, user,
				ctxp->depth, array);
		}
		giant_unlock();
		// We must wait always even if above function was failed.
		findxmlattr_dbq_wait_all(array, ctxp);
	} else {
		giant_unlock();
		if (!ctx_has_cookie(ctxp)) {
			gflog_debug(GFARM_MSG_1002109,
				"argument 'ctxp' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else
			e = GFARM_ERR_NO_ERROR;
	}

	*ap = array;
	if (e == GFARM_ERR_NO_ERROR)
		findxmlattr_set_found_attrnames(array, ctxp);

	while ((e == GFARM_ERR_NO_ERROR) &&
			array->isfilled && (ctxp->nvalid == 0)) {
		e = findxmlxattr_restart(peer, inode, array, ctxp);
	}
	if ((e == GFARM_ERR_NO_ERROR) && (ctxp->eof == 0)) {
		// remain array pointer at peer
		peer_findxmlattrctx_set(peer, array);
		*ap = NULL; // avoid to be freed
	} else {
		// purge array pointer from peer
		peer_findxmlattrctx_set(peer, NULL);
	}

	return e;
}
#endif /* ENABLE_XMLATTR */

gfarm_error_t
gfm_server_findxmlattr(struct peer *peer, int from_client, int skip)
{
	gfarm_error_t e;
	static const char diag[] = "GFM_PROTO_XMLATTR_FIND";
	char *expr = NULL, *ck_path = NULL, *ck_name = NULL;
	int depth, nalloc;
#ifdef ENABLE_XMLATTR
	struct gfs_xmlattr_ctx *ctxp = NULL;
	int fd, i;
	struct process *process;
	struct inode *inode;
	struct inum_path_array *array = NULL;
	int eof = 0, nvalid = 0;
#endif

	e = gfm_server_get_request(peer, diag,
			"siiss", &expr, &depth, &nalloc, &ck_path, &ck_name);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002110,
			"%s request failed: %s",
			diag, gfarm_error_string(e));
		return (e);
	}
	if (skip) {
		free(expr);
		free(ck_path);
		free(ck_name);
		return (GFARM_ERR_NO_ERROR);
	}

#ifdef ENABLE_XMLATTR
	if ((ctxp = gfs_xmlattr_ctx_alloc(nalloc)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1002111,
			"allocation of 'ctxp' failed");
		goto quit;
	}
	ctxp->expr = expr;
	ctxp->depth =
	    depth < gfarm_max_directory_depth ?
	    depth : gfarm_max_directory_depth;
	ctxp->cookie_path = ck_path;
	ctxp->cookie_attrname = ck_name;

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_1002112,
			"peer_get_process() failed :%s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002113,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, peer, fd, &inode, diag)
	    ) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1002114,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	}
	if (e == GFARM_ERR_NO_ERROR) {
		// giant_unlock() is called in findxmlattr()
		e = findxmlattr(peer, inode, ctxp, &array);
		eof = ctxp->eof;
		nvalid = ctxp->nvalid;
	} else
		giant_unlock();

quit:
	if ((e = gfm_server_put_reply(peer, diag, e, "ii", eof,
		nvalid)) == GFARM_ERR_NO_ERROR) {
		/*
		 * "ctxp" is not NULL here.  If the memory allocation error
		 * for "ctxp" has occurred, the valiable "e" is set to
		 * GFARM_ERR_NO_MEMORY and gfm_server_put_reply() here
		 * returns GFARM_ERR_NO_MEMORY (or a communication error
		 * code).
		 */
		for (i = 0; i < nvalid; i++) {
			e = gfp_xdr_send(peer_get_conn(peer), "ss",
				    ctxp->entries[i].path,
				    ctxp->entries[i].attrname);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning(GFARM_MSG_1000420,
				    "%s@%s: findxmlattr: %s",
					peer_get_username(peer),
					peer_get_hostname(peer),
					gfarm_error_string(e));
				break;
			}
		}
	}
	inum_path_array_free(array);
	gfs_xmlattr_ctx_free(ctxp, 0);
	return e;
#else
	free(expr);
	free(ck_path);
	free(ck_name);
	return gfm_server_put_reply(peer, diag,
			GFARM_ERR_OPERATION_NOT_SUPPORTED, "");
#endif
}

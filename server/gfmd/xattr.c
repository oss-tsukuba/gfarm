/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h> /* fd_set for "filetab.h" */
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#else
#define XATTR_CREATE    0x1     /* set value, fail if attr already exists */
#define XATTR_REPLACE   0x2     /* set value, fail if attr does not exist */
#endif

#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "hash.h"

#include "config.h"	/* gfarm_metadb_admin_user */
#include "auth.h"
#include "gfp_xdr.h"
#include "xattr_info.h"

#include "subr.h"
#include "db_access.h"
#include "peer.h"
#include "inode.h"
#include "process.h"
#include "metadb_common.h"
#include "dir.h"

#define MAX_XATTR_NAME_LEN	256

static int
isvalid_attrname(const char *attrname)
{
	int namelen = strlen(attrname);

	return ((0 < namelen) && (namelen <= MAX_XATTR_NAME_LEN));
}

static gfarm_error_t
setxattr(int xmlMode, struct inode *inode, char *attrname,
	void *value, size_t size, int flags, struct db_waitctx *waitctx,
	int *addattr)
{
	gfarm_error_t e;

	*addattr = 0;
	if (!isvalid_attrname(attrname)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"argument 'attrname' is invalid");
		return GFARM_ERR_INVALID_ARGUMENT;
	}
	if ((flags & (XATTR_CREATE|XATTR_REPLACE))
		== (XATTR_CREATE|XATTR_REPLACE)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"argument 'flags' is invalid");
		return GFARM_ERR_INVALID_ARGUMENT;
	}
	if (flags & XATTR_REPLACE) {
		if (!inode_xattr_isexists(inode, xmlMode, attrname)) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"xattr does not exist");
			return GFARM_ERR_NO_SUCH_OBJECT;
		}
	} else {
		e = inode_xattr_add(inode, xmlMode, attrname);
		if (e == GFARM_ERR_NO_ERROR)
			*addattr = 1;
		else if (e != GFARM_ERR_ALREADY_EXISTS
			|| (flags & XATTR_CREATE)) {
			gflog_debug(GFARM_MSG_UNFIXED,
				"inode_xattr_add() failed:%s",
				gfarm_error_string(e));
			return e;
		}
	}

	if (*addattr) {
		e = db_xattr_add(xmlMode, inode_get_number(inode),
			attrname, value, size, waitctx);
	} else
		e = db_xattr_modify(xmlMode, inode_get_number(inode),
			attrname, value, size, waitctx);

	return e;
}

gfarm_error_t
gfm_server_setxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	char *diag = xmlMode ? "xmlattr_set" : "xattr_set";
	char *attrname = NULL;
	size_t size;
	char *value = NULL;
	int flags;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;
	struct db_waitctx ctx, *waitctx;
	int addattr;

	e = gfm_server_get_request(peer, diag,
	    "sBi", &attrname, &size, &value, &flags);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"gfm_server_get_request() failure:%s",
			gfarm_error_string(e));
		goto quit;
	}
	if (skip) {
		goto quit;
	}
	if (xmlMode) {
		waitctx = &ctx;
#ifdef ENABLE_XMLATTR
		if (value[size-1] != '\0') {
			e = GFARM_ERR_INVALID_ARGUMENT;
			gflog_debug(GFARM_MSG_UNFIXED,
				"argument 'xmlMode' is invalid");
			goto quit;
		}
#else
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not supported(xmlMode)");
		goto quit;
#endif
	} else
		waitctx = NULL;

	db_waitctx_init(waitctx);
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_get_process() failed");
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = inode_access(inode, process_get_user(process),
			GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode_access() failed: %s",
			gfarm_error_string(e));
	} else
		e = setxattr(xmlMode, inode, attrname, value, size,
				flags, waitctx, &addattr);
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

static gfarm_error_t
getxattr(int xmlMode, struct inode *inode, char *attrname,
	void **value, size_t *size, struct db_waitctx *waitctx)
{
	if (!isvalid_attrname(attrname)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"argument 'attrname' is invalid");
		return GFARM_ERR_INVALID_ARGUMENT;
	}
	if (!inode_xattr_isexists(inode, xmlMode, attrname)) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"xattr does not exist");
		return GFARM_ERR_NO_SUCH_OBJECT;
	}
	return db_xattr_get(xmlMode, inode_get_number(inode),
			attrname, value, size, waitctx);
}

gfarm_error_t
gfm_server_getxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	char *diag = xmlMode ? "xmlattr_get" : "xattr_get";
	char *attrname = NULL;
	size_t size = 0;
	void *value = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;
	struct db_waitctx waitctx;

	e = gfm_server_get_request(peer, diag, "s", &attrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"%s request failed: %s",
			diag, gfarm_error_string(e));
		goto quit;
	}
	if (skip) {
		goto quit;
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not supported(xmlMode)");
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
	}
#endif

	db_waitctx_init(&waitctx);
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_get_process() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = inode_access(inode, process_get_user(process),
			GFS_R_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode_access() failed: %s",
			gfarm_error_string(e));
	} else
		e = getxattr(xmlMode, inode, attrname, &value, &size, &waitctx);
	giant_unlock();
	if (e == GFARM_ERR_NO_ERROR)
		e = dbq_waitret(&waitctx);
	db_waitctx_fini(&waitctx);
quit:
	e = gfm_server_put_reply(peer, diag, e, "b", size, value);
	free(attrname);
	free(value);
	return e;
}

gfarm_error_t
gfm_server_listxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	char *diag = xmlMode ? "xmlattr_list" : "xattr_list";
	size_t size;
	char *value = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;

	if (skip) {
		return GFARM_ERR_NO_ERROR;
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode)
		return gfm_server_put_reply(peer, diag,
				GFARM_ERR_OPERATION_NOT_SUPPORTED, "");
#endif

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_get_process() failed: %s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = inode_access(inode, process_get_user(process),
			GFS_R_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
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

	if (isvalid_attrname(attrname)) {
		e = inode_xattr_remove(inode, xmlMode, attrname);
		if (e == GFARM_ERR_NO_ERROR) {
			db_xattr_remove(xmlMode,
				inode_get_number(inode), attrname);
			inode_status_changed(inode);
		}
	} else {
		gflog_debug(GFARM_MSG_UNFIXED,
			"argument 'attrname' is invalid");
		e = GFARM_ERR_INVALID_ARGUMENT;
	}

	return e;
}

gfarm_error_t
gfm_server_removexattr(struct peer *peer, int from_client, int skip,
		int xmlMode)
{
	gfarm_error_t e;
	char *diag = xmlMode ? "xmlattr_remove" : "xattr_remove";
	char *attrname = NULL;
	struct process *process;
	gfarm_int32_t fd;
	struct inode *inode;

	e = gfm_server_get_request(peer, diag, "s", &attrname);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"%s request failure",
			diag);
		goto quit;
	}
	if (skip) {
		goto quit;
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"operation is not supported(xmlMode)");
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
	}
#endif

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_get_process() failed :%s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	} else if ((e = inode_access(inode, process_get_user(process),
			GFS_W_OK)) != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"inode_access() failed: %s",
			gfarm_error_string(e));
	} else
		e = removexattr(xmlMode, inode, attrname);
	giant_unlock();
quit:
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
	int i, err;
	char *msg = "inum_path_array_init";

	memset(array, 0, sizeof(*array));
	if ((err = pthread_mutex_init(&array->lock, NULL)) != 0)
		gflog_fatal(GFARM_MSG_1000417,
		    "%s: mutex: %s", msg, strerror(err));
	if ((err = pthread_cond_init(&array->cond, NULL)) != 0)
		gflog_fatal(GFARM_MSG_1000418,
		    "%s: cond: %s", msg, strerror(err));

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
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'inum_path_array' failed");

	return array;
}

static void
inum_path_array_fini(struct inum_path_array *array)
{
	int i;

	for (i = 0; i < array->nentry; i++) {
		inum_path_entry_fini(&array->entries[i]);
	}
	free(array->restartpath);
	free(array->ckpath);
	free(array->ckpathnames);
	pthread_cond_destroy(&array->cond);
	pthread_mutex_destroy(&array->lock);
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

	pthread_mutex_lock(&array->lock);
	entry->dberr = e;
	pthread_cond_signal(&array->cond);
	pthread_mutex_unlock(&array->lock);
}

gfarm_error_t
inum_path_array_add_attrnames(void *en, int nfound, void *in)
{
	struct inum_path_entry *entry = (struct inum_path_entry *)en;
	struct inum_path_array *array = entry->array;
	struct xattr_info *vinfo = (struct xattr_info *)in;
	int i;

	if (nfound <= 0)
		return GFARM_ERR_NO_ERROR;

	GFARM_MALLOC_ARRAY(entry->attrnames, nfound);
	if (entry->attrnames == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'attrnames' failed");
		return GFARM_ERR_NO_MEMORY;
	}

	pthread_mutex_lock(&array->lock);
	entry->nattrs = nfound;
	for (i = 0; i < nfound; i++) {
		entry->attrnames[i] = vinfo[i].attrname;
		vinfo[i].attrname = NULL; // to avoid free by caller
		vinfo[i].namelen = 0;
	}
	array->nattrssum += nfound;
	pthread_mutex_unlock(&array->lock);

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
		gflog_debug(GFARM_MSG_UNFIXED,
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
		gflog_debug(GFARM_MSG_UNFIXED,
			"inum_path_array_addpath() failed");
		return findxmlattr_dbq_enter(array, entry);
	}
	else {
		free(path);
		// path array is enough isfilled.
		return GFARM_ERR_NO_SPACE;
	}
}

static int
is_dot_dir(char *name, int namelen)
{
	if (name[0] == '.') {
		if (namelen == 1)
			return 1;
		if ((name[1] == '.') && (namelen == 2))
			return 1;
	}
	return 0;
}

static gfarm_error_t
findxmlattr_set_restart_path(struct inum_path_array *array,
	char *path)
{
	char *p, *q;
	int i;

	/*
	 * if restartpath = "dir1/dir2",
	 *   array->ckpath = "dir1\0dir2";
	 *   array->ckpathdepth = 2;
	 *   array->ckpathnames = { "dir1", "dir2" }
	 */
	array->restartpath = path;
	free(array->ckpath);
	array->ckpath = strdup(path);
	if (array->ckpath == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'ckpath' failed");
		return GFARM_ERR_NO_MEMORY;
	}

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
		gflog_debug(GFARM_MSG_UNFIXED,
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

	pathlen = (parent_path[0] == '\0') ? 0 : (strlen(parent_path) + 1);
	allocsz = gfarm_size_add(&overflow, pathlen, namelen);
	allocsz = gfarm_size_add(&overflow, allocsz, 1);
	if (!overflow)
		GFARM_MALLOC_ARRAY(subpath, allocsz);
	if (overflow || (subpath == NULL)) {
		gflog_debug(GFARM_MSG_UNFIXED,
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
		if (is_dot_dir(name, namelen))
			continue;
		entry_inode = dir_entry_get_inode(entry);
		is_dir = inode_is_dir(entry_inode);
		is_tgt = (!skip && is_find_target(entry_inode, user));
		skip = 0;
		if (!is_dir && !is_tgt)
			continue;

		subpath = make_subpath(path, name, namelen);
		if (subpath == NULL) {
			gflog_debug(GFARM_MSG_UNFIXED,
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
	char *toppath = strdup("");

	if (toppath == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'toppath' failed");
		return GFARM_ERR_NO_MEMORY;
	}

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
	int err;
	struct inum_path_entry *entry;

	/*
	 * NOTE: must call with array->lock
	 */
	entry = &array->entries[idx];
	while (IS_UNSET_ERRNO(entry->dberr)) {
		err = pthread_cond_wait(&array->cond, &array->lock);
		if (err != 0)
			gflog_fatal(GFARM_MSG_1000419, "db_findxmlattr_wait: "
				"condwait finished: %s", strerror(err));
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

	if (array == NULL)
		return;

	pthread_mutex_lock(&array->lock);
	for (i = 0; i < array->nentry; i++) {
		db_findxmlattr_wait(array, i);
	}
	pthread_mutex_unlock(&array->lock);
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

	if (array->nentry == 0)
		return GFARM_ERR_NO_ERROR;

	restartpath = strdup(array->entries[array->nentry-1].path);
	if (restartpath == NULL) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'restartpath' failed");
		return GFARM_ERR_NO_MEMORY;
	}
	inum_path_array_reinit(array, ctxp->expr);
	e = findxmlattr_set_restart_path(array, restartpath);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
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
			gflog_debug(GFARM_MSG_UNFIXED,
				"argument 'ctxp' is invalid");
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((array = inum_path_array_alloc(ctxp->expr))
			== NULL) {
			e = GFARM_ERR_NO_MEMORY;
			gflog_debug(GFARM_MSG_UNFIXED,
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
			gflog_debug(GFARM_MSG_UNFIXED,
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
	char *diag = "find_xml_attr";
	char *expr = NULL, *ck_path = NULL, *ck_name = NULL;
	int depth, nalloc;
#ifdef ENABLE_XMLATTR
	struct gfs_xmlattr_ctx *ctxp = NULL;
	int fd, i;
	struct process *process;
	struct inode *inode;
	struct inum_path_array *array = NULL;
#endif

	e = gfm_server_get_request(peer, diag,
			"siiss", &expr, &depth, &nalloc, &ck_path, &ck_name);
#ifdef ENABLE_XMLATTR
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"%s request failed: %s",
			diag, gfarm_error_string(e));
		goto quit;
	}
	if (skip) {
		goto quit;
	}

	if ((ctxp = gfs_xmlattr_ctx_alloc(nalloc)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_UNFIXED,
			"allocation of 'ctxp' failed");
		goto quit;
	}
	ctxp->expr = expr;
	ctxp->depth = depth;
	ctxp->cookie_path = ck_path;
	ctxp->cookie_attrname = ck_name;

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL) {
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_get_process() failed :%s",
			gfarm_error_string(e));
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"peer_fdpair_get_current() failed: %s",
			gfarm_error_string(e));
	} else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_UNFIXED,
			"process_get_file_inode() failed: %s",
			gfarm_error_string(e));
	}
	if (e == GFARM_ERR_NO_ERROR) {
		// giant_unlock() is called in findxmlattr()
		e = findxmlattr(peer, inode, ctxp, &array);
	} else
		giant_unlock();

quit:
	if ((e = gfm_server_put_reply(peer, diag, e, "ii", ctxp->eof,
			ctxp->nvalid)) == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < ctxp->nvalid; i++) {
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

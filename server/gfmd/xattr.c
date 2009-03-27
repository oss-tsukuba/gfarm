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
	if (!isvalid_attrname(attrname))
		return GFARM_ERR_INVALID_ARGUMENT;
	if ((flags & (XATTR_CREATE|XATTR_REPLACE))
		== (XATTR_CREATE|XATTR_REPLACE))
		return GFARM_ERR_INVALID_ARGUMENT;
	if (flags & XATTR_REPLACE) {
		if (inode_xattr_isexists(inode, xmlMode, attrname) == 0)
			return GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		e = inode_xattr_add(inode, xmlMode, attrname);
		if (e == GFARM_ERR_NO_ERROR)
			*addattr = 1;
		else if (e != GFARM_ERR_ALREADY_EXISTS
			|| (flags & XATTR_CREATE))
			return e;
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
	if (e != GFARM_ERR_NO_ERROR)
		goto quit;
	if (skip) {
		goto quit;
	}
	if (xmlMode) {
		waitctx = &ctx;
#ifdef ENABLE_XMLATTR
		if (value[size-1] != '\0') {
			e = GFARM_ERR_INVALID_ARGUMENT;
			goto quit;
		}
#else
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
#endif
	} else
		waitctx = NULL;

	db_waitctx_init(waitctx);
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_access(inode, process_get_user(process),
			GFS_W_OK)) != GFARM_ERR_NO_ERROR)
		;
	else
		e = setxattr(xmlMode, inode, attrname, value, size,
				flags, waitctx, &addattr);
	giant_unlock();

	if (e == GFARM_ERR_NO_ERROR) {
		e = dbq_waitret(waitctx);
		giant_lock();
		if (e == GFARM_ERR_NO_ERROR)
			inode_status_changed(inode);
		else if (addattr)
			inode_xattr_remove(inode, xmlMode, attrname);
		giant_unlock();
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
	if (!isvalid_attrname(attrname))
		return GFARM_ERR_INVALID_ARGUMENT;

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
	if (e != GFARM_ERR_NO_ERROR)
		goto quit;
	if (skip) {
		goto quit;
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
	}
#endif

	db_waitctx_init(&waitctx);
	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_access(inode, process_get_user(process),
			GFS_R_OK)) != GFARM_ERR_NO_ERROR)
		;
	else
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
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_access(inode, process_get_user(process),
			GFS_R_OK)) != GFARM_ERR_NO_ERROR)
		;
	else {
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
	} else
		e = GFARM_ERR_INVALID_ARGUMENT;

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
	if (e != GFARM_ERR_NO_ERROR)
		goto quit;
	if (skip) {
		goto quit;
	}
#ifndef ENABLE_XMLATTR
	if (xmlMode) {
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
	}
#endif

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_access(inode, process_get_user(process),
			GFS_W_OK)) != GFARM_ERR_NO_ERROR)
		;
	else
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
#define DEFAULT_INUM_PATH_ARRAY_SIZE 2
#define MINIMUM_DBQ_FREE_NUM 10

struct inum_path_array;
struct inum_path_entry {
	struct inum_path_array *array;
	gfarm_ino_t inum;
	char *path;
	gfarm_error_t dberr;
	int nfound;
	char **attrnames;
};

struct inum_path_array {
	// for DB waiting
	pthread_mutex_t lock;
	pthread_cond_t cond;

	// for cookie_path
	int check_ckpath;
	int check_ckname;
	int nckpathnames;
	char *restartpath;  // "dir1/dir2" etc
	char *ckpath;       // "dir1\0dir2" etc
	char **ckpathnames;

	// for found path and attrnames
	int nvalid;
	int nfoundsum;
	int filled;
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
	entry->dberr = -1;
}

static void
inum_path_entry_fini(struct inum_path_entry *entry)
{
	int i;

	free(entry->path);
	for (i = 0; i < entry->nfound; i++)
		free(entry->attrnames[i]);
	free(entry->attrnames);
}

static void
inum_path_array_init(struct inum_path_array *array)
{
	int i, err;
	char *msg = "inum_path_array_init";

	memset(array, 0, sizeof(*array));
	if ((err = pthread_mutex_init(&array->lock, NULL)) != 0)
		gflog_fatal("%s: mutex: %s", msg, strerror(err));
	if ((err = pthread_cond_init(&array->cond, NULL)) != 0)
		gflog_fatal("%s: cond: %s", msg, strerror(err));

	for (i = 0; i < GFARM_ARRAY_LENGTH(array->entries); i++)
		inum_path_entry_init(array, &array->entries[i]);
}

static struct inum_path_array *
inum_path_array_alloc(void)
{
	struct inum_path_array *array;

	GFARM_MALLOC(array);
	if (array != NULL)
		inum_path_array_init(array);
	return array;
}

static void
inum_path_array_fini(struct inum_path_array *array)
{
	int i;

	for (i = 0; i < array->nvalid; i++) {
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
inum_path_array_reinit(struct inum_path_array *array)
{
	if (array != NULL) {
		inum_path_array_fini(array);
		inum_path_array_init(array);
	}
}

static struct inum_path_entry *
inum_path_array_addpath(struct inum_path_array *array, gfarm_ino_t inum,
	char *path)
{
	int n = GFARM_ARRAY_LENGTH(array->entries);
	if (array->nvalid < n) {
		array->entries[array->nvalid].inum = inum;
		array->entries[array->nvalid].path = path;
		array->nvalid++;
		if (array->nvalid == n)
			array->filled = 1;
		return &array->entries[array->nvalid - 1];
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

	GFARM_MALLOC_ARRAY(entry->attrnames, nfound);
	if (entry->attrnames == NULL)
		return GFARM_ERR_NO_MEMORY;

	pthread_mutex_lock(&array->lock);
	entry->nfound = nfound;
	for (i = 0; i < nfound; i++) {
		entry->attrnames[i] = vinfo[i].attrname;
		vinfo[i].attrname = NULL; // to avoid free by caller
		vinfo[i].namelen = 0;
	}
	array->nfoundsum += nfound;
	pthread_mutex_unlock(&array->lock);

	return GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
findxmlattr_dbq_enter(struct inum_path_array *array,
	struct inum_path_entry *entry, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	int dbbusy = 0;

	if (db_getfreenum() > MINIMUM_DBQ_FREE_NUM) {
		e = db_xmlattr_find(entry->inum, ctxp->expr,
			inum_path_array_add_attrnames, entry,
			db_findxmlattr_done, entry);
	} else {
		array->filled = 1;
		dbbusy = 1;
		e = (array->nvalid > 1) ? GFARM_ERR_NO_ERROR
			: GFARM_ERR_RESOURCE_TEMPORARILY_UNAVAILABLE;
	}
	if ((e != GFARM_ERR_NO_ERROR) || dbbusy) {
		free(entry->path);
		entry->path = NULL;
		array->nvalid--;
	}
	return e;
}

static gfarm_error_t
inum_path_array_add(struct inum_path_array *array, gfarm_ino_t inum,
	char *path, struct gfs_xmlattr_ctx *ctxp)
{
	struct inum_path_entry *entry;

	entry = inum_path_array_addpath(array, inum, path);
	if (entry != NULL)
		return findxmlattr_dbq_enter(array, entry, ctxp);
	else {
		free(path);
		// path array is enough filled.
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
	 *   array->nckpathnames = 2;
	 *   array->ckpathnames = { "dir1", "dir2" }
	 */
	array->restartpath = path;
	free(array->ckpath);
	array->ckpath = strdup(path);
	if (array->ckpath == NULL)
		return GFARM_ERR_NO_MEMORY;

	array->check_ckpath = array->check_ckname = 1;
	p = array->ckpath;
	array->nckpathnames = 1;
	while ((q = strchr(p, '/')) != NULL) {
		array->nckpathnames++;
		do {
			q++;
		} while (*q == '/');
		p = q;
	}
	GFARM_MALLOC_ARRAY(array->ckpathnames, array->nckpathnames);
	if (array->ckpathnames == NULL) {
		free(array->ckpath);
		array->ckpath = NULL;
		return GFARM_ERR_NO_MEMORY;
	}
	p = array->ckpath;
	for (i = 0; i < array->nckpathnames; i++) {
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

static gfarm_error_t
findxmlattr_make_patharray(struct inode *inode, struct process *process,
	int curdepth, char *parent_path,
	struct inum_path_array *array, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	struct user *user = process_get_user(process);
	struct inode *entry_inode;
	char *name, *dir_path = NULL;
	int add_parent = 1, start_mid = 0;
	int pathlen, namelen, is_dir;
	size_t allocsz;
	int overflow;

	if (!inode_xattr_has_xmlattrs(inode)
		|| (inode_access(inode, user, GFS_R_OK) != GFARM_ERR_NO_ERROR)){
		add_parent = 0;
		e = GFARM_ERR_NO_ERROR;
	}
	if (array->check_ckpath) {
		start_mid = (curdepth < array->nckpathnames);
		if (strcmp(array->restartpath, parent_path) == 0)
			array->check_ckpath = 0;
		else
			add_parent = 0;
	}
	if (add_parent) {
		e = inum_path_array_add(array, inode_get_number(inode),
				parent_path, ctxp);
		if (e != GFARM_ERR_NO_ERROR)
			goto quit;
	}

	if ((curdepth >= ctxp->depth) || array->filled) {
		e = GFARM_ERR_NO_ERROR;
		goto quit;
	}

	dir = inode_get_dir(inode);
	if (dir == NULL) {
		e = GFARM_ERR_NO_ERROR;
		goto quit;
	}
	if (inode_access(inode, user, GFS_X_OK) != GFARM_ERR_NO_ERROR) {
		e = GFARM_ERR_NO_ERROR;
		goto quit;
	}

	if (start_mid) {
		char *path = array->ckpathnames[curdepth];
		if (dir_cursor_lookup(dir, path, strlen(path), &cursor) == 0) {
			e = GFARM_ERR_NO_ERROR;
			goto quit;
		}
	} else {
		if (dir_cursor_set_pos(dir, 0, &cursor) == 0) {
			e = GFARM_ERR_NO_ERROR;
			goto quit;
		}
	}

	pathlen = (parent_path[0] == '\0') ? 0 : (strlen(parent_path) + 1);

	do {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			break;
		name = dir_entry_get_name(entry, &namelen);
		if (is_dot_dir(name, namelen))
			continue;
		entry_inode = dir_entry_get_inode(entry);
		is_dir = inode_is_dir(entry_inode);
		if (!is_dir) {
			if ((inode_access(entry_inode, user, GFS_R_OK)
					!= GFARM_ERR_NO_ERROR)
				|| !inode_xattr_has_xmlattrs(entry_inode)) {
				continue;
			}
		}
		overflow = 0;
		allocsz = gfarm_size_add(&overflow, pathlen, namelen);
		allocsz = gfarm_size_add(&overflow, allocsz, 1);
		if (!overflow)
			GFARM_MALLOC_ARRAY(dir_path, allocsz);
		if (overflow || (dir_path == NULL)) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		if (pathlen > 0)
			sprintf(dir_path, "%s/", parent_path);
		memcpy(dir_path + pathlen, name, namelen);
		dir_path[pathlen + namelen] = '\0';
		if (is_dir) {
			e = findxmlattr_make_patharray(entry_inode, process,
				curdepth + 1, dir_path, array, ctxp);
		} else {
			e = inum_path_array_add(array,
				inode_get_number(entry_inode), dir_path, ctxp);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
	} while ((dir_cursor_next(dir, &cursor) != 0) && !array->filled);

quit:
	if (!add_parent)
		free(parent_path);
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
	while (entry->dberr < 0) {
		err = pthread_cond_wait(&array->cond, &array->lock);
		if (err != 0)
			gflog_fatal("db_findxmlattr_wait: "
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
	if (strcmp(entry->path, ctxp->cookie_path) == 0) {
		for (j = 0; j < entry->nfound; j++) {
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
	if (array->nreplied >= array->nfoundsum)
		return;

	jinit = array->replynameidx;
	for (i = array->replyentidx; i < array->nvalid; i++) {
		entry = &array->entries[i];
		for (j = jinit; j < entry->nfound; j++) {
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
	for (i = 0; i < array->nvalid; i++) {
		db_findxmlattr_wait(array, i);
	}
	pthread_mutex_unlock(&array->lock);
}

static void
findxmlattr_set_found_entries(struct inum_path_array *array,
	struct gfs_xmlattr_ctx *ctxp)
{
	int i, nreply, nremain;
	char *path, *name;

	if (array->check_ckname) {
		findxmlattr_reset_replyidx(array, ctxp);
		array->check_ckname = 0;
	}

	nremain = array->nfoundsum - array->nreplied;
	nreply = (nremain >= ctxp->nalloc)
		? ctxp->nalloc : nremain;
	for (i = 0; i < nreply; i++) {
		findxmlattr_get_nextname(array, ctxp, &path, &name);
		ctxp->entries[i].path = path;
		ctxp->entries[i].attrname = name;
	}

	ctxp->nvalid = nreply;
	ctxp->eof = ((array->filled == 0)
			&& (array->nfoundsum == array->nreplied));
}

static gfarm_error_t
findxmlxattr_restart(struct peer *peer, struct inode *inode,
	struct inum_path_array *array, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	struct process *process = peer_get_process(peer);
	char *restartpath, *p;

	if (array->nvalid == 0)
		return GFARM_ERR_NO_ERROR;

	restartpath = strdup(array->entries[array->nvalid-1].path);
	if (restartpath == NULL)
		return GFARM_ERR_NO_MEMORY;
	inum_path_array_reinit(array);
	e = findxmlattr_set_restart_path(array, restartpath);
	if (e != GFARM_ERR_NO_ERROR)
		return e;
	if ((p = strdup("")) == NULL)
		return GFARM_ERR_NO_MEMORY;

	giant_lock();
	e = findxmlattr_make_patharray(inode, process, 0,
		p, array, ctxp);
	giant_unlock();

	findxmlattr_dbq_wait_all(array, ctxp);
	if (e == GFARM_ERR_NO_ERROR) {
		findxmlattr_set_found_entries(array, ctxp);
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
	struct inum_path_array *array = NULL;
	char *p;

	array = peer_findxmlattrctx_get(peer);
	if (array == NULL) {
		if (ctx_has_cookie(ctxp)) {
			e = GFARM_ERR_INVALID_ARGUMENT;
		} else if ((array = inum_path_array_alloc()) == NULL)
			e = GFARM_ERR_NO_MEMORY;
		else if ((p = strdup("")) == NULL) {
			inum_path_array_free(array);
			e = GFARM_ERR_NO_MEMORY;
		} else {
			e = findxmlattr_make_patharray(inode, process, 0,
				p, array, ctxp);
		}
		giant_unlock();
		// We must wait always even if above function was failed.
		findxmlattr_dbq_wait_all(array, ctxp);
	} else {
		giant_unlock();
		if (!ctx_has_cookie(ctxp))
			e = GFARM_ERR_INVALID_ARGUMENT;
		else
			e = GFARM_ERR_NO_ERROR;
	}

	*ap = array;
	if (e == GFARM_ERR_NO_ERROR)
		findxmlattr_set_found_entries(array, ctxp);

	while ((e == GFARM_ERR_NO_ERROR) &&
			(array->filled) && (ctxp->nvalid == 0)) {
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
	if (e != GFARM_ERR_NO_ERROR)
		goto quit;
	if (skip) {
		goto quit;
	}

	if ((ctxp = gfs_xmlattr_ctx_alloc(nalloc)) == NULL) {
		e = GFARM_ERR_NO_MEMORY;
		goto quit;
	}
	ctxp->expr = expr;
	ctxp->depth = depth;
	ctxp->cookie_path = ck_path;
	ctxp->cookie_attrname = ck_name;

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;

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
				gflog_warning("%s@%s: findxmlattr: %s",
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

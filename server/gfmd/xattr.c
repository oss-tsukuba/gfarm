/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

static int isvalid_attrname(char *attrname)
{
	int namelen = strlen(attrname);

	return ((0 < namelen) && (namelen <= MAX_XATTR_NAME_LEN));
}

static gfarm_error_t
setxattr(int xmlMode, struct inode *inode, char *attrname,
		void *value, size_t size, int flags)
{
	gfarm_error_t e;

	if (!isvalid_attrname(attrname))
		return GFARM_ERR_INVALID_ARGUMENT;
	if ((flags & (XATTR_CREATE|XATTR_REPLACE)) == (XATTR_CREATE|XATTR_REPLACE))
		return GFARM_ERR_INVALID_ARGUMENT;

	if (flags & XATTR_REPLACE) {
		if (inode_xattrname_isexists(inode, xmlMode, attrname))
			e = db_xattr_modify(xmlMode, inode_get_number(inode), attrname, value, size);
		else
			e = GFARM_ERR_NO_SUCH_OBJECT;
	} else {
		e = inode_xattrname_add(inode, xmlMode, attrname);
		if (e == GFARM_ERR_NO_ERROR) {
			e = db_xattr_add(xmlMode, inode_get_number(inode), attrname, value, size);
			if (e != GFARM_ERR_NO_ERROR)
				inode_xattrname_remove(inode, xmlMode, attrname);
		} else if (e == GFARM_ERR_ALREADY_EXISTS && !(flags & XATTR_CREATE)) {
			e = db_xattr_modify(xmlMode, inode_get_number(inode), attrname, value, size);
		}
	}

	if (e == GFARM_ERR_NO_ERROR)
		inode_status_changed(inode);

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

	e = gfm_server_get_request(peer, diag,
	    "sBi", &attrname, &size, &value, &flags);
	if (e != GFARM_ERR_NO_ERROR)
		goto quit;
	if (skip) {
		goto quit;
	}
	if (xmlMode) {
#ifdef ENABLE_XMLATTR
		// drop length of '\0' to handle as text, not binary
		size--;
#else
		e = GFARM_ERR_OPERATION_NOT_SUPPORTED;
		goto quit;
#endif
	}

	giant_lock();
	if ((process = peer_get_process(peer)) == NULL)
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		;
	else if ((e = inode_access(inode, process_get_user(process), GFS_W_OK)) !=
		GFARM_ERR_NO_ERROR)
		;
	else
		e = setxattr(xmlMode, inode, attrname, value, size, flags);
	giant_unlock();
quit:
	free(value);
	free(attrname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static gfarm_error_t
getxattr(int xmlMode, struct inode *inode, char *attrname, void **value, size_t *size)
{
	if (!isvalid_attrname(attrname))
		return GFARM_ERR_INVALID_ARGUMENT;

	/*
	 * We have inode_xattr_isexists() to check.
	 * But we want to return ENOTSUP if xmlMode and backendDB doesn't support XML.
	 * So call db_xattr_load() always.
	 */
	return db_xattr_load(xmlMode, inode_get_number(inode), attrname, value, size);
}

gfarm_error_t
gfm_server_getxattr(struct peer *peer, int from_client, int skip, int xmlMode)
{
	gfarm_error_t e;
	char *diag = xmlMode ? "xmlattr_get" : "xattr_get";
	char *attrname = NULL;
	size_t size;
	void *value = NULL;
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
	else if ((e = inode_access(inode, process_get_user(process), GFS_R_OK)) !=
		GFARM_ERR_NO_ERROR)
		;
	else
		e = getxattr(xmlMode, inode, attrname, &value, &size);
	giant_unlock();
quit:
	e = gfm_server_put_reply(peer, diag, e, "b", size, value);
	free(value);
	free(attrname);
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
		return gfm_server_put_reply(peer, diag, GFARM_ERR_OPERATION_NOT_SUPPORTED, "");
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
	else if ((e = inode_access(inode, process_get_user(process), GFS_R_OK)) !=
		GFARM_ERR_NO_ERROR)
		;
	else
		e = inode_xattrname_list(inode, xmlMode, &value, &size);
	giant_unlock();

	e = gfm_server_put_reply(peer, diag, e, "b", size, value);
	free(value);
	return e;
}

static gfarm_error_t
removexattr(int xmlMode, struct inode *inode, char *attrname)
{
	gfarm_error_t e;

	if (!isvalid_attrname(attrname))
		return GFARM_ERR_INVALID_ARGUMENT;

	e = inode_xattrname_remove(inode, xmlMode, attrname);
	if (e == GFARM_ERR_NO_ERROR) {
		db_xattr_remove(xmlMode, inode_get_number(inode), attrname);
		inode_status_changed(inode);
	}
	return e;
}

gfarm_error_t
gfm_server_removexattr(struct peer *peer, int from_client, int skip, int xmlMode)
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
	else if ((e = inode_access(inode, process_get_user(process), GFS_W_OK)) !=
		GFARM_ERR_NO_ERROR)
		;
	else
		e = removexattr(xmlMode, inode, attrname);
	giant_unlock();
quit:
	free(attrname);
	return (gfm_server_put_reply(peer, diag, e, ""));
}

static void
remove_all_xattrs(struct inode *inode, int xmlMode)
{
	gfarm_error_t e;
	char *value = NULL, *name, *last;
	size_t size;
	gfarm_ino_t inum = inode_get_number(inode);

	if (!inode_xattr_exists(inode, xmlMode))
		return;

	// try remove all xattrs if backend DB supports
	e = db_xattr_remove(xmlMode, inum, NULL);
	if ((e == GFARM_ERR_NO_ERROR) || (e != GFARM_ERR_OPERATION_NOT_SUPPORTED))
		return;

	// get all xattr names, and remove one by one
	if ((e = inode_xattrname_list(inode, xmlMode, &value, &size)) !=
		GFARM_ERR_NO_ERROR)
		return;

	name = value;
	last = value + size;
	while (name < last) {
		e = db_xattr_remove(xmlMode, inum, name);
		if (e != GFARM_ERR_NO_ERROR)
			gflog_warning("removexmlattr: %s",
				gfarm_error_string(e));
		name += (strlen(name) +1);
	}

	free(value);
}

void
gfm_remove_all_xattrs(struct inode *inode)
{
	remove_all_xattrs(inode, 0);
#ifdef ENABLE_XMLATTR
	remove_all_xattrs(inode, 1);
#endif
}

#ifdef ENABLE_XMLATTR
struct inum_path_entry {
	gfarm_ino_t inum;
	char *path;
};

struct inum_path_array {
	int nalloc;
	int nvalid;
	struct inum_path_entry *entries;
};

#define DEFAULT_INUM_PATH_ARRAY_SIZE 100

static void
inum_path_array_init(struct inum_path_array *array)
{
	array->entries = NULL;
	array->nalloc = 0;
	array->nvalid = 0;
}

static void
inum_path_array_fini(struct inum_path_array *array)
{
	int i;
	/*
	 * NOTE: start i from 1, because entries[0] is always
	 * top directory and path is not malloced.
	 */
	for (i = 1; i < array->nvalid; i++) {
		free(array->entries[i].path);
	}
	free(array->entries);
	array->entries = NULL;
	array->nalloc = array->nvalid = 0;
}

static gfarm_error_t
inum_path_array_add(struct inum_path_array *array, gfarm_ino_t inum, char *path)
{
	if (array->nvalid == array->nalloc) {
		struct inum_path_entry *tmp;
		tmp = realloc(array->entries,
				(array->nalloc + DEFAULT_INUM_PATH_ARRAY_SIZE) * sizeof(*tmp));
		if (tmp == NULL) {
			return GFARM_ERR_NO_MEMORY;
		}
		array->entries = tmp;
		array->nalloc += DEFAULT_INUM_PATH_ARRAY_SIZE;
	}

	array->entries[array->nvalid].inum = inum;
	array->entries[array->nvalid].path = path;
	array->nvalid++;
	return GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
findxmlattr_db(gfarm_ino_t inum, char *inode_path, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	int nfound, i;
	struct xattr_list_info *entries = NULL;
	int checkname = 0;

	if (ctxp->cookie_path[0] != '\0') {
		if ((ctxp->cookie_path[0] != '.') && (strcmp(inode_path, ctxp->cookie_path) != 0))
			return GFARM_ERR_NO_ERROR;
		ctxp->cookie_path[0] = '\0';
		checkname = 1;
	}

	e = db_xattr_find(inum, ctxp->expr, &nfound, &entries);
	if (e == GFARM_ERR_NO_SUCH_OBJECT)
		return GFARM_ERR_NO_ERROR;
	if (e != GFARM_ERR_NO_ERROR)
		return e;

	i = 0;
	if (checkname) {
		for (i = 0; i < nfound; i++) {
			if (strcmp(ctxp->cookie_attrname, entries[i].attrname) == 0) {
				i++;
				break;
			}
		}
	}
	while (i < nfound) {
		if (ctxp->nvalid >= ctxp->nalloc)
			break;
		if (inode_path[0] == '\0')
			ctxp->entries[ctxp->nvalid].path = strdup(".");
		else
			ctxp->entries[ctxp->nvalid].path = strdup(inode_path);
		if (ctxp->entries[ctxp->nvalid].path == NULL)
			return GFARM_ERR_NO_MEMORY;
		ctxp->entries[ctxp->nvalid].attrname = strdup(entries[i].attrname);
		if (ctxp->entries[ctxp->nvalid].attrname == NULL)
			return GFARM_ERR_NO_MEMORY;
		ctxp->nvalid++;
		i++;
	}
	ctxp->eof = (i == nfound);
	gfarm_base_xattr_list_free_array(nfound, entries);
	return GFARM_ERR_NO_ERROR;
}

static gfarm_error_t
findxmlattr_inode(struct inode *inode, struct process *process,
		struct gfs_xmlattr_ctx *ctxp, char *inode_path)
{

	if (inode_access(inode, process_get_user(process), GFS_R_OK) != GFARM_ERR_NO_ERROR)
		// skip unaccesable files
		return GFARM_ERR_NO_ERROR;

	return findxmlattr_db(inode_get_number(inode), inode_path, ctxp);
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
findxmlattr_make_patharray(struct inode *inode, struct process *process,
		int curdepth, int maxdepth, char *parent_path,
		struct inum_path_array *array)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	Dir dir;
	DirEntry entry;
	DirCursor cursor;
	struct inode *entry_inode;
	char *name, *dir_path = NULL;
	int pathlen, namelen, is_dir;

	dir = inode_get_dir(inode);
	if (dir == NULL) {
		// inode is not directory
		free(parent_path);
		return GFARM_ERR_NO_ERROR;
	}
	if (inode_access(inode, process_get_user(process),
			GFS_R_OK) != GFARM_ERR_NO_ERROR) {
		// inode is unreadable directory
		free(parent_path);
		return GFARM_ERR_NO_ERROR;
	}
	if (dir_cursor_set_pos(dir, 0, &cursor) == 0) {
		free(parent_path);
		return GFARM_ERR_NO_ERROR;
	}

	e = inum_path_array_add(array, inode_get_number(inode), parent_path);
	if (e != GFARM_ERR_NO_ERROR)
		return e;
	if (curdepth >= maxdepth)
		return GFARM_ERR_NO_ERROR;

	pathlen = (parent_path[0] == '\0') ? 0 : (strlen(parent_path) + 1);		// +1 is '/'

	do {
		entry = dir_cursor_get_entry(dir, &cursor);
		if (entry == NULL)
			break;
		name = dir_entry_get_name(entry, &namelen);
		if (is_dot_dir(name, namelen))
			continue;
		entry_inode = dir_entry_get_inode(entry);
		is_dir = inode_is_dir(entry_inode);
		if (is_dir &&
				(inode_access(inode, process_get_user(process), GFS_X_OK)
						!= GFARM_ERR_NO_ERROR))
			continue;

		dir_path = malloc(pathlen + namelen + 1);
		if (dir_path == NULL) {
			e = GFARM_ERR_NO_MEMORY;
			break;
		}
		if (pathlen > 0)
			sprintf(dir_path, "%s/", parent_path);
		memcpy(dir_path + pathlen, name, namelen);
		dir_path[pathlen + namelen] = '\0';
		if (is_dir) {
			e = findxmlattr_make_patharray(entry_inode, process,
					curdepth + 1, maxdepth, dir_path, array);
		} else {
			e = inum_path_array_add(array,
					inode_get_number(entry_inode), dir_path);
		}
		if (e != GFARM_ERR_NO_ERROR)
			break;
	} while (dir_cursor_next(dir, &cursor) != 0);

	return e;
}

static gfarm_error_t
findxmlattr_search_patharray(struct inum_path_array *array,struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	int i;

	for (i = 0; i < array->nvalid; i++) {
		e = findxmlattr_db(array->entries[i].inum, array->entries[i].path, ctxp);
		if (e != GFARM_ERR_NO_ERROR)
			return e;
		if (ctxp->nvalid >= ctxp->nalloc)
			break;
	}
	if (i < array->nvalid)
		ctxp->eof = 0;

	return GFARM_ERR_NO_ERROR;
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
	if ((process = peer_get_process(peer)) == NULL) {
		giant_unlock();
		e = GFARM_ERR_OPERATION_NOT_PERMITTED;
	} else if ((e = peer_fdpair_get_current(peer, &fd)) !=
	    GFARM_ERR_NO_ERROR)
		giant_unlock();
	else if ((e = process_get_file_inode(process, fd, &inode)) !=
	    GFARM_ERR_NO_ERROR)
		giant_unlock();
	else if (GFARM_S_ISDIR(inode_get_mode(inode))) {
		struct inum_path_array array;
		inum_path_array_init(&array);
		e = findxmlattr_make_patharray(inode, process, 0, ctxp->depth, "", &array);
		giant_unlock();
		if (e == GFARM_ERR_NO_ERROR)
			e = findxmlattr_search_patharray(&array, ctxp);
		inum_path_array_fini(&array);
	} else {
		giant_unlock();
		e = findxmlattr_inode(inode, process, ctxp, "");
	}

quit:
	if ((e = gfm_server_put_reply(peer, diag, e, "ii", ctxp->eof, ctxp->nvalid)) == GFARM_ERR_NO_ERROR) {
		for (i = 0; i < ctxp->nvalid; i++) {
			e = gfp_xdr_send(peer_get_conn(peer), "ss",
				    ctxp->entries[i].path, ctxp->entries[i].attrname);
			if (e != GFARM_ERR_NO_ERROR) {
				gflog_warning("%s@%s: findxmlattr: %s",
					peer_get_username(peer),
					peer_get_hostname(peer),
					gfarm_error_string(e));
				break;
			}
		}
	}
	gfs_xmlattr_ctx_free(ctxp);
	return e;
#else
	free(expr);
	free(ck_path);
	free(ck_name);
	return gfm_server_put_reply(peer, diag,
			GFARM_ERR_OPERATION_NOT_SUPPORTED, "");
#endif
}

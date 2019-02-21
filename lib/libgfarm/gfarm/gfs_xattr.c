/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

#include <stdio.h>	/* config.h needs FILE */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>

#include "gfutil.h"
#include "timer.h"

#include "context.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_io.h"
#include "gfs_pio.h"
#include "gfs_profile.h"
#include "gfs_failover.h"
#include "gfs_misc.h"
#include "xattr_info.h"

#define staticp	(gfarm_ctxp->gfs_xattr_static)

struct gfarm_gfs_xattr_static {
	double xattr_time;
	unsigned long long xattr_count;
};

gfarm_error_t
gfarm_gfs_xattr_static_init(struct gfarm_context *ctxp)
{
	struct gfarm_gfs_xattr_static *s;

	GFARM_MALLOC(s);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);

	s->xattr_time = 0;
	s->xattr_count = 0;

	ctxp->gfs_xattr_static = s;
	return (GFARM_ERR_NO_ERROR);
}

void
gfarm_gfs_xattr_static_term(struct gfarm_context *ctxp)
{
	free(ctxp->gfs_xattr_static);
}

struct gfm_setxattr0_closure {
	int xmlMode;
	const char *name;
	const void *value;
	size_t size;
	int flags;
};

static gfarm_error_t
gfm_setxattr0_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_setxattr0_closure *c = closure;
	gfarm_error_t e = gfm_client_setxattr_request(gfm_server,
	    c->xmlMode, c->name, c->value, c->size, c->flags);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000160,
		    "setxattr request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_setxattr0_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_setxattr_result(gfm_server);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000161,
		    "setxattr result: %s", gfarm_error_string(e));
#endif
	return (e);
}

static int
gfm_setxattr0_must_be_warned(gfarm_error_t e, void *closure)
{
	struct gfm_setxattr0_closure *sc = closure;

	/* error returned from inode_xattr_add() */
	return ((sc->flags & GFS_XATTR_CREATE) != 0 &&
	    e == GFARM_ERR_ALREADY_EXISTS);
}

static gfarm_error_t
gfs_setxattr0(int xmlMode, int cflags, const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	gfarm_timerval_t t1, t2;
	struct gfm_setxattr0_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = xmlMode;
	closure.name = name;
	closure.value = value;
	closure.size = size;
	closure.flags = flags;
	e = gfm_inode_op_modifiable(path, cflags|GFARM_FILE_LOOKUP,
	    gfm_setxattr0_request,
	    gfm_setxattr0_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    gfm_setxattr0_must_be_warned,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001398,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_setxattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return (gfs_setxattr0(0, 0, path, name, value, size, flags));
}

gfarm_error_t
gfs_lsetxattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return (gfs_setxattr0(0, GFARM_FILE_SYMLINK_NO_FOLLOW, path,
		name, value, size, flags));
}

gfarm_error_t
gfs_setxmlattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return (gfs_setxattr0(1, 0, path, name, value, size, flags));
}

gfarm_error_t
gfs_lsetxmlattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return (gfs_setxattr0(1, GFARM_FILE_SYMLINK_NO_FOLLOW, path,
		name, value, size, flags));
}

gfarm_error_t
gfs_fsetxattr(GFS_File gf, const char *name, const void *value,
	size_t size, int flags)
{
	gfarm_timerval_t t1, t2;
	struct gfm_setxattr0_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = 0;
	closure.name = name;
	closure.value = value;
	closure.size = size;
	closure.flags = flags;
	e = gfm_client_compound_file_op_modifiable(gf,
	    gfm_setxattr0_request,
	    gfm_setxattr0_result,
	    NULL,
	    gfm_setxattr0_must_be_warned,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003985,
		    "gfm_client_compound_file_op_modifiable: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

struct gfm_getxattr_proccall_closure {
	int xmlMode;
	const char *name;
	void **valuep;
	size_t *sizep;
};

static gfarm_error_t
gfm_getxattr_proccall_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_getxattr_proccall_closure *c = closure;
	gfarm_error_t e = gfm_client_getxattr_request(gfm_server,
	    c->xmlMode, c->name);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000162,
		    "getxattr request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_getxattr_proccall_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_getxattr_proccall_closure *c = closure;
	gfarm_error_t e = gfm_client_getxattr_result(gfm_server,
	    c->xmlMode, c->valuep, c->sizep);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000163,
		    "getxattr result: %s", gfarm_error_string(e));

#endif
	return (e);
}

static gfarm_error_t
gfs_getxattr_proccall(int xmlMode, int cflags, const char *path,
	const char *name, void **valuep, size_t *sizep)
{
	gfarm_timerval_t t1, t2;
	struct gfm_getxattr_proccall_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = xmlMode;
	closure.name = name;
	closure.valuep = valuep;
	closure.sizep = sizep;
	e = gfm_inode_op_readonly(path, cflags|GFARM_FILE_LOOKUP,
	    gfm_getxattr_proccall_request,
	    gfm_getxattr_proccall_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001400,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

static gfarm_error_t
gfs_fgetxattr_proccall(int xmlMode, GFS_File gf, const char *name,
	void **valuep, size_t *sizep)
{
	gfarm_timerval_t t1, t2;
	struct gfm_getxattr_proccall_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = xmlMode;
	closure.name = name;
	closure.valuep = valuep;
	closure.sizep = sizep;
	e = gfm_client_compound_file_op_readonly(gf,
	    gfm_getxattr_proccall_request,
	    gfm_getxattr_proccall_result,
	    NULL,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003986,
		    "gfm_client_compound_file_op_readonly: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

static gfarm_error_t
gfs_getxattr0(int xmlMode, const char *path, GFS_File gf, int cflags,
		const char *name, void *value, size_t *size)
{
	gfarm_error_t e;
	void *v;
	size_t s;

	if (path != NULL)
		e = gfs_getxattr_proccall(xmlMode, cflags, path, name, &v, &s);
	else
		e = gfs_fgetxattr_proccall(xmlMode, gf, name, &v, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001402,
		    "%sgetxattr_proccall(%s, %s) failed: %s",
		    path != NULL ? "" : "f",
		    path != NULL ? path : gfs_pio_url(gf), name,
		    gfarm_error_string(e));
		return (e);
	}

	if (*size >= s)
		memcpy(value, v, s);
	else if (*size != 0) {
		gflog_debug(GFARM_MSG_1001403,
			"Result out of range (%llu) < (%llu): %s",
			(unsigned long long)*size, (unsigned long long)s,
			gfarm_error_string(GFARM_ERR_RESULT_OUT_OF_RANGE));
		e = GFARM_ERR_RESULT_OUT_OF_RANGE;
	}
	*size = s;
	free(v);
	return (e);
}

gfarm_error_t
gfs_getxattr(const char *path, const char *name, void *value, size_t *size)
{
	return (gfs_getxattr0(0, path, NULL, 0, name, value, size));
}

gfarm_error_t
gfs_lgetxattr(const char *path, const char *name, void *value, size_t *size)
{
	return (gfs_getxattr0(0, path, NULL, GFARM_FILE_SYMLINK_NO_FOLLOW,
		name, value, size));
}

gfarm_error_t
gfs_getxmlattr(const char *path, const char *name, void *value, size_t *size)
{
	return (gfs_getxattr0(1, path, NULL, 0, name, value, size));
}

gfarm_error_t
gfs_lgetxmlattr(const char *path, const char *name, void *value, size_t *size)
{
	return (gfs_getxattr0(1, path, NULL, GFARM_FILE_SYMLINK_NO_FOLLOW,
		name, value, size));
}

gfarm_error_t
gfs_fgetxattr(GFS_File gf, const char *name, void *value, size_t *size)
{
	return (gfs_getxattr0(0, NULL, gf, 0, name, value, size));
}


struct gfm_listxattr_proccall_closure {
	int xmlMode;
	char **listp;
	size_t *sizep;
};

static gfarm_error_t
gfm_listxattr_proccall_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_listxattr_proccall_closure *c = closure;
	gfarm_error_t e = gfm_client_listxattr_request(gfm_server, c->xmlMode);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000164,
		    "listxattr request: %s", gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_listxattr_proccall_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_listxattr_proccall_closure *c = closure;
	gfarm_error_t e = gfm_client_listxattr_result(gfm_server,
	    c->listp, c->sizep);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000165,
		    "listxattr result: %s", gfarm_error_string(e));
#endif
	return (e);
}

static gfarm_error_t
gfs_listxattr_proccall(int xmlMode, int cflags, const char *path,
	char **listp, size_t *sizep)
{
	gfarm_timerval_t t1, t2;
	struct gfm_listxattr_proccall_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = xmlMode;
	closure.listp = listp;
	closure.sizep = sizep;
	e = gfm_inode_op_readonly(path, cflags|GFARM_FILE_LOOKUP,
	    gfm_listxattr_proccall_request,
	    gfm_listxattr_proccall_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001404,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

static gfarm_error_t
gfs_listxattr0(int xmlMode, int cflags, const char *path, char *list,
	size_t *size)
{
	gfarm_error_t e;
	char *l;
	size_t s;

	e = gfs_listxattr_proccall(xmlMode, cflags, path, &l, &s);
	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001405,
			"gfs_listxattr_proccall(%s) failed: %s",
			path,
			gfarm_error_string(e));
		return (e);
	}

	if (*size >= s)
		memcpy(list, l, s);
	else if (*size != 0) {
		gflog_debug(GFARM_MSG_1001406,
			"Result out of range (%llu) < (%llu): %s",
			(unsigned long long)*size, (unsigned long long)s,
			gfarm_error_string(GFARM_ERR_RESULT_OUT_OF_RANGE));
		e = GFARM_ERR_RESULT_OUT_OF_RANGE;
	}
	*size = s;
	free(l);
	return (e);
}

gfarm_error_t
gfs_listxattr(const char *path, char *list, size_t *size)
{
	return (gfs_listxattr0(0, 0, path, list, size));
}

gfarm_error_t
gfs_llistxattr(const char *path, char *list, size_t *size)
{
	return (gfs_listxattr0(0, GFARM_FILE_SYMLINK_NO_FOLLOW,
		path, list, size));
}

gfarm_error_t
gfs_listxmlattr(const char *path, char *list, size_t *size)
{
	return (gfs_listxattr0(1, 0, path, list, size));
}

gfarm_error_t
gfs_llistxmlattr(const char *path, char *list, size_t *size)
{
	return (gfs_listxattr0(1, GFARM_FILE_SYMLINK_NO_FOLLOW,
		path, list, size));
}

struct gfm_removexattr0_closure {
	int xmlMode;
	const char *name;
};

static gfarm_error_t
gfm_removexattr0_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfm_removexattr0_closure *c = closure;
	gfarm_error_t e = gfm_client_removexattr_request(gfm_server,
	    c->xmlMode, c->name);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000166, "removexattr request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_removexattr0_result(struct gfm_connection *gfm_server, void *closure)
{
	gfarm_error_t e = gfm_client_removexattr_result(gfm_server);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000167,
		    "removexattr result: %s", gfarm_error_string(e));
#endif
	return (e);
}

static int
gfm_removexattr0_must_be_warned(gfarm_error_t e, void *closure)
{
	/* error returned from inode_xattr_remove() */
	return (e == GFARM_ERR_NO_SUCH_OBJECT);
}

static gfarm_error_t
gfs_removexattr0(int xmlMode, int cflags, const char *path, const char *name)
{
	gfarm_timerval_t t1, t2;
	struct gfm_removexattr0_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = xmlMode;
	closure.name = name;
	e = gfm_inode_op_modifiable(path, cflags|GFARM_FILE_LOOKUP,
	    gfm_removexattr0_request,
	    gfm_removexattr0_result,
	    gfm_inode_success_op_connection_free,
	    NULL,
	    gfm_removexattr0_must_be_warned,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001407,
			"gfm_inode_op(%s) failed: %s",
			path,
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t gfs_removexattr(const char *path, const char *name)
{
	return (gfs_removexattr0(0, 0, path, name));
}

gfarm_error_t gfs_lremovexattr(const char *path, const char *name)
{
	return (gfs_removexattr0(0, GFARM_FILE_SYMLINK_NO_FOLLOW, path, name));
}

gfarm_error_t gfs_removexmlattr(const char *path, const char *name)
{
	return (gfs_removexattr0(1, 0, path, name));
}

gfarm_error_t gfs_lremovexmlattr(const char *path, const char *name)
{
	return (gfs_removexattr0(1, GFARM_FILE_SYMLINK_NO_FOLLOW,
		path, name));
}

gfarm_error_t
gfs_fremovexattr(GFS_File gf, const char *name)
{
	gfarm_timerval_t t1, t2;
	struct gfm_removexattr0_closure closure;
	gfarm_error_t e;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	closure.xmlMode = 0;
	closure.name = name;
	e = gfm_client_compound_file_op_modifiable(gf,
	    gfm_removexattr0_request,
	    gfm_removexattr0_result,
	    NULL,
	    gfm_removexattr0_must_be_warned,
	    &closure);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1003987,
		    "gfm_client_compound_file_op_modifiable: %s",
		    gfarm_error_string(e));
	}

	return (e);
}

#ifndef GFARM_DEFAULT_FINDXMLATTR_NENRTY
#define GFARM_DEFAULT_FINDXMLATTR_NENRTY 100
#endif

struct gfs_xmlattr_ctx *
gfs_xmlattr_ctx_alloc(int nentry)
{
	struct gfs_xmlattr_ctx *ctxp;
	size_t ctxsize;
	int overflow;
	char *p = NULL;

	overflow = 0;
	ctxsize = gfarm_size_add(&overflow, sizeof(*ctxp),
	    gfarm_size_mul(&overflow, nentry, sizeof(*ctxp->entries)));
	if (!overflow)
		p = calloc(1, ctxsize);
	if (p != NULL) {
		ctxp = (struct gfs_xmlattr_ctx *)p;
		ctxp->nalloc = nentry;
		ctxp->entries = (struct gfs_foundxattr_entry *)(ctxp + 1);
		return (ctxp);
	} else
		return (NULL);
}

static void
gfs_xmlattr_ctx_free_entries(struct gfs_xmlattr_ctx *ctxp, int freepath)
{
	int i;

	for (i = 0; i < ctxp->nvalid; i++) {
		if (freepath) {
			free(ctxp->entries[i].path);
			free(ctxp->entries[i].attrname);
		}
		ctxp->entries[i].path = NULL;
		ctxp->entries[i].attrname = NULL;
	}
	ctxp->index = 0;
	ctxp->nvalid = 0;
}

void
gfs_xmlattr_ctx_free(struct gfs_xmlattr_ctx *ctxp, int freepath)
{
	if (ctxp != NULL) {
		gfs_xmlattr_ctx_free_entries(ctxp, freepath);
		free(ctxp->path);
		free(ctxp->expr);
		free(ctxp->cookie_path);
		free(ctxp->cookie_attrname);
		free(ctxp->workpath);
		free(ctxp);
	}
}

gfarm_error_t
gfs_findxmlattr(const char *path, const char *expr,
	int depth, struct gfs_xmlattr_ctx **ctxpp)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	struct gfs_xmlattr_ctx *ctxp;
	char *url;
	gfarm_ino_t ino;
	gfarm_uint64_t gen;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((ctxp = gfs_xmlattr_ctx_alloc(GFARM_DEFAULT_FINDXMLATTR_NENRTY))
			== NULL) {
		e = GFARM_ERR_NO_MEMORY;
		gflog_debug(GFARM_MSG_1001409,
			"allococation of 'gfs_xmlattr_ctx' failed: %s",
			gfarm_error_string(GFARM_ERR_NO_MEMORY));
	} else if ((e = gfm_open_fd(path, GFARM_FILE_RDONLY,
	    &ctxp->gfm_server, &ctxp->fd, &ctxp->type, &url, &ino, &gen, NULL))
		!= GFARM_ERR_NO_ERROR) {
		gfs_xmlattr_ctx_free(ctxp, 1);
		gflog_debug(GFARM_MSG_1003988,
		    "gfm_open_fd(%s) failed: %s",
		    path, gfarm_error_string(e));
	} else {
		free(url);
		ctxp->path = strdup(path);
		ctxp->expr = strdup(expr);
		ctxp->depth = depth;
		ctxp->ino = ino;
		*ctxpp = ctxp;
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	return (e);
}

static gfarm_error_t
gfm_findxmlattr_request(struct gfm_connection *gfm_server, void *closure)
{
	struct gfs_xmlattr_ctx *ctxp = closure;
	gfarm_error_t e = gfm_client_findxmlattr_request(gfm_server, ctxp);

	if (e != GFARM_ERR_NO_ERROR)
		gflog_warning(GFARM_MSG_1000168, "find_xml_attr request: %s",
		    gfarm_error_string(e));
	return (e);
}

static gfarm_error_t
gfm_findxmlattr_result(struct gfm_connection *gfm_server, void *closure)
{
	struct gfs_xmlattr_ctx *ctxp = closure;
	gfarm_error_t e = gfm_client_findxmlattr_result(gfm_server, ctxp);

#if 1 /* DEBUG */
	if (e != GFARM_ERR_NO_ERROR)
		gflog_debug(GFARM_MSG_1000169,
		    "find_xml_attr result: %s", gfarm_error_string(e));
#endif
	return (e);
}

static struct gfm_connection*
xmlattr_ctx_metadb(struct gfs_failover_file *super)
{
	return (((struct gfs_xmlattr_ctx *)super)->gfm_server);
}

static void
xmlattr_ctx_set_metadb(struct gfs_failover_file *super,
	struct gfm_connection *gfm_server)
{
	((struct gfs_xmlattr_ctx *)super)->gfm_server = gfm_server;
}

static gfarm_int32_t
xmlattr_ctx_fileno(struct gfs_failover_file *super)
{
	return (((struct gfs_xmlattr_ctx *)super)->fd);
}

static void
xmlattr_ctx_set_fileno(struct gfs_failover_file *super, gfarm_int32_t fd)
{
	((struct gfs_xmlattr_ctx *)super)->fd = fd;
}

static const char*
xmlattr_ctx_url(struct gfs_failover_file *super)
{
	return (((struct gfs_xmlattr_ctx *)super)->path);
}

static gfarm_ino_t
xmlattr_ctx_ino(struct gfs_failover_file *super)
{
	return (((struct gfs_xmlattr_ctx *)super)->ino);
}

static gfarm_error_t
gfs_findxmlattr_get(struct gfs_xmlattr_ctx *ctxp)
{
	struct gfs_failover_file_ops failover_file_ops = {
		ctxp->type,
		xmlattr_ctx_metadb,
		xmlattr_ctx_set_metadb,
		xmlattr_ctx_fileno,
		xmlattr_ctx_set_fileno,
		xmlattr_ctx_url,
		xmlattr_ctx_ino,
	};

	return (gfm_client_compound_fd_op_readonly(
	    (struct gfs_failover_file *)ctxp, &failover_file_ops,
	    gfm_findxmlattr_request, gfm_findxmlattr_result, NULL,
	    ctxp));
}

gfarm_error_t
gfs_getxmlent(struct gfs_xmlattr_ctx *ctxp, char **fpathp, char **namep)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_timerval_t t1, t2;
	char *fpath, *p;
	int pathlen, overflow;
	size_t allocsz;

#ifdef __GNUC__ /* workaround gcc warning: might be used uninitialized */
	p = NULL;
#endif
	*fpathp = NULL;
	*namep = NULL;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((ctxp->eof == 0) && (ctxp->index >= ctxp->nvalid)) {
		gfs_xmlattr_ctx_free_entries(ctxp, 1);
		e = gfs_findxmlattr_get(ctxp);
	}
	if (e == GFARM_ERR_NO_ERROR) {
		if (ctxp->index < ctxp->nvalid) {
			fpath = ctxp->entries[ctxp->index].path;
			pathlen = strlen(ctxp->path);
			overflow = 0;
			allocsz = gfarm_size_add(&overflow, gfarm_size_add(
			    &overflow, pathlen, strlen(fpath)), 2);
			if (!overflow)
				p = realloc(ctxp->workpath, allocsz);
			if (!overflow && (p != NULL)) {
				ctxp->workpath = p;
				if (ctxp->path[pathlen-1] == '/')
					sprintf(ctxp->workpath, "%s%s",
					    ctxp->path, fpath);
				else
					sprintf(ctxp->workpath, "%s/%s",
					    ctxp->path, fpath);
				pathlen = strlen(ctxp->workpath);
				if ((pathlen > 1) && (ctxp->workpath[pathlen-1]
				    == '/'))
					ctxp->workpath[pathlen-1] = '\0';
				*fpathp = ctxp->workpath;
				*namep = ctxp->entries[ctxp->index].attrname;
				ctxp->index++;
			} else
				e = GFARM_ERR_NO_MEMORY;
		}
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001411,
			"error occurred during process: %s",
			gfarm_error_string(e));
	}

	return (e);
}

gfarm_error_t
gfs_closexmlattr(struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if (ctxp != NULL) {
		e = gfm_close_fd(ctxp->gfm_server, ctxp->fd, NULL, NULL);
		gfm_client_connection_free(ctxp->gfm_server);
		gfs_xmlattr_ctx_free(ctxp, 1);
	} else {
		e = GFARM_ERR_NO_ERROR;
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(staticp->xattr_time += gfarm_timerval_sub(&t2, &t1));
	gfs_profile(staticp->xattr_count++);

	if (e != GFARM_ERR_NO_ERROR) {
		gflog_debug(GFARM_MSG_1001412,
			"gfm_close_fd() failed: %s",
			gfarm_error_string(e));
	}

	/* ignore result */
	return (GFARM_ERR_NO_ERROR);
}

struct gfs_profile_list xattr_profile_items[] = {
	{ "xattr_time", "gfs_xattr time  : %g sec", "%g", 'd',
	  offsetof(struct gfarm_gfs_xattr_static, xattr_time) },
	{ "xattr_count", "gfs_xattr count : %llu", "%llu", 'l',
	  offsetof(struct gfarm_gfs_xattr_static, xattr_count) },
};

void
gfs_xattr_display_timers(void)
{
	int n = GFARM_ARRAY_LENGTH(xattr_profile_items);

	gfs_profile_display_timers(n, xattr_profile_items, staticp);
}

gfarm_error_t
gfs_xattr_profile_value(const char *name, char *value, size_t *sizep)
{
	int n = GFARM_ARRAY_LENGTH(xattr_profile_items);

	return (gfs_profile_value(name, n, xattr_profile_items,
		    staticp, value, sizep));
}

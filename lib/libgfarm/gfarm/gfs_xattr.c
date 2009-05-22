/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

#include <stdio.h>	/* config.h needs FILE */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define GFARM_INTERNAL_USE
#include <gfarm/gfarm.h>
#include "xattr_info.h"
#include "gfutil.h"
#include "timer.h"
#include "gfm_client.h"
#include "lookup.h"
#include "gfs_io.h"

#include "config.h"
#include "gfs_profile.h"


static double gfs_xattr_time = 0.0;

static gfarm_error_t
gfs_setxattr0(int xmlMode, const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_tmp_open_request(gfm_server, path,
		    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_setxattr_request(gfm_server,
				xmlMode, name, value, size, flags))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("setxattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_tmp_open_result(gfm_server, path, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_setxattr_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("setxattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t
gfs_setxattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return gfs_setxattr0(0, path, name, value, size, flags);
}

gfarm_error_t
gfs_setxmlattr(const char *path, const char *name,
	const void *value, size_t size, int flags)
{
	return gfs_setxattr0(1, path, name, value, size, flags);
}

gfarm_error_t
gfs_fsetxattr(GFS_File gf, const char *name, const void *value,
		size_t size, int flags)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_request(
				  gfm_server, gfs_pio_fileno(gf)))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_setxattr_request(gfm_server,
				0, name, value, size, flags))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("setxattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_setxattr_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("setxattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
gfs_getxattr_proccall(int xmlMode, const char *path, const char *name,
		void **valuep, size_t *size)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_tmp_open_request(gfm_server, path,
		    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_getxattr_request(gfm_server,
				xmlMode, name)) != GFARM_ERR_NO_ERROR)
			gflog_warning("getxattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_tmp_open_result(gfm_server, path, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_getxattr_result(gfm_server,
				xmlMode, valuep, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning("getxattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
gfs_fgetxattr_proccall(int xmlMode, GFS_File gf, const char *name,
		void **valuep, size_t *size)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_request(
				  gfm_server, gfs_pio_fileno(gf)))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_getxattr_request(
				gfm_server, xmlMode, name))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("getxattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_getxattr_result(gfm_server,
				xmlMode, valuep, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning("getxattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
gfs_getxattr0(int xmlMode, const char *path, GFS_File gf,
		const char *name, void *value, size_t *size)
{
	gfarm_error_t e;
	void *v;
	size_t s;

	if (path != NULL)
		e = gfs_getxattr_proccall(xmlMode, path, name, &v, &s);
	else
		e = gfs_fgetxattr_proccall(xmlMode, gf, name, &v, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return e;

	if (*size >= s)
		memcpy(value, v, s);
	else if (*size != 0)
		e = GFARM_ERR_RESULT_OUT_OF_RANGE;
	*size = s;
	free(v);
	return e;
}

gfarm_error_t
gfs_getxattr(const char *path, const char *name, void *value, size_t *size)
{
	return gfs_getxattr0(0, path, NULL, name, value, size);
}

gfarm_error_t
gfs_getxmlattr(const char *path, const char *name, void *value, size_t *size)
{
	return gfs_getxattr0(1, path, NULL, name, value, size);
}

gfarm_error_t
gfs_fgetxattr(GFS_File gf, const char *name, void *value, size_t *size)
{
	return gfs_getxattr0(0, NULL, gf, name, value, size);
}

static gfarm_error_t
gfs_listxattr_proccall(int xmlMode, const char *path, char **listp,
		size_t *size)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_tmp_open_request(gfm_server, path,
		    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_listxattr_request(gfm_server,
		    xmlMode)) != GFARM_ERR_NO_ERROR)
			gflog_warning("listxattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_tmp_open_result(gfm_server, path, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_listxattr_result(gfm_server,
				listp, size)) != GFARM_ERR_NO_ERROR)
			gflog_warning("listxattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
gfs_listxattr0(int xmlMode, const char *path, char *list, size_t *size)
{
	gfarm_error_t e;
	char *l;
	size_t s;

	e = gfs_listxattr_proccall(xmlMode, path, &l, &s);
	if (e != GFARM_ERR_NO_ERROR)
		return e;

	if (*size >= s)
		memcpy(list, l, s);
	else if (*size != 0)
		e = GFARM_ERR_RESULT_OUT_OF_RANGE;
	*size = s;
	free(l);
	return e;
}

gfarm_error_t
gfs_listxattr(const char *path, char *list, size_t *size)
{
	return gfs_listxattr0(0, path, list, size);
}

gfarm_error_t
gfs_listxmlattr(const char *path, char *list, size_t *size)
{
	return gfs_listxattr0(1, path, list, size);
}

static gfarm_error_t
gfs_removexattr0(int xmlMode, const char *path, const char *name)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_tmp_open_request(gfm_server, path,
		    GFARM_FILE_LOOKUP)) != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) request: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_removexattr_request(gfm_server,
				xmlMode, name)) != GFARM_ERR_NO_ERROR)
			gflog_warning("removexattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_tmp_open_result(gfm_server, path, NULL))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("tmp_open(%s) result: %s", path,
			    gfarm_error_string(e));
		else if ((e = gfm_client_removexattr_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("removexattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	/* NOTE: the opened descriptor is automatically closed by gfmd */

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

gfarm_error_t gfs_removexattr(const char *path, const char *name)
{
	return gfs_removexattr0(0, path, name);
}

gfarm_error_t gfs_removexmlattr(const char *path, const char *name)
{
	return gfs_removexattr0(1, path, name);
}

gfarm_error_t
gfs_fremovexattr(GFS_File gf, const char *name)
{
	gfarm_error_t e;
	struct gfm_connection *gfm_server;
	int retry = 0;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_put_fd_request(
				  gfm_server, gfs_pio_fileno(gf)))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_removexattr_request(
				gfm_server, 0, name))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("removexattr request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_client_put_fd_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("put_fd result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_removexattr_result(gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("removexattr result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(gfm_server))
		    != GFARM_ERR_NO_ERROR) {
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		}

		break;
	}
	gfm_client_connection_free(gfm_server);

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

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
		return ctxp;
	} else
		return NULL;
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

static gfarm_error_t
gfs_findxmlattr_open(const char *path, struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	int retry = 0;

	/*
	 * NOTE: I copied this function from gfs_desc_open()
	 * to known fd, is_dir and not to allocate some buffers
	 * by gfs_desc_open_common().
	 */
	for (;;) {
		if ((e = gfarm_metadb_connection_acquire(&ctxp->gfm_server)) !=
		    GFARM_ERR_NO_ERROR)
			return (e);

		if ((e = gfm_client_compound_begin_request(ctxp->gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_begin request: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_open_request(ctxp->gfm_server, path,
		    GFARM_FILE_RDONLY)) != GFARM_ERR_NO_ERROR)
			gflog_warning("open path request; %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_request(
		    ctxp->gfm_server)) != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end request: %s",
			    gfarm_error_string(e));

		else if ((e = gfm_client_compound_begin_result(
		    ctxp->gfm_server)) != GFARM_ERR_NO_ERROR) {
			if (gfm_client_is_connection_error(e) && ++retry <= 1){
				gfm_client_connection_free(ctxp->gfm_server);
				continue;
			}
			gflog_warning("compound_begin result: %s",
			    gfarm_error_string(e));
		} else if ((e = gfm_open_result(ctxp->gfm_server, path,
		    &ctxp->fd, &ctxp->is_dir)) != GFARM_ERR_NO_ERROR)
			gflog_warning("open path result: %s",
			    gfarm_error_string(e));
		else if ((e = gfm_client_compound_end_result(ctxp->gfm_server))
		    != GFARM_ERR_NO_ERROR)
			gflog_warning("compound_end result: %s",
			    gfarm_error_string(e));
		else
			return (GFARM_ERR_NO_ERROR);

		gfm_client_connection_free(ctxp->gfm_server);
		return (e);
	}
}

gfarm_error_t
gfs_findxmlattr(const char *path, const char *expr,
	int depth, struct gfs_xmlattr_ctx **ctxpp)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;
	struct gfs_xmlattr_ctx *ctxp;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if ((ctxp = gfs_xmlattr_ctx_alloc(GFARM_DEFAULT_FINDXMLATTR_NENRTY))
			== NULL)
		e = GFARM_ERR_NO_MEMORY;
	else if ((e = gfs_findxmlattr_open(path, ctxp)) != GFARM_ERR_NO_ERROR)
		gfs_xmlattr_ctx_free(ctxp, 1);
	else {
		ctxp->path = strdup(path);
		ctxp->expr = strdup(expr);
		ctxp->depth = depth;
		*ctxpp = ctxp;
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

static gfarm_error_t
gfs_findxmlattr_get(struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;

	if ((e = gfm_client_compound_begin_request(ctxp->gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_request(ctxp->gfm_server, ctxp->fd))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd request: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_findxmlattr_request(
		ctxp->gfm_server, ctxp)) != GFARM_ERR_NO_ERROR)
		gflog_warning("find_xml_attr request: %s",
				gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_request(ctxp->gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end request: %s",
		    gfarm_error_string(e));

	else if ((e = gfm_client_compound_begin_result(ctxp->gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_begin result: %s",
		    gfarm_error_string(e));
	else if ((e = gfm_client_put_fd_result(ctxp->gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("put_fd result: %s",
		    gfarm_error_string(e));
	 else if ((e = gfm_client_findxmlattr_result(
			 ctxp->gfm_server, ctxp)) != GFARM_ERR_NO_ERROR)
		 gflog_warning("find_xml_attr result: %s",
			gfarm_error_string(e));
	else if ((e = gfm_client_compound_end_result(ctxp->gfm_server))
	    != GFARM_ERR_NO_ERROR)
		gflog_warning("compound_end result: %s",
		    gfarm_error_string(e));

	return (e);
}

gfarm_error_t
gfs_getxmlent(struct gfs_xmlattr_ctx *ctxp, char **fpathp, char **namep)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;
	gfarm_timerval_t t1, t2;
	char *fpath, *p;
	int pathlen, overflow;
	size_t allocsz;

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
			allocsz = gfarm_size_add(&overflow,
				gfarm_size_add(&overflow, pathlen, strlen(fpath)), 2);
			if (!overflow)
				p = realloc(ctxp->workpath, allocsz);
			if (!overflow && (p != NULL)) {
				ctxp->workpath = p;
				if (ctxp->path[pathlen-1] == '/')
					sprintf(ctxp->workpath, "%s%s", ctxp->path, fpath);
				else
					sprintf(ctxp->workpath, "%s/%s", ctxp->path, fpath);
				pathlen = strlen(ctxp->workpath);
				if ((pathlen > 1) && (ctxp->workpath[pathlen-1] == '/'))
					ctxp->workpath[pathlen-1] = '\0';
				*fpathp = ctxp->workpath;
				*namep = ctxp->entries[ctxp->index].attrname;
				ctxp->index++;
			} else
				e = GFARM_ERR_NO_MEMORY;
		}
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return e;
}

gfarm_error_t
gfs_closexmlattr(struct gfs_xmlattr_ctx *ctxp)
{
	gfarm_error_t e;
	gfarm_timerval_t t1, t2;

	GFARM_TIMEVAL_FIX_INITIALIZE_WARNING(t1);
	gfs_profile(gfarm_gettimerval(&t1));

	if (ctxp != NULL) {
		e = gfm_close_fd(ctxp->gfm_server, ctxp->fd);
		gfm_client_connection_free(ctxp->gfm_server);
		gfs_xmlattr_ctx_free(ctxp, 1);
	} else {
		e = GFARM_ERR_NO_ERROR;
	}

	gfs_profile(gfarm_gettimerval(&t2));
	gfs_profile(gfs_xattr_time += gfarm_timerval_sub(&t2, &t1));

	return (e);
}

void
gfs_xattr_display_timers(void)
{
	gflog_info("gfs_xattr      : %g sec", gfs_xattr_time);
}

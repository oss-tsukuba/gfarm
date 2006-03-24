/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gfarm/gfarm.h>

#include "config.h"
#include "metadb_access.h"
#include "metadb_sw.h"

/**********************************************************************/

/*
 * XXX it may be better to initialize this by a table of functions which
 * always report "metadb is not correctly initialized".
 */
static const struct gfarm_metadb_internal_ops *metadb_ops =
#ifdef HAVE_LDAP
	&gfarm_ldap_metadb_ops;
#else
	&gfarm_pgsql_metadb_ops;
#endif

char *
gfarm_metab_use_ldap(void)
{
#ifdef HAVE_LDAP
	metadb_ops = &gfarm_ldap_metadb_ops;
	return (NULL);
#else
	return ("gfarm.conf: ldap is specified, "
	    "but it is not linked into the gfarm library");
#endif
}

char *
gfarm_metab_use_postgresql(void)
{
#ifdef HAVE_POSTGRESQL
	metadb_ops = &gfarm_pgsql_metadb_ops;
	return (NULL);
#else
	return ("gfarm.conf: postgresql is specified, "
	    "but it is not linked into the gfarm library");
#endif
}

static pid_t gfarm_metadb_client_pid = 0;
static int gfarm_metadb_connection_shared = 0;

void
gfarm_metadb_share_connection(void)
{
	gfarm_metadb_connection_shared = 1;
}

char *
gfarm_metadb_initialize(void)
{
	char *e;

	gfarm_metadb_client_pid = 0;
	e = (*metadb_ops->initialize)();
	if (e != NULL)
		return (e);
	gfarm_metadb_client_pid = getpid();
	return (NULL);
}

char *
gfarm_metadb_terminate(void)
{
	char *e = (*metadb_ops->terminate)();

	gfarm_metadb_client_pid = 0;
	return (e);
}

int
gfarm_does_own_metadb_connection(void)
{
	return (gfarm_metadb_connection_shared ||
	    getpid() == gfarm_metadb_client_pid);
}

/**********************************************************************/

static void
gfarm_base_generic_info_free_all(
	int n,
	void *vinfos,
	const struct gfarm_base_generic_info_ops *ops)
{
	int i;
	char *infos = vinfos;

	for (i = 0; i < n; i++) {
		ops->free(infos);
		infos += ops->info_size;
	}
	free(vinfos);
}

/**********************************************************************/

static void gfarm_base_host_info_clear(void *info);
static int gfarm_base_host_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_host_info_ops = {
	sizeof(struct gfarm_host_info),
	(void (*)(void *))gfarm_metadb_host_info_free,
	gfarm_base_host_info_clear,
	gfarm_base_host_info_validate,
};

static void
gfarm_base_host_info_clear(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	memset(info, 0, sizeof(*info));
#if 0
	info->ncpu = GFARM_HOST_INFO_NCPU_NOT_SET;
#else
	info->ncpu = 1; /* assume 1 CPU by default */
#endif
}

static int
gfarm_base_host_info_validate(void *vinfo)
{
	struct gfarm_host_info *info = vinfo;

	/* info->hostaliases may be NULL */
	return (
	    info->hostname != NULL &&
	    info->architecture != NULL &&
	    info->ncpu != GFARM_HOST_INFO_NCPU_NOT_SET
	);
}

void
gfarm_metadb_host_info_free(
	struct gfarm_host_info *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
	if (info->hostaliases != NULL) {
		gfarm_strarray_free(info->hostaliases);
		info->nhostaliases = 0;
	}
	if (info->architecture != NULL)
		free(info->architecture);
	/*
	 * if implementation of this function is changed,
	 * implementation of gfarm_*_host_info_get_architecture_by_host()
	 * should be changed, too.
	 */
}

char *
gfarm_metadb_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	return ((*metadb_ops->host_info_get)(hostname, info));
}

char *
gfarm_metadb_host_info_remove_hostaliases(const char *hostname)
{
	return ((*metadb_ops->host_info_remove_hostaliases)(hostname));
}

char *
gfarm_metadb_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	return ((*metadb_ops->host_info_set)(hostname, info));
}

char *
gfarm_metadb_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	return ((*metadb_ops->host_info_replace)(hostname, info));
}

char *
gfarm_metadb_host_info_remove(const char *hostname)
{
	return ((*metadb_ops->host_info_remove)(hostname));
}

void
gfarm_metadb_host_info_free_all(
	int n,
	struct gfarm_host_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_host_info_ops);
}

char *
gfarm_metadb_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	return ((*metadb_ops->host_info_get_all)(np, infosp));
}

char *
gfarm_metadb_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	return ((*metadb_ops->host_info_get_by_name_alias)(name_alias, info));
}

char *
gfarm_metadb_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	return ((*metadb_ops->host_info_get_allhost_by_architecture)(
	    architecture, np, infosp));
}

/**********************************************************************/

static void gfarm_base_path_info_clear(void *info);
static int gfarm_base_path_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_path_info_ops = {
	sizeof(struct gfarm_path_info),
	(void (*)(void *))gfarm_path_info_free,
	gfarm_base_path_info_clear,
	gfarm_base_path_info_validate,
};

static void
gfarm_base_path_info_clear(void *vinfo)
{
	struct gfarm_path_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_path_info_validate(void *vinfo)
{
	struct gfarm_path_info *info = vinfo;

	/* XXX - should check all fields are filled */
	return (
	    info->pathname != NULL &&
	    info->status.st_user != NULL &&
	    info->status.st_group != NULL
	);
}

void
gfarm_path_info_free(
	struct gfarm_path_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->status.st_user != NULL)
		free(info->status.st_user);
	if (info->status.st_group != NULL)
		free(info->status.st_group);
}

char *
gfarm_metadb_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	return ((*metadb_ops->path_info_get)(pathname, info));
}


char *
gfarm_metadb_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	return ((*metadb_ops->path_info_set)(pathname, info));
}

char *
gfarm_metadb_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	return ((*metadb_ops->path_info_replace)(pathname, info));
}

char *
gfarm_metadb_path_info_remove(const char *pathname)
{
	return ((*metadb_ops->path_info_remove)(pathname));
}

/* XXX - remove this? since currently this interface isn't actually used. */
void
gfarm_path_info_free_all(
	int n,
	struct gfarm_path_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_path_info_ops);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
char *
gfarm_metadb_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	return ((*metadb_ops->path_info_get_all_foreach)(callback, closure));
}

#if 0 /* GFarmFile history isn't actually used yet */

void
gfarm_file_history_free_allfile(int n, char **v)
{
	gfarm_path_info_free_all(n, (struct gfarm_path_info *)v);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/

static void gfarm_base_file_section_info_clear(void *info);
static int gfarm_base_file_section_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_file_section_info_ops = {
	sizeof(struct gfarm_file_section_info),
	(void (*)(void *))gfarm_file_section_info_free,
	gfarm_base_file_section_info_clear,
	gfarm_base_file_section_info_validate,
};

static void
gfarm_base_file_section_info_clear(void *vinfo)
{
	struct gfarm_file_section_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_file_section_info_validate(void *vinfo)
{
	struct gfarm_file_section_info *info = vinfo;

	/* XXX - should check all fields are filled */
	return (
	    info->section != NULL
	);
}

void
gfarm_file_section_info_free(struct gfarm_file_section_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->section != NULL)
		free(info->section);
	if (info->checksum_type != NULL)
		free(info->checksum_type);
	if (info->checksum != NULL)
		free(info->checksum);
}

char *
gfarm_metadb_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	return ((*metadb_ops->file_section_info_get)(pathname, section, info));
}

char *
gfarm_metadb_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return ((*metadb_ops->file_section_info_set)(pathname, section, info));
}

char *
gfarm_metadb_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return ((*metadb_ops->file_section_info_replace)(pathname, section,
	    info));
}

char *
gfarm_metadb_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	return ((*metadb_ops->file_section_info_remove)(pathname, section));
}

void
gfarm_file_section_info_free_all(
	int n,
	struct gfarm_file_section_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_file_section_info_ops);
}

char *
gfarm_metadb_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	return ((*metadb_ops->file_section_info_get_all_by_file)(pathname,
	    np, infosp));
}

/**********************************************************************/

static void gfarm_base_file_section_copy_info_clear(void *info);
static int gfarm_base_file_section_copy_info_validate(void *info);

const struct gfarm_base_generic_info_ops
	gfarm_base_file_section_copy_info_ops =
{
	sizeof(struct gfarm_file_section_copy_info),
	(void (*)(void *))gfarm_file_section_copy_info_free,
	gfarm_base_file_section_copy_info_clear,
	gfarm_base_file_section_copy_info_validate,
};

static void
gfarm_base_file_section_copy_info_clear(void *vinfo)
{
	struct gfarm_file_section_copy_info *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_file_section_copy_info_validate(void *vinfo)
{
	struct gfarm_file_section_copy_info *info = vinfo;

	return (
	    info->pathname != NULL &&
	    info->section != NULL &&
	    info->hostname != NULL
	);
}

void
gfarm_file_section_copy_info_free(
	struct gfarm_file_section_copy_info *info)
{
	if (info->pathname != NULL)
		free(info->pathname);
	if (info->section != NULL)
		free(info->section);
	if (info->hostname != NULL)
		free(info->hostname);
}

char *
gfarm_metadb_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return ((*metadb_ops->file_section_copy_info_get)(
	    pathname, section, hostname, info));
}

char *
gfarm_metadb_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return ((*metadb_ops->file_section_copy_info_set)(
	    pathname, section, hostname, info));
}

char *
gfarm_metadb_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	return ((*metadb_ops->file_section_copy_info_remove)(
	    pathname, section, hostname));
}

void
gfarm_file_section_copy_info_free_all(
	int n,
	struct gfarm_file_section_copy_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_file_section_copy_info_ops);
}

char *
gfarm_metadb_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return ((*metadb_ops->file_section_copy_info_get_all_by_file)(
	    pathname, np, infosp));
}

char *
gfarm_metadb_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return ((*metadb_ops->file_section_copy_info_get_all_by_section)(
	    pathname, section, np, infosp));
}

char *
gfarm_metadb_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return ((*metadb_ops->file_section_copy_info_get_all_by_host)(
	    hostname, np, infosp));
}

/**********************************************************************/

#if 0 /* GFarmFile history isn't actually used yet */

static void gfarm_base_file_history_clear(void *info);
static int gfarm_base_file_history_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_file_history_ops = {
	sizeof(struct gfarm_file_history),
	(void (*)(void *))gfarm_file_history_free,
	gfarm_base_file_history_clear,
	gfarm_base_file_history_validate,
};

static void
gfarm_base_file_history_clear(void *vinfo)
{
	struct gfarm_file_history *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
gfarm_base_file_history_validate(void *vinfo)
{
	struct gfarm_file_history *info = vinfo;

	return (
	    info->program != NULL &&
	    info->input_files != NULL &&
	    info->parameter != NULL
	);
}

void
gfarm_file_history_free(
	struct gfarm_file_history *info)
{
	if (info->program != NULL)
		free(info->program);
	if (info->input_files != NULL)
		gfarm_strarray_free(info->input_files);
	if (info->parameter != NULL)
		free(info->parameter);
}

#endif /* GFarmFile history isn't actually used yet */

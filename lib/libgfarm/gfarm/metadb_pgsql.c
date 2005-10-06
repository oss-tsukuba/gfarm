/*
 * $Id$
 */

#include <stdlib.h>
#include <gfarm/gfarm.h>

#include "config.h"
#include "metadb_access.h"
#include "metadb_sw.h"

/**********************************************************************/

static char *
gfarm_pgsql_initialize(void)
{
#if 1
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
#else
	char *e;
	e = do open;
	if (e != NULL) {
		(void)gfarm_metadb_terminate();
		return (e);
	}

	return (NULL);
#endif
}

static char *
gfarm_pgsql_terminate(void)
{
#if 1
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
#else
	char *e;

	/* close and free connection resources */
	if (gfarm_does_own_metadb_connection()) {
		e = do close;
	}

	return (e);
#endif
}

/**********************************************************************/


static char *
gfarm_pgsql_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_remove_hostaliases(const char *hostname)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_set(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_replace(
	char *hostname,
	struct gfarm_host_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_remove(const char *hostname)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

static char *
gfarm_pgsql_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_set(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_replace(
	char *pathname,
	struct gfarm_path_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_path_info_remove(const char *pathname)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_pgsql_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#if 0 /* GFarmFile history isn't actually used yet */

/* get GFarmFiles which were created by the program */
static char *
gfarm_pgsql_file_history_get_allfile_by_program(
	char *program,
	int *np,
	char ***gfarm_files_p)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/* get GFarmFiles which were created from the file as a input */
static char *
gfarm_pgsql_file_history_get_allfile_by_file(
	char *input_gfarm_file,
	int *np,
	char ***gfarm_files_p)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/


static char *
gfarm_pgsql_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_set(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_replace(
	char *pathname,
	char *section,
	struct gfarm_file_section_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

static char *
gfarm_pgsql_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_set(
	char *pathname,
	char *section,
	char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

/**********************************************************************/

#if 0 /* GFarmFile history isn't actually used yet */

static char *
gfarm_pgsql_file_history_get(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_history_set(
	char *gfarm_file,
	struct gfarm_file_history *info)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

static char *
gfarm_pgsql_file_history_remove(char *gfarm_file)
{
	return (GFARM_ERR_FUNCTION_NOT_IMPLEMENTED);
}

#endif /* GFarmFile history isn't actually used yet */

/**********************************************************************/

const struct gfarm_metadb_internal_ops gfarm_pgsql_metadb_ops = {
	gfarm_pgsql_initialize,
	gfarm_pgsql_terminate,

	gfarm_pgsql_host_info_get,
	gfarm_pgsql_host_info_remove_hostaliases,
	gfarm_pgsql_host_info_set,
	gfarm_pgsql_host_info_replace,
	gfarm_pgsql_host_info_remove,
	gfarm_pgsql_host_info_get_all,
	gfarm_pgsql_host_info_get_by_name_alias,
	gfarm_pgsql_host_info_get_allhost_by_architecture,

	gfarm_pgsql_path_info_get,
	gfarm_pgsql_path_info_set,
	gfarm_pgsql_path_info_replace,
	gfarm_pgsql_path_info_remove,
	gfarm_pgsql_path_info_get_all_foreach,

	gfarm_pgsql_file_section_info_get,
	gfarm_pgsql_file_section_info_set,
	gfarm_pgsql_file_section_info_replace,
	gfarm_pgsql_file_section_info_remove,
	gfarm_pgsql_file_section_info_get_all_by_file,

	gfarm_pgsql_file_section_copy_info_get,
	gfarm_pgsql_file_section_copy_info_set,
	gfarm_pgsql_file_section_copy_info_remove,
	gfarm_pgsql_file_section_copy_info_get_all_by_file,
	gfarm_pgsql_file_section_copy_info_get_all_by_section,
	gfarm_pgsql_file_section_copy_info_get_all_by_host,
};

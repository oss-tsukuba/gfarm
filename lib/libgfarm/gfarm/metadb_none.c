/*
 * $Id$
 */

#include <sys/types.h>

struct gfarm_host_info;
struct gfarm_path_info;
struct gfarm_file_section_info;
struct gfarm_file_section_copy_info;

#include "gfutil.h"

#include "metadb_sw.h"

char GFARM_ERR_UNKNOWN_METADB_TYPE[] = "metadata server type isn't configured";

static char *
gfarm_none_report_error(void)
{
	static int error_is_reported = 0;

	if (!error_is_reported && gflog_syslog_enabled()) {
		error_is_reported = 1;
		gflog_error("gfarm_metadb: %s", GFARM_ERR_UNKNOWN_METADB_TYPE);
	}
	return (GFARM_ERR_UNKNOWN_METADB_TYPE);
}

static char *
gfarm_none_initialize(void)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_terminate(void)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_get(
	const char *hostname,
	struct gfarm_host_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_remove_hostaliases(const char *hostname)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_set(
	const char *hostname,
	const struct gfarm_host_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_replace(
	const char *hostname,
	const struct gfarm_host_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_remove(const char *hostname)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_get_all(
	int *np,
	struct gfarm_host_info **infosp)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_get_by_name_alias(
	const char *name_alias,
	struct gfarm_host_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_host_info_get_allhost_by_architecture(const char *architecture,
	int *np, struct gfarm_host_info **infosp)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_path_info_get(
	const char *pathname,
	struct gfarm_path_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_path_info_set(
	const char *pathname,
	const struct gfarm_path_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_path_info_replace(
	const char *pathname,
	const struct gfarm_path_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_path_info_remove(const char *pathname)
{
	return (gfarm_none_report_error());
}

/* XXX - this is for a stopgap implementation of gfs_opendir() */
static char *
gfarm_none_path_info_get_all_foreach(
	void (*callback)(void *, struct gfarm_path_info *),
	void *closure)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_info_get(
	const char *pathname,
	const char *section,
	struct gfarm_file_section_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_info_set(
	const char *pathname,
	const char *section,
	const struct gfarm_file_section_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_info_replace(
	const char *pathname,
	const char *section,
	const struct gfarm_file_section_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_info_remove(
	const char *pathname,
	const char *section)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_info **infosp)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_get(
	const char *pathname,
	const char *section,
	const char *hostname,
	struct gfarm_file_section_copy_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_set(
	const char *pathname,
	const char *section,
	const char *hostname,
	const struct gfarm_file_section_copy_info *info)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_remove(
	const char *pathname,
	const char *section,
	const char *hostname)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_get_all_by_file(
	const char *pathname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_get_all_by_section(
	const char *pathname,
	const char *section,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (gfarm_none_report_error());
}

static char *
gfarm_none_file_section_copy_info_get_all_by_host(
	const char *hostname,
	int *np,
	struct gfarm_file_section_copy_info **infosp)
{
	return (gfarm_none_report_error());
}

/**********************************************************************/

const struct gfarm_metadb_internal_ops gfarm_none_metadb_ops = {
	gfarm_none_initialize,
	gfarm_none_terminate,

	gfarm_none_host_info_get,
	gfarm_none_host_info_remove_hostaliases,
	gfarm_none_host_info_set,
	gfarm_none_host_info_replace,
	gfarm_none_host_info_remove,
	gfarm_none_host_info_get_all,
	gfarm_none_host_info_get_by_name_alias,
	gfarm_none_host_info_get_allhost_by_architecture,

	gfarm_none_path_info_get,
	gfarm_none_path_info_set,
	gfarm_none_path_info_replace,
	gfarm_none_path_info_remove,
	gfarm_none_path_info_get_all_foreach,

	gfarm_none_file_section_info_get,
	gfarm_none_file_section_info_set,
	gfarm_none_file_section_info_replace,
	gfarm_none_file_section_info_remove,
	gfarm_none_file_section_info_get_all_by_file,

	gfarm_none_file_section_copy_info_get,
	gfarm_none_file_section_copy_info_set,
	gfarm_none_file_section_copy_info_remove,
	gfarm_none_file_section_copy_info_get_all_by_file,
	gfarm_none_file_section_copy_info_get_all_by_section,
	gfarm_none_file_section_copy_info_get_all_by_host,
};

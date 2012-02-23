/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>

#include <gfarm/error.h>
#include <gfarm/gflog.h>
#include <gfarm/gfarm_misc.h>

#include <gfarm/host_info.h>
#include <gfarm/group_info.h>
#include <gfarm/user_info.h>
#include <gfarm/gfs.h>

#include "metadb_common.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "metadb_server.h"

#include "internal_host_info.h"

/**********************************************************************/

static void gfarm_base_internal_host_info_clear(void *info);
static int gfarm_base_internal_host_info_validate(void *info);

const struct gfarm_base_generic_info_ops gfarm_base_internal_host_info_ops = {
	sizeof(struct gfarm_internal_host_info),
	(void (*)(void *))gfarm_internal_host_info_free,
	gfarm_base_internal_host_info_clear,
	gfarm_base_internal_host_info_validate,
};

void
gfarm_internal_host_info_free(struct gfarm_internal_host_info *i_info)
{
	struct gfarm_host_info *info = &i_info->hi;

	if (i_info->fsngroupname != NULL)
		free(i_info->fsngroupname);
	gfarm_host_info_free(info);
}

static void
gfarm_base_internal_host_info_clear(void *vinfo)
{
	struct gfarm_internal_host_info *i_info = vinfo;
	struct gfarm_host_info *info = &i_info->hi;

	memset((void *)i_info, 0, sizeof(*i_info));
#if 0
	info->ncpu = GFARM_HOST_INFO_NCPU_NOT_SET;
#else
	info->ncpu = 1; /* assume 1 CPU by default */
#endif
}

static int
gfarm_base_internal_host_info_validate(void *vinfo)
{
	struct gfarm_internal_host_info *i_info = vinfo;
	struct gfarm_host_info *info = &i_info->hi;

	/* info->hostaliases may be NULL */
	return (
	    info->hostname != NULL &&
	    info->architecture != NULL &&
	    info->ncpu != GFARM_HOST_INFO_NCPU_NOT_SET
	);
}

void
gfarm_internal_host_info_free_all(
	int n,
	struct gfarm_internal_host_info *infos)
{
	gfarm_base_generic_info_free_all(n, infos,
	    &gfarm_base_internal_host_info_ops);
}

void
gfarm_internal_host_info_free_except_hostname(
    struct gfarm_internal_host_info *i_info)
{
	struct gfarm_host_info *info = &i_info->hi;

	if (i_info->fsngroupname != NULL)
		free(i_info->fsngroupname);
	gfarm_host_info_free_except_hostname(info);
}

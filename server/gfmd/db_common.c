/*
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gfarm/gfarm.h>

#include "metadb_common.h"

#include "db_access.h"
#include "db_ops.h"
#include "db_common.h"


/**********************************************************************/

static void db_inode_cksum_arg_free(struct db_inode_cksum_arg *info);
static void db_base_inode_cksum_arg_clear(void *info);
static int db_base_inode_cksum_arg_validate(void *info);

const struct gfarm_base_generic_info_ops db_base_inode_cksum_arg_ops = {
	sizeof(struct db_inode_cksum_arg),
	(void (*)(void *))db_inode_cksum_arg_free,
	db_base_inode_cksum_arg_clear,
	db_base_inode_cksum_arg_validate,
};

static void
db_inode_cksum_arg_free(struct db_inode_cksum_arg *info)
{
	if (info->type != NULL)
		free(info->type);
	if (info->sum != NULL)
		free(info->sum);
}

static void
db_base_inode_cksum_arg_clear(void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_base_inode_cksum_arg_validate(void *vinfo)
{
	struct db_inode_cksum_arg *info = vinfo;

	return (
	    info->type != NULL &&
	    info->sum != NULL
	);
}

/**********************************************************************/

static void db_filecopy_arg_free(struct db_filecopy_arg *info);
static void db_base_filecopy_arg_clear(void *info);
static int db_base_filecopy_arg_validate(void *info);

const struct gfarm_base_generic_info_ops db_base_filecopy_arg_ops = {
	sizeof(struct db_filecopy_arg),
	(void (*)(void *))db_filecopy_arg_free,
	db_base_filecopy_arg_clear,
	db_base_filecopy_arg_validate,
};

static void
db_filecopy_arg_free(struct db_filecopy_arg *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
}

static void
db_base_filecopy_arg_clear(void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_base_filecopy_arg_validate(void *vinfo)
{
	struct db_filecopy_arg *info = vinfo;

	return (
	    info->hostname != NULL
	);
}

/**********************************************************************/

static void db_deadfilecopy_arg_free(struct db_deadfilecopy_arg *info);
static void db_base_deadfilecopy_arg_clear(void *info);
static int db_base_deadfilecopy_arg_validate(void *info);

const struct gfarm_base_generic_info_ops db_base_deadfilecopy_arg_ops = {
	sizeof(struct db_deadfilecopy_arg),
	(void (*)(void *))db_deadfilecopy_arg_free,
	db_base_deadfilecopy_arg_clear,
	db_base_deadfilecopy_arg_validate,
};

static void
db_deadfilecopy_arg_free(struct db_deadfilecopy_arg *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
}

static void
db_base_deadfilecopy_arg_clear(void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_base_deadfilecopy_arg_validate(void *vinfo)
{
	struct db_deadfilecopy_arg *info = vinfo;

	return (
	    info->hostname != NULL
	);
}

/**********************************************************************/

static void db_direntry_arg_free(struct db_direntry_arg *info);
static void db_base_direntry_arg_clear(void *info);
static int db_base_direntry_arg_validate(void *info);

const struct gfarm_base_generic_info_ops db_base_direntry_arg_ops = {
	sizeof(struct db_direntry_arg),
	(void (*)(void *))db_direntry_arg_free,
	db_base_direntry_arg_clear,
	db_base_direntry_arg_validate,
};

static void
db_direntry_arg_free(struct db_direntry_arg *info)
{
	if (info->entry_name != NULL)
		free(info->entry_name);
}

static void
db_base_direntry_arg_clear(void *vinfo)
{
	struct db_direntry_arg *info = vinfo;

	memset(info, 0, sizeof(*info));
}

static int
db_base_direntry_arg_validate(void *vinfo)
{
	struct db_direntry_arg *info = vinfo;

	return (
	    info->entry_name != NULL
	);
}

#include <stdlib.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include <gfarm/host_info.h>
#include <gfarm/group_info.h>
#include <gfarm/user_info.h>

void
gfarm_host_info_free(
	struct gfarm_host_info *info)
{
	if (info->hostname != NULL)
		free(info->hostname);
	if (info->hostaliases != NULL)
		gfarm_strarray_free(info->hostaliases);
	if (info->architecture != NULL)
		free(info->architecture);
}

void
gfarm_user_info_free(struct gfarm_user_info *info)
{
	if (info->username != NULL)
		free(info->username);
	if (info->realname != NULL)
		free(info->realname);
	if (info->homedir != NULL)
		free(info->homedir);
	if (info->gsi_dn != NULL)
		free(info->gsi_dn);
}

void
gfarm_group_info_free(struct gfarm_group_info *info)
{
	int i;

	if (info->groupname != NULL)
		free(info->groupname);
	for (i = 0; i < info->nusers; i++)
		free(info->usernames[i]);
}

void
gfarm_group_names_free(struct gfarm_group_names *info)
{
	int i;

	for (i = 0; i < info->ngroups; i++)
		free(info->groupnames[i]);
}


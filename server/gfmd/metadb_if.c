/*
 * This file provides interface declared in "libgfarm/gfarm/metadb_server.h"
 */

#include <stddef.h>

#include <gfarm/error.h>
#include <gfarm/gfarm_misc.h>

#include "metadb_server.h"

#include "user.h"

/* this routine is called from gfarm_authorize() */
gfarm_error_t
gfarm_metadb_verify_username(const char *global_user)
{
	if (user_lookup(global_user) == NULL)
		return (GFARM_ERR_AUTHENTICATION);
	return (GFARM_ERR_NO_ERROR);
}

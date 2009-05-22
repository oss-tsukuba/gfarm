#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <gfarm/gfarm.h>

#include "gfutil.h"

#include "auth.h"
#include "gfm_client.h"

gfarm_error_t
gfarm_auth_uid_to_global_username_gsi(void *closure,
	const char *auth_user_id, char **global_usernamep)
{
	struct gfm_connection *gfm_server = closure;
	gfarm_error_t e;
	char *global_username;
	struct gfarm_user_info ui;

	/* In GSI, auth_user_id is a DN */
	e = gfm_client_user_info_get_by_gsi_dn(gfm_server,
		auth_user_id, &ui);
	if (e != GFARM_ERR_NO_ERROR)
		return (e);
	if (global_usernamep == NULL) {
		gfarm_user_info_free(&ui);
		return (GFARM_ERR_NO_ERROR);
	}
	global_username = strdup(ui.username);
	gfarm_user_info_free(&ui);
	if (global_username == NULL)
		return (GFARM_ERR_NO_MEMORY);
	*global_usernamep = global_username;
	return (GFARM_ERR_NO_ERROR);
}

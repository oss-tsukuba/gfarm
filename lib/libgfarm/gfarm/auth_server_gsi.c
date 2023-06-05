#include <stdlib.h>

#include <gssapi.h>

#include <gfarm/gfarm.h>

#include "gfsl_secure_session.h"
#include "gss.h"

#include "auth.h"
#include "auth_gss.h"
#include "gfarm_gss.h"

/*
 * server side authentication
 */

int
gfarm_auth_server_method_is_gsi_available(void)
{
	return (gfarm_gss_gsi() != NULL);
}

/*
 * "gsi" method
 */

gfarm_error_t
gfarm_authorize_gsi(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_gsi();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss(conn, gss,
	    service_tag, hostname, 0, GFARM_AUTH_METHOD_GSI,
	    auth_uid_to_global_user, closure,
	    peer_rolep, global_usernamep));
}

/*
 * "gsi_auth" method
 */

gfarm_error_t
gfarm_authorize_gsi_auth(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_gsi();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss_auth(conn, gss,
	    service_tag, hostname, 0, GFARM_AUTH_METHOD_GSI_AUTH,
	    auth_uid_to_global_user, closure,
	    peer_rolep, global_usernamep));
}

#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "auth.h"
#include "gfarm_gss.h"

/*
 * server side authentication
 */
/*
 * "kerberos" method
 */

gfarm_error_t
gfarm_authorize_kerberos(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss(conn, gss,
	    service_tag, hostname, 1, GFARM_AUTH_METHOD_KERBEROS,
	    auth_uid_to_global_user, closure,
	    peer_rolep, global_usernamep));
}

/*
 * "kerberos_auth" method
 */

gfarm_error_t
gfarm_authorize_kerberos_auth(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, const char *,
	    enum gfarm_auth_id_role *, char **), void *closure,
	enum gfarm_auth_id_role *peer_rolep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss_auth(conn, gss,
	    service_tag, hostname, 1, GFARM_AUTH_METHOD_KERBEROS_AUTH,
	    auth_uid_to_global_user, closure,
	    peer_rolep, global_usernamep));
}

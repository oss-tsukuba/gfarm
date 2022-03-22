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
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss(conn, gss,
	    service_tag, hostname, GFARM_AUTH_METHOD_KERBEROS,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep));
}

/*
 * "kerberos_auth" method
 */

gfarm_error_t
gfarm_authorize_kerberos_auth(struct gfp_xdr *conn,
	char *service_tag, char *hostname,
	gfarm_error_t (*auth_uid_to_global_user)(void *,
	    enum gfarm_auth_method, enum gfarm_auth_id_type, const char *,
	    char **), void *closure,
	enum gfarm_auth_id_type *peer_typep, char **global_usernamep)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);

	return (gfarm_authorize_gss_auth(conn, gss,
	    service_tag, hostname, GFARM_AUTH_METHOD_KERBEROS_AUTH,
	    auth_uid_to_global_user, closure,
	    peer_typep, global_usernamep));
}

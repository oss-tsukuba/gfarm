#include <stdlib.h>

#include <gfarm/gfarm.h>

#include "auth.h"
#include "gfarm_gss.h"

gfarm_error_t
gfarm_auth_request_kerberos(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_role self_role, const char *user,
	struct passwd *pwd)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);
	return (gfarm_auth_request_gss(conn, gss, service_tag, hostname,
	    1, self_role, user, pwd));
}

/*
 * multiplexed version of gfarm_auth_request_kerberos()
 * for parallel authentication
 */

gfarm_error_t
gfarm_auth_request_kerberos_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_role self_role, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);
	return (gfarm_auth_request_gss_multiplexed(q, conn, gss, service_tag,
	    hostname, 1, self_role, user, pwd, auth_timeout,
	    continuation, closure, statepp));
}

gfarm_error_t
gfarm_auth_result_kerberos_multiplexed(void *sp)
{
	return (gfarm_auth_result_gss_multiplexed(sp));
}

/*
 * "kerberos_auth" method
 */

gfarm_error_t
gfarm_auth_request_kerberos_auth(struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_role self_role, const char *user,
	struct passwd *pwd)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);
	return (gfarm_auth_request_gss_auth(conn, gss, service_tag, hostname,
	    1, self_role, user, pwd));
}

gfarm_error_t
gfarm_auth_request_kerberos_auth_multiplexed(struct gfarm_eventqueue *q,
	struct gfp_xdr *conn,
	const char *service_tag, const char *hostname,
	enum gfarm_auth_id_role self_role, const char *user,
	struct passwd *pwd, int auth_timeout,
	void (*continuation)(void *), void *closure,
	void **statepp)
{
	struct gfarm_gss *gss = gfarm_gss_kerberos();

	if (gss == NULL)
		return (GFARM_ERR_INTERNAL_ERROR);
	return (gfarm_auth_request_gss_auth_multiplexed(q, conn, gss,
	    service_tag, hostname, 1, self_role, user, pwd, auth_timeout,
	    continuation, closure, statepp));
}

gfarm_error_t
gfarm_auth_result_kerberos_auth_multiplexed(void *sp)
{
	return (gfarm_auth_result_gss_auth_multiplexed(sp));
}

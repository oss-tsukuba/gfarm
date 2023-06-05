#include <stdlib.h>

#include <gfarm/gfarm.h>

#ifndef HAVE_GSI

/* this is a public function exported by <gfarm/gfarm_misc.h> */
gfarm_error_t
gfarm_auth_method_gsi_available(void)
{
	return (GFARM_ERR_PROTOCOL_NOT_SUPPORTED);
}

#else /* HAVE_GSI */

#include <gssapi.h>

#include "gfsl_secure_session.h"
#include "gss.h"

#include "context.h"
#include "auth.h"
#include "auth_gss.h"
#include "gfarm_gss.h"

/*
 * XXX - thread-unsafe interface.
 *
 * these functions assume a single thread server
 * like gfsd and gfarm_gridftp_dsi.  this is not for gfmd.
 *
 * or, caller should protect the resource appropriately.
 */

/*
 * this is a public function exported by <gfarm/gfarm_misc.h>
 *
 * this is similar to gfarm_auth_client_method_is_gsi_available(),
 * except the client_cred_failed check
 */
gfarm_error_t
gfarm_auth_method_gsi_available(void)
{
	return (gfarm_auth_client_method_gss_protocol_available(
	    gfarm_gss_gsi(), gfarm_ctxp->auth_common_gsi_static));
}

/*
 * Delegated credential
 */

/* gfarm-gridftp-dsi since v1.0.5 is using this interface */
void
gfarm_gsi_client_cred_set(gss_cred_id_t cred)
{
	gfarm_auth_gss_client_cred_set(
	    gfarm_ctxp->auth_common_gsi_static, cred);
}

gss_cred_id_t
gfarm_gsi_client_cred_get(void)
{
	return (gfarm_auth_gss_client_cred_get(
	    gfarm_ctxp->auth_common_gsi_static));
}

/*
 * deprecated. gfarm_gsi_set_delegated_cred() will be removed in future.
 * (gfarm-gridftp-dsi was using this until v1.0.4)
 */
void
gfarm_gsi_set_delegated_cred(gss_cred_id_t cred)
{
	gfarm_gsi_client_cred_set(cred);
}

char *
gfarm_gsi_client_cred_name(void)
{
	struct gfarm_gss *gss = gfarm_gss_gsi();

	if (gss == NULL)
		return (NULL);
	return (gfarm_gss_client_cred_name(gss,
	    gfarm_ctxp->auth_common_gsi_static));
}

/*
 * end of thread-unsafe interface
 */

static void
gfarm_auth_gsi_client_cred_set_failed(void)
{
	struct gfarm_gss *gss = gfarm_gss_gsi();

	assert(gss != NULL);
	gfarm_auth_gss_client_cred_set_failed(gss,
	    gfarm_ctxp->auth_common_gsi_static);
}

static gfarm_error_t
gfarm_auth_gsi_client_cred_check_failed(void)
{
	struct gfarm_gss *gss = gfarm_gss_gsi();

	assert(gss != NULL);
	return (gfarm_auth_gss_client_cred_check_failed(gss,
	    gfarm_ctxp->auth_common_gsi_static));
}

static struct gfarm_auth_gss_ops gfarm_auth_gsi_ops = {
	gfarm_gsi_client_cred_get,
	gfarm_auth_gsi_client_cred_set_failed,
	gfarm_auth_gsi_client_cred_check_failed,
};

gfarm_error_t
gfarm_auth_common_gsi_static_init(struct gfarm_context *ctxp)
{
	static const char diag[] = "gfarm_auth_common_gsi_static_init";

	return (gfarm_auth_common_gss_static_init(
	    &ctxp->auth_common_gsi_static, diag));
}

void
gfarm_auth_common_gsi_static_term(struct gfarm_context *ctxp)
{
	static const char diag[] = "gfarm_auth_common_gsi_static_term";

	gfarm_auth_common_gss_static_term(ctxp->auth_common_gsi_static, diag);
}

static struct gfarm_gss *libgfsl_gsi;

static void
libgfsl_gsi_initialize(void)
{
	libgfsl_gsi = gfarm_gss_dlopen(LIBGFSL_GSI, "gsi",
	    &gfarm_auth_gsi_ops);
}


struct gfarm_gss *
gfarm_gss_gsi(void)
{
	static pthread_once_t initialized = PTHREAD_ONCE_INIT;

	pthread_once(&initialized, libgfsl_gsi_initialize);
	return (libgfsl_gsi);
}

#endif /* HAVE_GSI */



#include "tlsutils.h"

SSL_CTX *own_sslctx = NULL;

/* GFARM_MSG_UNFIXED */
static gfarm_error_t
tls_init_context(void)
{
	return(GFARM_ERR_NO_ERROR);
}

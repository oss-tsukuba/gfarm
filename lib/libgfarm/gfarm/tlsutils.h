#pragma once

#ifdef HAVE_TLS_1_3



/*
 * Just temp.
 */
#define GFP_XDR_TLS_ROLE    1
#define GFP_XDR_TLS_ACCEPT    0
#define GFP_XDR_TLS_INITIATE    1
#define GFP_XDR_TLS_CLIENT_AUTHENTICATION    2
#define GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)    \
	(((flags) & GFP_XDR_TLS_ROLE) == GFP_XDR_TLS_INITIATE)

gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn, int fd,
	int flags, char *service, char *name);
void
gfp_xdr_tls_reset(struct gfp_xdr *conn);
char *
gfp_xdr_tls_initiator_dn(struct gfp_xdr *conn);



#else

extern const bool tls_not_used;



#endif /* HAVE_TLS_1_3 */

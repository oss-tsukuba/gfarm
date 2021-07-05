/*
 * gfp_xdr_tls_alloc() flags:
 */
#define GFP_XDR_TLS_ROLE_IS_INITIATOR(flags)	\
	(((flags) & GFP_XDR_TLS_ROLE) == GFP_XDR_TLS_INITIATE)
#define GFP_XDR_TLS_ACCEPT			0
#define GFP_XDR_TLS_INITIATE			1
#define GFP_XDR_TLS_ROLE			1

#define GFP_XDR_TLS_CLIENT_AUTHENTICATION	2 /* tls_client_certificate */
#define GFP_XDR_TLS_CLIENT_USE_GSI_PROXY_CERTIFICATE \
						4 
						/* tls_gsi_proxy_certificate */

gfarm_error_t gfp_xdr_tls_alloc(struct gfp_xdr *, int, int);
	/* gfp_xdr_tls_alloc(conn, fd, flags) */

void gfp_xdr_tls_reset(struct gfp_xdr *);

char *gfp_xdr_tls_peer_dn_rfc2253(struct gfp_xdr *);
char *gfp_xdr_tls_peer_dn_gsi(struct gfp_xdr *);
char *gfp_xdr_tls_peer_dn_common_name(struct gfp_xdr *);

int gfp_xdr_tls_is_readable(struct gfp_xdr *);

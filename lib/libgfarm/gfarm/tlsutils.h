#pragma once

#ifdef HAVE_TLS_1_3



gfarm_error_t
gfp_xdr_tls_alloc(struct gfp_xdr *conn, int fd,
	int flags, char *service, char *name);
void
gfp_xdr_tls_reset(struct gfp_xdr *conn);
char *
gfp_xdr_tls_initiator_dn(struct gfp_xdr *conn);



#else

extern const int tls_not_used;



#endif /* HAVE_TLS_1_3 */

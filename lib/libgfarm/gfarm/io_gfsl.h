struct gfp_xdr;
struct gfarmSecSession;
struct gfarm_iobuffer;

gfarm_error_t gfp_xdr_set_secsession(struct gfp_xdr *,
	struct gfarmSecSession *, gss_cred_id_t);
void gfp_xdr_reset_secsession(struct gfp_xdr *);
void gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *,
	void *, int);

void gfp_xdr_downgrade_to_insecure_session(struct gfp_xdr *);

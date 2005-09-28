struct xxx_connection;
struct gfarmSecSession;
struct gfarm_iobuffer;

char *xxx_connection_set_secsession(struct xxx_connection *,
	struct gfarmSecSession *, gss_cred_id_t);
void xxx_connection_reset_secsession(struct xxx_connection *);
void gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *,
	void *, int);

void xxx_connection_downgrade_to_insecure_session(struct xxx_connection *);

struct xxx_connection;
struct gfarmSecSession;
struct gfarm_iobuffer;

char *gfarm_gsi_initialize(void);
char *gfarm_auth_request_gsi(struct xxx_connection *);
char *gfarm_authorize_gsi(struct xxx_connection *, int, char **);

char *xxx_connection_set_secsession(struct xxx_connection *,
	struct gfarmSecSession *);
void xxx_connection_reset_secsession(struct xxx_connection *);
void gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *,
	void *, int);

struct xxx_connection;
struct gfarmSecSession;
struct gfarm_iobuffer;

char *gfarm_gsi_initialize(void);

char *xxx_connection_set_secsession(struct xxx_connection *,
	struct gfarmSecSession *);
void xxx_connection_reset_secsession(struct xxx_connection *);
void gfarm_iobuffer_write_close_secsession_op(struct gfarm_iobuffer *,
	void *, int);

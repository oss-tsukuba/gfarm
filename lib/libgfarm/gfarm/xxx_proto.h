/*
 * private definition. these functions may be merged to nslib.
 */

struct gfarm_iobuffer;

struct xxx_iobuffer_ops {
	char *(*close)(void *, int);
	char *(*export_credential)(void *);
	char *(*delete_credential)(void *);
	char *(*env_for_credential)(void *);
	int (*nonblocking_read)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*nonblocking_write)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*blocking_read)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
	int (*blocking_write)(struct gfarm_iobuffer *, void *, int,
	    void *, int);
};

struct xxx_connection;

char *xxx_connection_new(struct xxx_iobuffer_ops *, void *, int,
	struct xxx_connection **);
char *xxx_connection_free(struct xxx_connection *);

void *xxx_connection_cookie(struct xxx_connection *);
int xxx_connection_fd(struct xxx_connection *);
void xxx_connection_set(struct xxx_connection *,
	struct xxx_iobuffer_ops *, void *, int);

char *xxx_connection_export_credential(struct xxx_connection *);
char *xxx_connection_delete_credential(struct xxx_connection *);
char *xxx_connection_env_for_credential(struct xxx_connection *);

void gfarm_iobuffer_set_nonblocking_read_xxx(struct gfarm_iobuffer *,
	struct xxx_connection *);
void gfarm_iobuffer_set_nonblocking_write_xxx(struct gfarm_iobuffer *,
	struct xxx_connection *);

char *xxx_proto_flush(struct xxx_connection *);
char *xxx_proto_purge(struct xxx_connection *, int, int);
char *xxx_proto_vsend(struct xxx_connection *, char **, va_list *);
char *xxx_proto_vrecv(struct xxx_connection *, int, int *, char **, va_list *);
char *xxx_proto_send(struct xxx_connection *, char *, ...);
char *xxx_proto_recv(struct xxx_connection *, int, int *, char *, ...);
char *xxx_proto_vrpc_request(struct xxx_connection *, gfarm_int32_t,
			     char **, va_list *);
char *xxx_proto_vrpc_result(struct xxx_connection *, int, gfarm_int32_t *,
			    char **, va_list *);
char *xxx_proto_vrpc(struct xxx_connection *,
		     int, gfarm_int32_t, gfarm_int32_t *, char **, va_list *);

int xxx_recv_partial(struct xxx_connection *, int, void *, int);
char *xxx_read_direct(struct xxx_connection *, void *, int, int *);
char *xxx_write_direct(struct xxx_connection *, void *, int, int *);

/*
 * rpc format string mnemonic:
 *
 *	c	[u]int8_t
 *	h	[u]int16_t
 *	i	[u]int32_t
 *	o	file_offset_t (on network: int64_t)
 *	s	char * (on network: int32_t, int8_t-array)
 *	b	fixed size buffer
 *		request: size_t, char *
 *		result:  size_t, size_t *, char *
 *		(on network: int32_t, int8_t-array)
 *
 * (all integers are transfered as big endian on network)
 */

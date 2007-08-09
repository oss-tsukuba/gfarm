/*
 * private definition. these functions may be merged to nslib.
 */

struct gfarm_iobuffer;

struct gfp_iobuffer_ops {
	gfarm_error_t (*close)(void *, int);
	gfarm_error_t (*export_credential)(void *);
	gfarm_error_t (*delete_credential)(void *, int);
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

struct gfp_xdr;

#define IS_CONNECTION_ERROR(e) \
	((e) == GFARM_ERR_BROKEN_PIPE || (e) == GFARM_ERR_UNEXPECTED_EOF || \
	 (e) == GFARM_ERR_PROTOCOL || \
	 (e) == GFARM_ERR_NETWORK_IS_DOWN || \
	 (e) == GFARM_ERR_NETWORK_IS_UNREACHABLE || \
	 (e) == GFARM_ERR_CONNECTION_ABORTED || \
	 (e) == GFARM_ERR_CONNECTION_RESET_BY_PEER || \
	 (e) == GFARM_ERR_SOCKET_IS_NOT_CONNECTED || \
	 (e) == GFARM_ERR_OPERATION_TIMED_OUT || \
	 (e) == GFARM_ERR_CONNECTION_REFUSED || \
	 (e) == GFARM_ERR_NO_ROUTE_TO_HOST)

gfarm_error_t gfp_xdr_new(struct gfp_iobuffer_ops *, void *, int,
	struct gfp_xdr **);
gfarm_error_t gfp_xdr_free(struct gfp_xdr *);

void *gfp_xdr_cookie(struct gfp_xdr *);
int gfp_xdr_fd(struct gfp_xdr *);
void gfp_xdr_set(struct gfp_xdr *,
	struct gfp_iobuffer_ops *, void *, int);

gfarm_error_t gfp_xdr_export_credential(struct gfp_xdr *);
gfarm_error_t gfp_xdr_delete_credential(struct gfp_xdr *, int);
char *gfp_xdr_env_for_credential(struct gfp_xdr *);

void gfarm_iobuffer_set_nonblocking_read_xxx(struct gfarm_iobuffer *,
	struct gfp_xdr *);
void gfarm_iobuffer_set_nonblocking_write_xxx(struct gfarm_iobuffer *,
	struct gfp_xdr *);

gfarm_error_t gfp_xdr_flush(struct gfp_xdr *);
gfarm_error_t gfp_xdr_purge(struct gfp_xdr *, int, int);
gfarm_error_t gfp_xdr_vsend(struct gfp_xdr *,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrecv(struct gfp_xdr *, int, int *,
	const char **, va_list *);
gfarm_error_t gfp_xdr_send(struct gfp_xdr *, const char *, ...);
gfarm_error_t gfp_xdr_recv(struct gfp_xdr *, int, int *,
	const char *, ...);
gfarm_error_t gfp_xdr_vrpc_request(struct gfp_xdr *, gfarm_int32_t,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc_result(struct gfp_xdr *, int,
	gfarm_int32_t *,
	const char **, va_list *);
gfarm_error_t gfp_xdr_vrpc(struct gfp_xdr *,
	int, gfarm_int32_t, gfarm_int32_t *, const char **, va_list *);

int gfp_xdr_recv_partial(struct gfp_xdr *, int, void *, int);
gfarm_error_t gfp_xdr_read_direct(struct gfp_xdr *, void *, int, int *);
gfarm_error_t gfp_xdr_write_direct(struct gfp_xdr *, void *, int, int *);

/*
 * rpc format string mnemonic:
 *
 *	c	gfarm_[u]int8_t
 *	h	gfarm_[u]int16_t
 *	i	gfarm_[u]int32_t
 *	l	gfarm_[u]int64_t
 *	s	char * (on network: gfarm_int32_t, gfarm_int8_t[])
 *	b	fixed size buffer
 *		request: size_t, char *
 *		result:  size_t, size_t *, char *
 *		(on network: gfarm_int32_t, gfarm_int8_t[])
 *
 * (all integers are transfered as big endian on network)
 */

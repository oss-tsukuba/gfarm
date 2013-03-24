struct gfm_connection;
struct gfp_xdr_context;

/* do not use following type indentifies in other headers */
typedef gfarm_error_t (*gfm_inode_request_op_t)(
	struct gfm_connection*, struct gfp_xdr_context *, void *);
typedef gfarm_error_t (*gfm_name_request_op_t)(struct gfm_connection *,
	struct gfp_xdr_context *, void *, const char *);
typedef gfarm_error_t (*gfm_name2_inode_request_op_t)(struct gfm_connection *,
	struct gfp_xdr_context *, void *, const char *);
typedef gfarm_error_t (*gfm_name2_request_op_t)(struct gfm_connection *,
	struct gfp_xdr_context *, void *,
	const char *, const char *);
typedef gfarm_error_t (*gfm_result_op_t)(struct gfm_connection *,
	struct gfp_xdr_context *, void *);
typedef gfarm_error_t (*gfm_success_op_t)(struct gfm_connection *, void *,
	int, const char *, gfarm_ino_t);
typedef gfarm_error_t (*gfm_name2_success_op_t)(struct gfm_connection *,
	void *);
typedef void (*gfm_cleanup_op_t)(struct gfm_connection *, void *);
typedef int (*gfm_must_be_warned_op_t)(gfarm_error_t, void *);

gfarm_error_t gfarm_url_parse_metadb(const char **,
	struct gfm_connection **);
gfarm_error_t gfarm_get_hostname_by_url(const char *, char **, int *);
gfarm_error_t gfm_client_connection_and_process_acquire_by_path(const char *,
	struct gfm_connection **);
gfarm_error_t gfm_client_connection_and_process_acquire_by_path_follow(
	const char *, struct gfm_connection **);
int gfm_is_mounted(struct gfm_connection *);

gfarm_error_t gfm_inode_success_op_connection_free(struct gfm_connection *,
	void *, int, const char *, gfarm_ino_t);
gfarm_error_t gfm_inode_op_readonly(const char *, int,
	gfm_inode_request_op_t, gfm_result_op_t, gfm_success_op_t,
	gfm_cleanup_op_t, void *);
gfarm_error_t gfm_inode_op_modifiable(const char *, int,
	gfm_inode_request_op_t, gfm_result_op_t, gfm_success_op_t,
	gfm_cleanup_op_t, gfm_must_be_warned_op_t, void *);
gfarm_error_t gfm_inode_op_no_follow_readonly(const char *, int,
	gfm_inode_request_op_t, gfm_result_op_t, gfm_success_op_t,
	gfm_cleanup_op_t, void *);
gfarm_error_t gfm_inode_op_no_follow_modifiable(const char *, int,
	gfm_inode_request_op_t, gfm_result_op_t, gfm_success_op_t,
	gfm_cleanup_op_t, gfm_must_be_warned_op_t, void *);

gfarm_error_t gfm_name_success_op_connection_free(struct gfm_connection *,
	void *, int, const char *, gfarm_ino_t);
gfarm_error_t gfm_name_op_modifiable(const char *, gfarm_error_t,
	gfm_name_request_op_t, gfm_result_op_t, gfm_success_op_t,
	gfm_must_be_warned_op_t, void *);

gfarm_error_t gfm_name2_success_op_connection_free(struct gfm_connection *,
	void *);
gfarm_error_t gfm_name2_op_modifiable(const char *, const char *, int,
	gfm_name2_inode_request_op_t, gfm_name2_request_op_t, gfm_result_op_t,
	gfm_name2_success_op_t, gfm_cleanup_op_t, gfm_must_be_warned_op_t,
	void *);

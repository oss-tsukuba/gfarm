/*
 * $Id$
 */

/*
 * Replication scheduler:
 */
struct inode;
struct host;
void fsngroup_replicate_file(
	struct inode *, struct host *, const char *,
	int, struct host **);

/*
 * Server side RPC stubs:
 */
gfarm_error_t gfm_server_fsngroup_get_all(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_fsngroup_get_by_hostname(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_fsngroup_modify(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

/*
 * $Id$
 */

struct host;
gfarm_error_t fsngroup_get_hosts(const char *, int *, struct host ***);

/*
 * Server side RPC stubs:
 */
gfarm_error_t gfm_server_fsngroup_get_all(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_fsngroup_get_by_hostname(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);
gfarm_error_t gfm_server_fsngroup_modify(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

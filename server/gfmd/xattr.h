/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

gfarm_error_t xattr_inherit(struct inode *, struct inode *,
			    void **, size_t *, void **, size_t *,
			    void **, size_t *, void **, size_t *);

gfarm_error_t gfm_server_setxattr(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int, int);
gfarm_error_t gfm_server_getxattr(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int, int);
gfarm_error_t gfm_server_listxattr(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int, int);
gfarm_error_t gfm_server_removexattr(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int, int);

gfarm_error_t gfm_server_findxmlattr(
	struct peer *, gfp_xdr_xid_t, size_t *, int, int);

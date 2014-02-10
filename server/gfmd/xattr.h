/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

gfarm_error_t xattr_inherit(struct inode *, struct inode *,
			    void **, size_t *, void **, size_t *,
			    void **, size_t *, void **, size_t *);

gfarm_error_t gfm_server_setxattr(struct peer *, int, int, int);
gfarm_error_t gfm_server_getxattr(struct peer *, int, int, int);
gfarm_error_t gfm_server_listxattr(struct peer *, int, int, int);
gfarm_error_t gfm_server_removexattr(struct peer *, int, int, int);

gfarm_error_t gfm_server_findxmlattr(struct peer *, int, int);

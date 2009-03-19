/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

gfarm_error_t gfm_server_setxattr(struct peer *peer, int from_client, int skip, int xmlMode);
gfarm_error_t gfm_server_getxattr(struct peer *peer, int from_client, int skip, int xmlMode);
gfarm_error_t gfm_server_listxattr(struct peer *peer, int from_client, int skip, int xmlMode);
gfarm_error_t gfm_server_removexattr(struct peer *peer, int from_client, int skip, int xmlMode);

gfarm_error_t gfm_server_findxmlattr(struct peer *peer, int from_client, int skip);

void gfm_remove_all_xattrs(struct inode *inode);

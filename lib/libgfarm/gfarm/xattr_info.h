/*
 * Copyright (c) 2009 National Institute of Informatics in Japan.
 * All rights reserved.
 */

struct gfs_foundxattr_entry {
	char *path;
	char *attrname;
};

struct gfm_connection;

struct gfs_xmlattr_ctx {
	/* for request from API */
	char *path;
	char *expr;
	int depth;
	/* reply of open */
	struct gfm_connection *gfm_server;
	int fd;
	int type;
	gfarm_ino_t ino;
	/* for request to gfmd */
	char *cookie_path;
	char *cookie_attrname;
	/* for reply */
	int nalloc, nvalid, index;
	struct gfs_foundxattr_entry *entries;
	int eof;
	char *workpath;
};

struct gfs_xmlattr_ctx *gfs_xmlattr_ctx_alloc(int nentry);
void gfs_xmlattr_ctx_free(struct gfs_xmlattr_ctx *, int);

struct xattr_info {
	gfarm_ino_t inum;
	char *attrname;
	int namelen;
	void *attrvalue;
	int attrsize;
};

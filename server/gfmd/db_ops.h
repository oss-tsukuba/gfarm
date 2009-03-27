/*
 * Copyright (c) 2003-2006 National Institute of Advanced
 * Industrial Science and Technology (AIST).  All rights reserved.
 *
 * Copyright (c) 2006 National Institute of Informatics in Japan,
 * All rights reserved.
 *
 * This file or a portion of this file is licensed under the terms of
 * the NAREGI Public License, found at
 * http://www.naregi.org/download/index.html.
 * If you redistribute this file, with or without modifications, you
 * must include this notice in the file.
 */

/*
 * Metadata access switch for internal implementation
 *
 * $Id$
 */

struct db_host_modify_arg {
	struct gfarm_host_info hi;
	int modflags;
	int add_count; char **add_aliases;
	int del_count; char **del_aliases;
};

struct db_user_modify_arg {
	struct gfarm_user_info ui;
	int modflags;
};

struct db_group_modify_arg {
	struct gfarm_group_info gi;
	int modflags;
	int add_count; char **add_users;
	int del_count; char **del_users;
};

struct db_inode_uint64_modify_arg {
	gfarm_ino_t inum;
	gfarm_uint64_t uint64;
};

struct db_inode_uint32_modify_arg {
	gfarm_ino_t inum;
	gfarm_uint32_t uint32;
};

struct db_inode_string_modify_arg {
	gfarm_ino_t inum;
	char *string;
};

struct db_inode_timespec_modify_arg {
	gfarm_ino_t inum;
	struct gfarm_timespec time;
};

struct db_inode_cksum_arg {
	gfarm_ino_t inum;
	char *type;
	size_t len;
	char *sum;
};

struct db_inode_inum_arg {
	gfarm_ino_t inum;
};

struct db_filecopy_arg {
	gfarm_ino_t inum;
	char *hostname;
};

struct db_deadfilecopy_arg {
	gfarm_ino_t inum;
	gfarm_uint64_t igen;
	char *hostname;
};

struct db_direntry_arg {
	gfarm_ino_t dir_inum, entry_inum;
	char *entry_name;
	int entry_len;
};

struct db_symlink_arg {
	gfarm_ino_t inum;
	char *source_path;
};

struct db_xattr_arg {
	int xmlMode;
	gfarm_ino_t inum;
	char *attrname;
	// to set
	void *value;
	size_t size;
	// to get
	void **valuep;
	size_t *sizep;
};

struct db_xmlattr_find_arg {
	gfarm_ino_t inum;
	const char *expr;
	gfarm_error_t (*foundcallback)(void *,int, void *);
	void *foundcbdata;
};

struct xattr_info;

struct db_ops {
	gfarm_error_t (*initialize)(void);
	gfarm_error_t (*terminate)(void);

	gfarm_error_t (*host_add)(struct gfarm_host_info *);
	gfarm_error_t (*host_modify)(struct db_host_modify_arg *);
	gfarm_error_t (*host_remove)(char *);
	gfarm_error_t (*host_load)(void *,
		void (*)(void *, struct gfarm_host_info *));

	gfarm_error_t (*user_add)(struct gfarm_user_info *);
	gfarm_error_t (*user_modify)(struct db_user_modify_arg *);
	gfarm_error_t (*user_remove)(char *);
	gfarm_error_t (*user_load)(void *,
		void (*)(void *, struct gfarm_user_info *));

	gfarm_error_t (*group_add)(struct gfarm_group_info *);
	gfarm_error_t (*group_modify)(struct db_group_modify_arg *);
	gfarm_error_t (*group_remove)(char *);
	gfarm_error_t (*group_load)(void *,
		void (*)(void *, struct gfarm_group_info *));

	gfarm_error_t (*inode_add)(struct gfs_stat *);
	gfarm_error_t (*inode_modify)(struct gfs_stat *);
	gfarm_error_t (*inode_nlink_modify)(struct db_inode_uint64_modify_arg *);
	gfarm_error_t (*inode_size_modify)(struct db_inode_uint64_modify_arg *);
	gfarm_error_t (*inode_mode_modify)(struct db_inode_uint32_modify_arg *);
	gfarm_error_t (*inode_user_modify)(struct db_inode_string_modify_arg *);
	gfarm_error_t (*inode_group_modify)(struct db_inode_string_modify_arg *);
	gfarm_error_t (*inode_atime_modify)(struct db_inode_timespec_modify_arg *);
	gfarm_error_t (*inode_mtime_modify)(struct db_inode_timespec_modify_arg *);
	gfarm_error_t (*inode_ctime_modify)(struct db_inode_timespec_modify_arg *);
	/* inode_remove: never remove any inode to keep inode->i_gen */
	gfarm_error_t (*inode_load)(void *,
		void (*)(void *, struct gfs_stat *));

	gfarm_error_t (*inode_cksum_add)(struct db_inode_cksum_arg *);
	gfarm_error_t (*inode_cksum_modify)(struct db_inode_cksum_arg *);
	gfarm_error_t (*inode_cksum_remove)(struct db_inode_inum_arg *);
	gfarm_error_t (*inode_cksum_load)(void *,
		void (*)(void *, gfarm_ino_t, char *, size_t, char *));

	gfarm_error_t (*filecopy_add)(struct db_filecopy_arg *);
	gfarm_error_t (*filecopy_remove)(struct db_filecopy_arg *);
	gfarm_error_t (*filecopy_load)(void *,
		void (*)(void *, gfarm_ino_t, char *));

	gfarm_error_t (*deadfilecopy_add)(struct db_deadfilecopy_arg *);
	gfarm_error_t (*deadfilecopy_remove)(struct db_deadfilecopy_arg *);
	gfarm_error_t (*deadfilecopy_load)(void *,
		void (*)(void *, gfarm_ino_t, gfarm_uint64_t, char *));

	gfarm_error_t (*direntry_add)(struct db_direntry_arg *);
	gfarm_error_t (*direntry_remove)(struct db_direntry_arg *);
	gfarm_error_t (*direntry_load)(void *,
		void (*)(void *, gfarm_ino_t, char *, int, gfarm_ino_t));

	gfarm_error_t (*symlink_add)(struct db_symlink_arg *);
	gfarm_error_t (*symlink_remove)(struct db_inode_inum_arg *);
	gfarm_error_t (*symlink_load)(void *,
		void (*)(void *, gfarm_ino_t, char *));

	gfarm_error_t (*xattr_add)(struct db_xattr_arg *);
	gfarm_error_t (*xattr_modify)(struct db_xattr_arg *);
	gfarm_error_t (*xattr_remove)(struct db_xattr_arg *);
	gfarm_error_t (*xattr_removeall)(struct db_xattr_arg *);
	gfarm_error_t (*xattr_get)(struct db_xattr_arg *);
	gfarm_error_t (*xattr_load)(void *,
		void (*)(void *, struct xattr_info *));
	gfarm_error_t (*xmlattr_find)(struct db_xmlattr_find_arg *);
};

struct db_user_auth_arg;
struct db_user_auth_trampoline_closure {
	void *closure;
	void (*callback)(void *, struct db_user_auth_arg *);
};

struct db_inode_cksum_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, char *, size_t, char *);
};

struct db_filecopy_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, char *);
};

struct db_deadfilecopy_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, gfarm_uint64_t, char *);
};

struct db_direntry_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, char *, int, gfarm_ino_t);
};

struct db_symlink_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, char *);
};

struct db_quota_dirset_trampoline_closure {
	void *closure;
	void (*callback)(void *,
	    struct gfarm_dirset_info *, struct quota_metadata *);
};

struct db_quota_dir_trampoline_closure {
	void *closure;
	void (*callback)(void *, gfarm_ino_t, struct gfarm_dirset_info *);
};

extern const struct gfarm_base_generic_info_ops
	db_base_user_auth_arg_ops,
	db_base_inode_cksum_arg_ops,
	db_base_filecopy_arg_ops,
	db_base_deadfilecopy_arg_ops,
	db_base_direntry_arg_ops,
	db_base_symlink_arg_ops,
	db_base_quota_dirset_arg_ops,
	db_base_quota_dir_arg_ops;

void db_user_auth_callback_trampoline(void *, void *);
void db_inode_cksum_callback_trampoline(void *, void *);
void db_filecopy_callback_trampoline(void *, void *);
void db_deadfilecopy_callback_trampoline(void *, void *);
void db_direntry_callback_trampoline(void *, void *);
void db_symlink_callback_trampoline(void *, void *);
void db_quota_dirset_callback_trampoline(void *, void *);
void db_quota_dir_callback_trampoline(void *, void *);

void db_user_auth_arg_free(struct db_user_auth_arg *);


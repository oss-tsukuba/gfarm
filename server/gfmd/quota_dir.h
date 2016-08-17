struct quota_dir;

gfarm_error_t quota_dir_enter(gfarm_ino_t, struct dirset *,
	struct quota_dir **);
gfarm_error_t quota_dir_remove(gfarm_ino_t);
gfarm_error_t quota_dir_remove_in_cache(gfarm_ino_t);
gfarm_ino_t quota_dir_get_inum(struct quota_dir *);
struct dirset;
struct dirset *quota_dir_get_dirset_by_inum(gfarm_ino_t);

/* private interface between quota_dir and dirset */
struct quota_dir *quota_dir_list_new(void);
void quota_dir_list_free(struct quota_dir *);
void quota_dir_foreach_in_dirset(struct quota_dir *,
	void *closure, void (*)(void *, struct quota_dir *));
int quota_dir_foreach_in_dirset_interruptible(struct quota_dir *,
	void *closure, int (*)(void *, struct quota_dir *));

gfarm_error_t gfm_server_quota_dir_get(struct peer *, int, int);
gfarm_error_t gfm_server_quota_dir_set(struct peer *, int, int);

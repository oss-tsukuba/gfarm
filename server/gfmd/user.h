void user_init(void);

struct user;
struct user *user_lookup_including_invalid(const char *);
struct user *user_lookup_or_enter_invalid(const char *);
struct user *user_lookup(const char *);
struct user *user_lookup_gsi_dn(const char *);
char *user_name(struct user *);
char *user_name_with_invalid(struct user *);
char *user_realname(struct user *);
char *user_gsi_dn(struct user *);
int user_is_invalid(struct user *);
int user_is_valid(struct user *);
struct gfarm_user_info;
gfarm_error_t user_enter(struct gfarm_user_info *, struct user **);
gfarm_error_t user_modify(struct user *, struct gfarm_user_info *);
gfarm_error_t user_remove_in_cache(const char *);

void user_foreach(void *, void (*)(void *, struct user *), int);

struct quota;
struct quota *user_quota(struct user *);
struct gfarm_quota_subject_info;
struct gfarm_quota_subject_info *user_usage_tmp(struct user *);

struct dirset;
gfarm_error_t user_enter_dirset(struct user *, const char *, int,
	struct dirset **);
struct dirset *user_lookup_dirset(struct user *, const char *);
gfarm_error_t user_remove_dirset(struct user *, const char *);
struct dirsets;
struct dirsets *user_get_dirsets(struct user *);


extern char ADMIN_USER_NAME[];

struct group;
int user_in_group(struct user *, struct group *);
int user_is_admin(struct user *);
int user_is_root(struct user *);
struct inode;
int user_is_root_for_inode(struct user *, struct inode *);

struct peer;
gfarm_error_t gfm_server_user_info_get_all(struct peer *, int, int);
gfarm_error_t gfm_server_user_info_get_by_names(struct peer *, int, int);
gfarm_error_t gfm_server_user_info_get_by_gsi_dn(struct peer *, int, int);
gfarm_error_t gfm_server_user_info_set(struct peer *, int, int);
gfarm_error_t gfm_server_user_info_modify(struct peer *, int, int);
gfarm_error_t gfm_server_user_info_remove(struct peer *, int, int);

struct group_assignment;
/* subroutine of grpassign_add(), shouldn't be called from elsewhere */
void grpassign_add_group(struct group_assignment *);


/* exported for a use from a private extension */
gfarm_error_t user_info_remove_default(const char *, const char *);
extern gfarm_error_t (*user_info_remove)(const char *, const char *);

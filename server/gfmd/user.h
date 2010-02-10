void user_init(void);

struct user;
struct user *user_lookup(const char *);
struct user *user_lookup_gsi_dn(const char *);
char *user_name(struct user *);
char *user_gsi_dn(struct user *);
int user_is_invalidated(struct user *);
int user_is_active(struct user *);

void user_all(void *, void (*)(void *, struct user *), int);

struct quota;
struct quota *user_quota(struct user *);

extern char ADMIN_USER_NAME[];

struct group;
int user_in_group(struct user *, struct group *);
int user_is_admin(struct user *);
int user_is_root(struct user *);

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

gfarm_error_t user_init(void);

struct user;
struct user *user_lookup(const char *);
char *user_name(struct user *);
int user_is_admin(struct user *);

struct group;
int user_in_group(struct user *, struct group *);

struct peer;
gfarm_error_t gfm_server_user_info_get_all(struct peer *, int);
gfarm_error_t gfm_server_user_info_get_by_names(struct peer *, int);
gfarm_error_t gfm_server_user_info_set(struct peer *, int);
gfarm_error_t gfm_server_user_info_modify(struct peer *, int);
gfarm_error_t gfm_server_user_info_remove(struct peer *, int);
